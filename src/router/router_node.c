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

#include <stdio.h>
#include <nexus/router.h>
#include <nexus/server.h>
#include <nexus/message.h>
#include <nexus/link_allocator.h>
#include <nexus/threading.h>
#include <nexus/timer.h>
#include <nexus/ctools.h>
#include <nexus/hash.h>
#include <nexus/iterator.h>

static void router_rx_handler(void* context, pn_delivery_t *delivery, void *link_context);
static void router_tx_handler(void* context, pn_delivery_t *delivery, void *link_context);
static void router_disp_handler(void* context, pn_delivery_t *delivery, void *link_context);
static int  router_incoming_link_handler(void* context, pn_link_t *link);
static int  router_outgoing_link_handler(void* context, pn_link_t *link);
static int  router_writable_link_handler(void* context, pn_link_t *link);
static int  router_link_detach_handler(void* context, pn_link_t *link, int closed);

static nx_node_type_t router_node = {"router", 0, 0,
                                     router_rx_handler,
                                     router_tx_handler,
                                     router_disp_handler,
                                     router_incoming_link_handler,
                                     router_outgoing_link_handler,
                                     router_writable_link_handler,
                                     router_link_detach_handler };
static int type_registered = 0;

struct nx_router_t {
    nx_node_t         *node;
    nx_link_list_t     in_links;
    nx_link_list_t     out_links;
    nx_message_list_t  in_fifo;
    sys_mutex_t       *lock;
    nx_timer_t        *timer;
    hash_t            *out_hash;
    uint64_t           dtag;
};


typedef struct {
    pn_link_t         *link;
    nx_message_list_t  out_fifo;
} nx_router_link_t;


static void router_tx_handler(void* context, pn_delivery_t *delivery, void *link_context)
{
    nx_router_t      *router = (nx_router_t*) context;
    pn_link_t        *link   = pn_delivery_link(delivery);
    nx_router_link_t *rlink = (nx_router_link_t*) link_context;
    nx_message_t     *msg;
    nx_buffer_t      *buf;
    size_t            size;

    sys_mutex_lock(router->lock);
    msg = DEQ_HEAD(rlink->out_fifo);
    if (!msg) {
        // TODO - Recind the delivery
        sys_mutex_unlock(router->lock);
        return;
    }

    DEQ_REMOVE_HEAD(rlink->out_fifo);

    buf = DEQ_HEAD(msg->buffers);
    while (buf) {
        DEQ_REMOVE_HEAD(msg->buffers);
        pn_link_send(link, (char*) nx_buffer_base(buf), nx_buffer_size(buf));
        nx_free_buffer(buf);
        buf = DEQ_HEAD(msg->buffers);
    }

    pn_delivery_set_context(delivery, (void*) msg);

    //
    // If there is no incoming delivery, it was pre-settled.  In this case,
    // we must pre-settle the outgoing delivery as well.
    //
    if (msg->in_delivery)
        msg->out_delivery = delivery;
    else
        pn_delivery_settle(delivery);

    size = (DEQ_SIZE(rlink->out_fifo));
    sys_mutex_unlock(router->lock);

    pn_link_advance(link);
    pn_link_offered(link, size);
}


static void router_rx_handler(void* context, pn_delivery_t *delivery, void *link_context)
{
    nx_router_t  *router = (nx_router_t*) context;
    pn_link_t    *link   = pn_delivery_link(delivery);
    nx_message_t *msg;
    int           valid_message = 0;

    //
    // Receive the message into a local representation.  If the returned message
    // pointer is NULL, we have not yet received a complete message.
    //
    sys_mutex_lock(router->lock);
    msg = nx_message_receive(delivery);
    sys_mutex_unlock(router->lock);

    if (!msg)
        return;

    valid_message = nx_message_check(msg);

    pn_link_advance(link);
    pn_link_flow(link, 1);

    if (valid_message) {
        nx_field_iterator_t *iter = nx_message_field_to(msg);
        nx_router_link_t    *rlink;
        if (iter) {
            nx_field_iterator_reset(iter, ITER_VIEW_NO_HOST);
            sys_mutex_lock(router->lock);
            int result = hash_retrieve(router->out_hash, iter, (void*) &rlink);
            nx_field_iterator_free(iter);

            if (result == 0) {
                DEQ_INSERT_TAIL(rlink->out_fifo, msg);
                pn_link_offered(rlink->link, DEQ_SIZE(rlink->out_fifo));
                nx_container_activate_link(rlink->link);
            } else {
                pn_delivery_update(delivery, PN_RELEASED);
                pn_delivery_settle(delivery);
            }

            sys_mutex_unlock(router->lock);
        }
    } else {
        pn_delivery_update(delivery, PN_REJECTED);
        pn_delivery_settle(delivery);
    }
}


static void router_disp_handler(void* context, pn_delivery_t *delivery, void *link_context)
{
    pn_link_t *link = pn_delivery_link(delivery);

    if (pn_link_is_sender(link)) {
        pn_disposition_t  disp = pn_delivery_remote_state(delivery);
        nx_message_t     *msg  = pn_delivery_get_context(delivery);
        pn_delivery_t    *activate = 0;

        if (msg) {
            assert(delivery == msg->out_delivery);
            if (disp != 0) {
                activate = msg->in_delivery;
                pn_delivery_update(msg->in_delivery, disp);
                // TODO - handling of the data accompanying RECEIVED/MODIFIED
            }

            if (pn_delivery_settled(delivery)) {
                activate = msg->in_delivery;
                pn_delivery_settle(msg->in_delivery);
                pn_delivery_settle(delivery);
                nx_free_message(msg);
            }

            if (activate)
                nx_container_activate_link(pn_delivery_link(activate));

            return;
        }
    }

    pn_delivery_settle(delivery);
}


