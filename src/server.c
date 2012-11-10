/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


#include <nexus/ctools.h>
#include <nexus/threading.h>
#include "server_private.h"
#include "timer_private.h"
#include "alloc_private.h"
#include "auth.h"
#include "work_queue.h"
#include "context_pvt.h"
#include <stdio.h>
#include <sys/time.h>
#include <signal.h>

typedef struct nx_thread_t {
    int           thread_id;
    volatile int  running;
    int           using_thread;
    sys_thread_t *thread;
} nx_thread_t;


typedef struct nx_server_t {
    int                     thread_count;
    pn_driver_t            *driver;
    nx_thread_start_cb_t    start_handler;
    nx_conn_handler_cb_t    conn_handler;
    nx_signal_handler_cb_t  signal_handler;
    void                   *handler_context;
    sys_cond_t             *cond;
    sys_mutex_t            *lock;
    nx_thread_t           **threads;
    work_queue_t           *work_queue;
    nx_timer_list_t         pending_timers;
    bool                    a_thread_is_waiting;
    int                     threads_active;
    int                     pause_requests;
    int                     threads_paused;
    int                     pause_next_sequence;
    int                     pause_now_serving;
    int                     pending_signal;
} nx_server_t;


/**
 * Singleton Concurrent Proton Driver object
 */
static nx_server_t *nx_server = 0;


static void signal_handler(int signum)
{
    nx_server->pending_signal = signum;
    sys_cond_signal_all(nx_server->cond);
}


static nx_thread_t *thread(int id)
{
    nx_thread_t *thread = NEW(nx_thread_t);
    if (!thread)
        return 0;

    thread->thread_id    = id;
    thread->running      = 0;
    thread->using_thread = 0;

    return thread;
}


static void thread_process_listeners(pn_driver_t *driver)
{
    pn_listener_t  *listener = pn_driver_listener(driver);
    pn_connector_t *conn;
    context_t      *ctx;

    while (listener) {
        //printf("[Accepting Connection]\n");
        conn = pn_listener_accept(listener);
        ctx = context(CONN_STATE_AUTHENTICATING, conn);
        pn_connector_set_context(conn, ctx);
        listener = pn_driver_listener(driver);
    }
}


static void handle_signals_LH(void)
{
    int signum = nx_server->pending_signal;

    if (signum) {
        nx_server->pending_signal = 0;
        sys_mutex_unlock(nx_server->lock);
        nx_server->signal_handler(nx_server->handler_context, signum);
        sys_mutex_lock(nx_server->lock);
    }
}


static void block_if_paused_LH(void)
{
    if (nx_server->pause_requests > 0) {
        nx_server->threads_paused++;
        sys_cond_signal_all(nx_server->cond);
        while (nx_server->pause_requests > 0)
            sys_cond_wait(nx_server->cond, nx_server->lock);
        nx_server->threads_paused--;
    }
}

//
// TEMPORARY FUNCTION PROTOTYPES
//
void pn_driver_wait_1(pn_driver_t *d);
void pn_driver_wait_2(pn_driver_t *d, int timeout);
void pn_driver_wait_3(pn_driver_t *d);
//
// END TEMPORARY
//

