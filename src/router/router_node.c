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
#include <nexus/message.h>
#include <nexus/link_allocator.h>
#include <nexus/threading.h>
#include <nexus/timer.h>
#include <nexus/ctools.h>

struct nx_router_t {
    node_descriptor_t  desc;
    container_node_t  *node;
    nx_link_list_t     in_links;
    nx_link_list_t     out_links;
    nx_message_list_t  in_fifo;
    nx_message_list_t  out_fifo;
    sys_mutex_t       *lock;
    nx_timer_t        *timer;
};


static void router_tx_handler(void* context, pn_delivery_t *delivery)
{
    nx_router_t    *router = (nx_router_t*) context;
    pn_link_t      *link   = pn_delivery_link(delivery);
    nx_message_t   *msg;
    nx_buffer_t    *buf;
    size_t          size;

    sys_mutex_lock(router->lock);
    msg = DEQ_HEAD(router->out_fifo);
    if (!msg) {
        // TODO - Recind the delivery
        sys_mutex_unlock(router->lock);
        return;
    }

    DEQ_REMOVE_HEAD(router->out_fifo);

    buf = DEQ_HEAD(msg->buffers);
    while (buf) {
        DEQ_REMOVE_HEAD(msg->buffers);
        pn_link_send(link, (char*) nx_buffer_base(buf), nx_buffer_size(buf));
        nx_free_buffer(buf);
        buf = DEQ_HEAD(msg->buffers);
    }

    nx_free_message(msg); // TODO - Move to 'archived' state, don't free
    size = (DEQ_SIZE(router->out_fifo));
    sys_mutex_unlock(router->lock);

    pn_link_advance(link);
    pn_link_offered(link, size);
}


static void router_rx_handler(void* context, pn_delivery_t *delivery)
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
    if (msg) {
        valid_message = nx_message_check(msg);
        if (valid_message) {
            DEQ_INSERT_TAIL(router->in_fifo, msg);
        }
    }
    sys_mutex_unlock(router->lock);

    if (!msg)
        return;

    nx_field_iterator_t iter;
    int to_present = nx_message_field_to(msg, &iter);
    if (to_present) {
        printf("TO: ");
        unsigned char c;
        do {
            c = nx_field_iterator_octet(&iter);
            printf("%c", c);
        } while (nx_field_iterator_next(&iter));
        printf("\n");
    }

    pn_link_advance(link);
    pn_link_flow(link, 1);
    if (valid_message)
        pn_delivery_update(delivery, PN_ACCEPTED);
    else
        pn_delivery_update(delivery, PN_REJECTED);
    pn_delivery_settle(delivery);
}


static void router_disp_handler(void* context, pn_delivery_t *delivery)
{
    //nx_router_t *router = (nx_router_t*) context;
    //pn_link_t     *link = pn_link(delivery);

    pn_delivery_settle(delivery);
}


static void router_incoming_link_handler(void* context, pn_link_t *link)
{
    nx_router_t    *router = (nx_router_t*) context;
    const char     *name   = pn_link_name(link);
    const char     *r_tgt  = pn_link_remote_target(link);
    const char     *r_src  = pn_link_remote_source(link);
    nx_link_item_t *item   = nx_link_item(link);

    sys_mutex_lock(router->lock);
    if (item) {
        printf("[Router %s: Opening Incoming Link - name=%s source=%s target=%s]\n", 
               router->desc.name, name, r_src, r_tgt);

        DEQ_INSERT_TAIL(router->in_links, item);

        pn_link_set_target(link, r_tgt);
        pn_link_set_source(link, r_src);
        pn_link_flow(link, 8);
        pn_link_open(link);
    } else {
        pn_link_close(link);
    }
    sys_mutex_unlock(router->lock);
}


static void router_outgoing_link_handler(void* context, pn_link_t *link)
{
    nx_router_t    *router = (nx_router_t*) context;
    const char     *name   = pn_link_name(link);
    const char     *r_tgt  = pn_link_remote_target(link);
    const char     *r_src  = pn_link_remote_source(link);
    nx_link_item_t *item   = nx_link_item(link);

    sys_mutex_lock(router->lock);
    if (item) {
        printf("[Router %s: Opening Outgoing Link - name=%s source=%s target=%s]\n",
               router->desc.name, name, r_src, r_tgt);

        DEQ_INSERT_TAIL(router->out_links, item);

        pn_link_set_target(link, r_tgt);
        pn_link_set_source(link, r_src);
        pn_link_open(link);
    } else {
        pn_link_close(link);
    }
    sys_mutex_unlock(router->lock);
}


static void router_writable_link_handler(void* context, pn_link_t *link)
{
    nx_router_t *router = (nx_router_t*) context;
    int            grant_delivery = 0;
    pn_delivery_t *delivery;

    sys_mutex_lock(router->lock);
    if (DEQ_SIZE(router->out_fifo) > 0)
        grant_delivery = 1;
    sys_mutex_unlock(router->lock);

    if (grant_delivery) {
        pn_delivery(link, pn_dtag("delivery-xxx", 13)); // TODO - use a unique delivery tag
        delivery = pn_link_current(link);
        if (delivery)
            router_tx_handler(context, delivery);
    }
}


static void router_link_closed_handler(void* context, pn_link_t *link)
{
    nx_router_t    *router = (nx_router_t*) context;
    const char     *name   = pn_link_name(link);
    const char     *r_tgt  = pn_link_remote_target(link);
    const char     *r_src  = pn_link_remote_source(link);
    nx_link_item_t *item;

    printf("[Router %s: Link Closed - name=%s source=%s target=%s]\n",
           router->desc.name, name, r_src, r_tgt);

    sys_mutex_lock(router->lock);
    if (pn_link_is_sender(link))
        item = DEQ_HEAD(router->out_links);
    else
        item = DEQ_HEAD(router->in_links);

    while (item) {
        if (item->link == link) {
            if (pn_link_is_sender(link)) {
                DEQ_REMOVE(router->out_links, item);
            } else {
                DEQ_REMOVE(router->in_links, item);
            }
            nx_link_item_free(item);
            break;
        }
        item = item->next;
    }

    sys_mutex_unlock(router->lock);
}


static void nx_router_timer_handler(void *context)
{
    nx_router_t *router = (nx_router_t*) context;

    //
    // Periodic processing.
    //

    nx_timer_schedule(router->timer, 1000);
}


nx_router_t *nx_router(char *name, nx_router_configuration_t *config)
{
    nx_router_t *router = NEW(nx_router_t);

    router->desc.name                = name;
    router->desc.context             = (void*) router;
    router->desc.rx_handler          = router_rx_handler;
    router->desc.tx_handler          = router_tx_handler;
    router->desc.disp_handler        = router_disp_handler;
    router->desc.incoming_handler    = router_incoming_link_handler;
    router->desc.outgoing_handler    = router_outgoing_link_handler;
    router->desc.writable_handler    = router_writable_link_handler;
    router->desc.link_closed_handler = router_link_closed_handler;

    router->node = container_register_node(router->desc);
    if (!router->node) {
        free(router);
        return 0;
    }

    DEQ_INIT(router->in_links);
    DEQ_INIT(router->out_links);
    DEQ_INIT(router->in_fifo);
    DEQ_INIT(router->out_fifo);

    router->lock = sys_mutex();

    router->timer = nx_timer(nx_router_timer_handler, (void*) router);
    nx_timer_schedule(router->timer, 0); // Immediate

    return router;
}


void nx_router_free(nx_router_t *router)
{
    sys_mutex_free(router->lock);
    free(router);
}

