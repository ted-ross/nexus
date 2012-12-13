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

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <nexus/timer.h>
#include "test_case.h"
#include <nexus/server.h>
#include <nexus/threading.h>

#define THREAD_COUNT 4
#define OCTET_COUNT  100

static sys_mutex_t *test_lock;

static void *expected_context;
static int   call_count;
static int   threads_seen[THREAD_COUNT];
static char  stored_error[512];

static int   write_count;
static int   read_count;
static int   fd[2];
static nx_user_fd_t *ufd_write;
static nx_user_fd_t *ufd_read;


static void thread_start(void *context, int thread_id)
{
    sys_mutex_lock(test_lock);
    if (context != expected_context && !stored_error[0])
        sprintf(stored_error, "Unexpected Context Value: %lx", (long) context);
    if (thread_id >= THREAD_COUNT && !stored_error[0])
        sprintf(stored_error, "Thread_ID too large: %d", thread_id);
    if (thread_id < 0 && !stored_error[0])
        sprintf(stored_error, "Thread_ID negative: %d", thread_id);

    call_count++;
    if (thread_id >= 0 && thread_id < THREAD_COUNT)
        threads_seen[thread_id]++;

    if (call_count == THREAD_COUNT)
        nx_server_stop();
    sys_mutex_unlock(test_lock);
}


static int conn_handler(void *context, nx_conn_event_t event, nx_connection_t *conn)
{
    return 0;
}


static void ufd_handler(void *context, nx_user_fd_t *ufd)
{
    long    dir = (long) context;
    char    buffer;
    ssize_t len;
    static  int in_read  = 0;
    static  int in_write = 0;

    if (dir == 0) { // READ
        in_read++;
        assert(in_read == 1);
        if (!nx_user_fd_is_readable(ufd_read)) {
            sprintf(stored_error, "Expected Readable");
            nx_server_stop();
        } else {
            len = read(fd[0], &buffer, 1);
            if (len == 1) {
                read_count++;
                if (read_count == OCTET_COUNT)
                    nx_server_stop();
            }
            nx_user_fd_activate_read(ufd_read);
        }
        in_read--;
    } else {        // WRITE
        in_write++;
        assert(in_write == 1);
        if (!nx_user_fd_is_writeable(ufd_write)) {
            sprintf(stored_error, "Expected Writable");
            nx_server_stop();
        } else {
            write(fd[1], "X", 1);

            write_count++;
            if (write_count < OCTET_COUNT)
                nx_user_fd_activate_write(ufd_write);
        }
        in_write--;
    }
}


static void fd_test_start(void *context)
{
    nx_user_fd_activate_read(ufd_read);
}


static char* test_start_handler(void *context)
{
    int i;

    nx_server_initialize(THREAD_COUNT);

    expected_context = (void*) 0x00112233;
    stored_error[0] = 0x0;
    call_count      = 0;
    for (i = 0; i < THREAD_COUNT; i++)
        threads_seen[i] = 0;

    nx_server_set_conn_handler(conn_handler);
    nx_server_set_start_handler(thread_start, expected_context);
    nx_server_run();
    nx_server_finalize();

    if (stored_error[0])            return stored_error;
    if (call_count != THREAD_COUNT) return "Incorrect number of thread-start callbacks";
    for (i = 0; i < THREAD_COUNT; i++)
        if (threads_seen[i] != 1)   return "Incorrect count on one thread ID";

    return 0;
}


static char* test_user_fd(void *context)
{
    int res;
    nx_timer_t *timer;

    nx_server_initialize(THREAD_COUNT);
    nx_server_set_conn_handler(conn_handler);
    nx_server_set_user_fd_handler(ufd_handler);
    timer = nx_timer(fd_test_start, 0);
    nx_timer_schedule(timer, 0);

    stored_error[0] = 0x0;
    res = pipe2(fd, O_NONBLOCK);
    if (res != 0) return "Error creating pipe2";

    ufd_write = nx_user_fd(fd[1], (void*) 1);
    ufd_read  = nx_user_fd(fd[0], (void*) 0);

    nx_server_run();
    nx_timer_free(timer);
    nx_server_finalize();
    close(fd[0]);
    close(fd[1]);

    if (stored_error[0])            return stored_error;
    if (write_count - OCTET_COUNT > 2) sprintf(stored_error, "Excessively high Write Count: %d", write_count);
    if (read_count != OCTET_COUNT)  sprintf(stored_error, "Incorrect Read Count: %d", read_count);;

    if (stored_error[0]) return stored_error;
    return 0;
}


int server_tests(void)
{
    int result = 0;
    test_lock = sys_mutex();

    TEST_CASE(test_start_handler, 0);
    TEST_CASE(test_user_fd, 0);

    sys_mutex_free(test_lock);
    return result;
}