static void *thread_run(void *arg)
{
    nx_thread_t    *thread = (nx_thread_t*) arg;
    pn_connector_t *work;
    context_t      *ctx;

    if (!thread)
        return 0;

    thread->running = 1;

    //
    // Invoke the start handler if the application supplied one.
    // This handler can be used to set NUMA or processor affinnity for the thread.
    //
    if (nx_server->start_handler)
        nx_server->start_handler(nx_server->handler_context, thread->thread_id);

    //
    // Main Loop
    //
    while (thread->running) {
        sys_mutex_lock(nx_server->lock);

        //
        // Check for pending signals to process
        //
        handle_signals_LH();
        if (!thread->running) {
            sys_mutex_unlock(nx_server->lock);
            break;
        }

        //
        // Check to see if the server is pausing.  If so, block here.
        //
        block_if_paused_LH();
        if (!thread->running) {
            sys_mutex_unlock(nx_server->lock);
            break;
        }

        //
        // Service pending timers.
        //
        nx_timer_t *timer = DEQ_HEAD(nx_server->pending_timers);
        if (timer) {
            DEQ_REMOVE_HEAD(nx_server->pending_timers);

            //
            // Mark the timer as idle in case it reschedules itself.
            //
            nx_timer_idle_LH(timer);

            //
            // Release the lock and invoke the connection handler.
            //
            sys_mutex_unlock(nx_server->lock);
            timer->handler(timer->context);
            pn_driver_wakeup(nx_server->driver);
            continue;
        }

        //
        // Check the work queue for connectors scheduled for processing.
        //
        work = work_queue_get(nx_server->work_queue);
        if (!work) {
            //
            // There is no pending work to do
            //
            if (nx_server->a_thread_is_waiting) {
                //
                // Another thread is waiting on the proton driver, this thread must
                // wait on the condition variable until signaled.
                //
                sys_cond_wait(nx_server->cond, nx_server->lock);
            } else {
                //
                // This thread elects itself to wait on the proton driver.  Set the
                // thread-is-waiting flag so other idle threads will not interfere.
                //
                nx_server->a_thread_is_waiting = true;

                //
                // Ask the timer module when its next timer is scheduled to fire.  We'll
                // use this value in driver_wait as the timeout.  If there are no scheduled
                // timers, the returned value will be -1.
                //
                long duration = nx_timer_next_duration_LH();

                //
                // Invoke the proton driver's wait sequence.  This is a bit of a hack for now
                // and will be improved in the future.  The wait process is divided into three parts,
                // the first and third of which need to be non-reentrant, and the second of which
                // must be reentrant (and blocks).
                //
                pn_driver_wait_1(nx_server->driver);
                sys_mutex_unlock(nx_server->lock);
                pn_driver_wait_2(nx_server->driver, duration);
                sys_mutex_lock(nx_server->lock);
                pn_driver_wait_3(nx_server->driver);

                if (!thread->running) {
                    sys_mutex_unlock(nx_server->lock);
                    break;
                }

                //
                // Visit the timer module.  Note that this is probably not a very efficient way
                // to do this.  TODO - make it more efficient.
                //
                struct timeval tv;
                gettimeofday(&tv, 0);
                long milliseconds = tv.tv_sec * 1000 + tv.tv_usec / 1000;
                nx_timer_visit_LH(milliseconds);

                //
                // Process listeners (incoming connections).
                //
                thread_process_listeners(nx_server->driver);

                //
                // Traverse the list of connectors-needing-service from the proton driver.
                // If the connector is not already in the work queue and it is not currently
                // being processed by another thread, put it in the work queue and signal the
                // condition variable.
                //
                work = pn_driver_connector(nx_server->driver);
                while (work) {
                    ctx = pn_connector_context(work);
                    if (!context_get_enqueued(ctx) && context_get_owner_thread(ctx) == CONTEXT_NO_OWNER) {
                        context_set_enqueued(ctx, 1);
                        work_queue_put(nx_server->work_queue, work);
                        sys_cond_signal(nx_server->cond);
                    }
                    work = pn_driver_connector(nx_server->driver);
                }

                //
                // Release our exclusive claim on pn_driver_wait.
                //
                nx_server->a_thread_is_waiting = false;
            }
        }

        //
        // If we were given a connector to work on from the work queue, mark it as
        // owned by this thread and as no longer enqueued.
        //
        if (work) {
            ctx = pn_connector_context(work);
            if (context_get_owner_thread(ctx) == CONTEXT_NO_OWNER) {
                context_set_owner_thread(ctx, thread->thread_id);
                context_set_enqueued(ctx, 0);
                nx_server->threads_active++;
            } else {
                //
                // This connector is being processed by another thread, re-queue it.
                //
                work_queue_put(nx_server->work_queue, work);
                work = 0;
            }
        }
        sys_mutex_unlock(nx_server->lock);

        //
        // Process the connector that we now have exclusive access to.
        //
        if (work) {
            ctx = pn_connector_context(work);
            int events = 0;
            int auth_passes = 0;

            do {
                //
                // Step the engine for pre-handler processing
                //
                pn_connector_process(work);

                //
                // Call the handler that is appropriate for the connector's state.
                //
                if      (context_get_state(ctx) == CONN_STATE_AUTHENTICATING) {
                    if (auth_passes == 0) {
                        auth_handler(work);
                        events = 1;
                    } else {
                        auth_passes++;
                        events = 0;
                    }
                }
                else if (context_get_state(ctx) == CONN_STATE_OPERATIONAL) {
                    if (pn_connector_closed(work)) {
                        nx_server->conn_handler(nx_server->handler_context,
                                                NX_CONN_EVENT_CLOSE,
                                                pn_connector_connection(work));
                        events = 0;
                    }
                    else
                        events = nx_server->conn_handler(nx_server->handler_context,
                                                         NX_CONN_EVENT_PROCESS,
                                                         pn_connector_connection(work));
                }
            } while (events > 0);

            //
            // Check to see if the connector was closed during processing
            //
            if (pn_connector_closed(work)) {
                //
                // Connector is closed.  Free the context and the connector.
                //
                pn_connection_t *conn = pn_connector_connection(work);
                sys_mutex_lock(nx_server->lock);
                context_free(ctx);
                pn_connector_free(work);
                if (conn)
                    pn_connection_free(conn);
                nx_server->threads_active--;
                sys_mutex_unlock(nx_server->lock);
            } else {
                //
                // The connector lives on.  Mark it as no longer owned by this thread.
                //
                sys_mutex_lock(nx_server->lock);
                context_set_owner_thread(ctx, CONTEXT_NO_OWNER);
                nx_server->threads_active--;
                sys_mutex_unlock(nx_server->lock);
            }

            //
            // Wake up the proton driver to force it to reconsider its set of FDs
            // in light of the processing that just occurred.
            //
            pn_driver_wakeup(nx_server->driver);
        }
    }
    return 0;
}