static int router_incoming_link_handler(void* context, pn_link_t *link)
{
    nx_router_t    *router = (nx_router_t*) context;
    nx_link_item_t *item   = nx_link_item(link);

    sys_mutex_lock(router->lock);
    if (item) {
        DEQ_INSERT_TAIL(router->in_links, item);
        pn_terminus_copy(pn_link_source(link), pn_link_remote_source(link));
        pn_terminus_copy(pn_link_target(link), pn_link_remote_target(link));
        pn_link_flow(link, 8);
        pn_link_open(link);
    } else {
        pn_link_close(link);
    }
    sys_mutex_unlock(router->lock);
    return 0;
}


static int router_outgoing_link_handler(void* context, pn_link_t *link)
{
    nx_router_t *router = (nx_router_t*) context;
    const char  *r_tgt  = pn_terminus_get_address(pn_link_remote_target(link));

    sys_mutex_lock(router->lock);
    nx_router_link_t *rlink = NEW(nx_router_link_t);
    rlink->link = link;
    DEQ_INIT(rlink->out_fifo);
    nx_container_set_link_context(link, rlink);

    nx_field_iterator_t *iter = nx_field_iterator_string(r_tgt, ITER_VIEW_NO_HOST);
    int result = hash_insert(router->out_hash, iter, rlink);
    nx_field_iterator_free(iter);

    if (result == 0) {
        pn_terminus_copy(pn_link_source(link), pn_link_remote_source(link));
        pn_terminus_copy(pn_link_target(link), pn_link_remote_target(link));
        pn_link_open(link);
        sys_mutex_unlock(router->lock);
        printf("[Router - Registered new local address: %s]\n", r_tgt);
        return 0;
    }

    pn_link_close(link);
    sys_mutex_unlock(router->lock);
    return 0;
}


static int router_writable_link_handler(void* context, pn_link_t *link)
{
    nx_router_t      *router = (nx_router_t*) context;
    int               grant_delivery = 0;
    pn_delivery_t    *delivery;
    nx_router_link_t *rlink = (nx_router_link_t*) nx_container_get_link_context(link);
    uint64_t          tag;

    sys_mutex_lock(router->lock);
    if (DEQ_SIZE(rlink->out_fifo) > 0) {
        grant_delivery = 1;
        tag = router->dtag++;
    }
    sys_mutex_unlock(router->lock);

    if (grant_delivery) {
        pn_delivery(link, pn_dtag((char*) &tag, 8));
        delivery = pn_link_current(link);
        if (delivery) {
            void *link_context = nx_container_get_link_context(link);
            router_tx_handler(context, delivery, link_context);
            return 1;
        }
    }

    return 0;
}


static int router_link_detach_handler(void* context, pn_link_t *link, int closed)
{
    nx_router_t    *router = (nx_router_t*) context;
    const char     *r_tgt  = pn_terminus_get_address(pn_link_remote_target(link));
    nx_link_item_t *item;

    sys_mutex_lock(router->lock);
    if (pn_link_is_sender(link)) {
        item = DEQ_HEAD(router->out_links);

        nx_field_iterator_t *iter = nx_field_iterator_string(r_tgt, ITER_VIEW_NO_HOST);
        hash_remove(router->out_hash, iter);
        nx_field_iterator_free(iter);
        printf("[Router - Removed local address: %s]\n", r_tgt);
    }
    else
        item = DEQ_HEAD(router->in_links);

    while (item) {
        if (item->link == link) {
            if (pn_link_is_sender(link))
                DEQ_REMOVE(router->out_links, item);
            else
                DEQ_REMOVE(router->in_links, item);
            nx_link_item_free(item);
            break;
        }
        item = item->next;
    }

    sys_mutex_unlock(router->lock);
    return 0;
}


static void nx_router_timer_handler(void *context)
{
    nx_router_t *router = (nx_router_t*) context;

    //
    // Periodic processing.
    //
    nx_timer_schedule(router->timer, 1000);
}


nx_router_t *nx_router(nx_router_configuration_t *config)
{
    if (!type_registered) {
        type_registered = 1;
        nx_container_register_node_type(&router_node);
    }

    nx_router_t *router = NEW(nx_router_t);
    nx_container_set_default_node_type(&router_node, (void*) router, NX_DIST_BOTH);

    DEQ_INIT(router->in_links);
    DEQ_INIT(router->out_links);
    DEQ_INIT(router->in_fifo);

    router->lock = sys_mutex();

    router->timer = nx_timer(nx_router_timer_handler, (void*) router);
    nx_timer_schedule(router->timer, 0); // Immediate

    router->out_hash = hash(10, 32, 0);
    router->dtag = 1;

    return router;
}


void nx_router_free(nx_router_t *router)
{
    nx_container_set_default_node_type(0, 0, NX_DIST_BOTH);
    sys_mutex_free(router->lock);
    free(router);
}