static void thread_start(nx_thread_t *thread)
{
    if (!thread)
        return;

    thread->using_thread = 1;
    thread->thread = sys_thread(thread_run, (void*) thread);
}


static void thread_cancel(nx_thread_t *thread)
{
    if (!thread)
        return;

    thread->running = 0;
}


static void thread_join(nx_thread_t *thread)
{
    if (!thread)
        return;

    if (thread->using_thread)
        sys_thread_join(thread->thread);
}


static void thread_free(nx_thread_t *thread)
{
    if (!thread)
        return;

    free(thread);
}


void nx_server_initialize(int                     thread_count,
                          nx_conn_handler_cb_t    conn_handler,
                          nx_signal_handler_cb_t  signal_handler,
                          nx_thread_start_cb_t    start_handler,
                          void                   *handler_context)
{
    int i;

    if (nx_server)
        return;     // TODO - Fail in a more dramatic way

    nx_alloc_initialize();
    nx_server = NEW(nx_server_t);

    if (!nx_server)
        return;   // TODO - Fail in a more dramatic way

    nx_server->thread_count    = thread_count;
    nx_server->driver          = pn_driver();
    nx_server->start_handler   = start_handler;
    nx_server->conn_handler    = conn_handler;
    nx_server->signal_handler  = signal_handler;
    nx_server->handler_context = handler_context;
    nx_server->lock            = sys_mutex();
    nx_server->cond            = sys_cond();

    nx_timer_initialize(nx_server->lock);

    nx_server->threads = NEW_PTR_ARRAY(nx_thread_t, thread_count);
    for (i = 0; i < thread_count; i++)
        nx_server->threads[i] = thread(i);

    nx_server->work_queue          = work_queue();
    nx_server->a_thread_is_waiting = false;
    nx_server->threads_active      = 0;
    nx_server->pause_requests      = 0;
    nx_server->threads_paused      = 0;
    nx_server->pause_next_sequence = 0;
    nx_server->pause_now_serving   = 0;
    nx_server->pending_signal      = 0;
}


void nx_server_finalize(void)
{
    int i;
    if (!nx_server)
        return;

    for (i = 0; i < nx_server->thread_count; i++)
        thread_free(nx_server->threads[i]);

    work_queue_free(nx_server->work_queue);
    DEQ_INIT(nx_server->pending_timers);

    pn_driver_free(nx_server->driver);
    sys_mutex_free(nx_server->lock);
    sys_cond_free(nx_server->cond);
    free(nx_server);
    nx_server = 0;
}


void nx_server_run(void)
{
    int i;
    if (!nx_server)
        return;

    for (i = 1; i < nx_server->thread_count; i++)
        thread_start(nx_server->threads[i]);

    thread_run((void*) nx_server->threads[0]);

    for (i = 1; i < nx_server->thread_count; i++)
        thread_join(nx_server->threads[i]);
}


void nx_server_stop(void)
{
    int idx;

    for (idx = 0; idx < nx_server->thread_count; idx++)
        thread_cancel(nx_server->threads[idx]);

    sys_cond_signal_all(nx_server->cond);
    pn_driver_wakeup(nx_server->driver);
}


void nx_server_signal(int signum)
{
    signal(signum, signal_handler);
}


void nx_server_pause(void)
{
    sys_mutex_lock(nx_server->lock);

    //
    // Bump the request count to stop all the threads.
    //
    nx_server->pause_requests++;
    int my_sequence = nx_server->pause_next_sequence++;

    //
    // Awaken all threads that are currently blocking.
    //
    sys_cond_signal_all(nx_server->cond);
    pn_driver_wakeup(nx_server->driver);

    //
    // Wait for the paused thread count plus the number of threads requesting a pause to equal
    // the total thread count.  Also, don't exit the blocking loop until now_serving equals our
    // sequence number.  This ensures that concurrent pausers don't run at the same time.
    //
    while ((nx_server->threads_paused + nx_server->pause_requests < nx_server->thread_count) ||
           (my_sequence != nx_server->pause_now_serving))
        sys_cond_wait(nx_server->cond, nx_server->lock);

    sys_mutex_unlock(nx_server->lock);
}


void nx_server_resume(void)
{
    sys_mutex_lock(nx_server->lock);
    nx_server->pause_requests--;
    nx_server->pause_now_serving++;
    sys_cond_signal_all(nx_server->cond);
    sys_mutex_unlock(nx_server->lock);
}


void nx_server_activate(pn_link_t *link)
{
    if (!link)
        return;

    pn_session_t *sess = pn_link_session(link);
    if (!sess)
        return;

    pn_connection_t *conn = pn_session_connection(sess);
    if (!conn)
        return;

    context_t *ctx = pn_connection_get_context(conn);
    if (!ctx)
        return;

    pn_connector_t *ctor = context_get_connector(ctx);
    if (!ctor)
        return;

    if (!pn_connector_closed(ctor))
        pn_connector_activate(ctor, PN_CONNECTOR_WRITABLE);
}


nx_server_listener_t *nx_server_listener(nx_server_config_t *config, void *context)
{
    nx_server_listener_t *li = NEW(nx_server_listener_t);

    if (!li)
        return 0;

    li->config      = config;
    li->context     = context;
    li->pn_listener = pn_listener(nx_server->driver, config->host, config->port, (void*) li);

    if (!li->pn_listener) {
        printf("[Driver Error %d (%s)]\n",
               pn_driver_errno(nx_server->driver), pn_driver_error(nx_server->driver));
        free(li);
        return 0;
    }
    printf("[Server: Listening on %s:%s]\n", config->host, config->port);

    return li;
}


void nx_server_listener_free(nx_server_listener_t* li)
{
    pn_listener_free(li->pn_listener);
    free(li);
}


void nx_server_listener_close(nx_server_listener_t* li)
{
    pn_listener_close(li->pn_listener);
}


nx_server_connector_t *nx_server_connector(nx_server_config_t *config, void *context)
{
    nx_server_connector_t *ct = NEW(nx_server_connector_t);

    if (!ct)
        return 0;

    ct->config       = config;
    ct->context      = context;
    ct->pn_connector = pn_connector(nx_server->driver, config->host, config->port, (void*) ct);

    if (!ct->pn_connector) {
        printf("[Driver Error %d (%s)]\n",
               pn_driver_errno(nx_server->driver), pn_driver_error(nx_server->driver));
        free(ct);
        return 0;
    }
    printf("[Server: Connecting to %s:%s]\n", config->host, config->port);

    return ct;
}


void nx_server_connector_free(nx_server_connector_t* ct)
{
    // Don't free the proton connector.  This will be done by the connector
    // processing/cleanup.
    free(ct);
}


void nx_server_connector_close(nx_server_connector_t* ct)
{
    pn_connector_close(ct->pn_connector);
}


void nx_server_timer_pending_LH(nx_timer_t *timer)
{
    DEQ_INSERT_TAIL(nx_server->pending_timers, timer);
}


void nx_server_timer_cancel_LH(nx_timer_t *timer)
{
    DEQ_REMOVE(nx_server->pending_timers, timer);
}

