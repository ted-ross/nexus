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
#include <proton/message.h>
#include <nexus/basic_queue.h>
#include <nexus/message.h>
#include <nexus/threading.h>
#include <nexus/ctools.h>
#include <nexus/hash.h>
#include <nexus/link_allocator.h>


struct basic_queue_t {
    node_descriptor_t            desc;
    container_node_t            *node;
    basic_queue_configuration_t *config;
    nx_link_list_t               in_links;
    nx_link_list_t               out_links;
    nx_message_list_t            fifo;
    sys_mutex_t                 *lock;
    unsigned long                in_messages;
    unsigned long                in_transfers;
    unsigned long                out_messages;
    unsigned long                out_transfers;
};

static size_t enqueue_message_LH(basic_queue_t *bq, nx_message_t *msg)
{
    DEQ_INSERT_TAIL(bq->fifo, msg);

    printf("[Basic Queue %s: Message Enqueued, depth=%d]\n", bq->desc.name, (int) DEQ_SIZE(bq->fifo));
    return DEQ_SIZE(bq->fifo);
}


static void bq_tx_handler(void* context, pn_delivery_t *delivery)
{
    basic_queue_t  *bq   = (basic_queue_t*) context;
    pn_link_t      *link = pn_delivery_link(delivery);
    nx_message_t   *msg;
    nx_buffer_t    *buf;
    size_t          size;

    sys_mutex_lock(bq->lock);
    msg = DEQ_HEAD(bq->fifo);
    if (!msg) {
        // TODO - Recind the delivery
        sys_mutex_unlock(bq->lock);
        return;
    }

    DEQ_REMOVE_HEAD(bq->fifo);

    buf = DEQ_HEAD(msg->buffers);
    while (buf) {
        DEQ_REMOVE_HEAD(msg->buffers);
        pn_link_send(link, nx_buffer_base(buf), nx_buffer_size(buf));
        nx_free_buffer(buf);
        buf = DEQ_HEAD(msg->buffers);
    }

    nx_free_message(msg); // TODO - Move to 'archived' state, don't free
    size = (DEQ_SIZE(bq->fifo));
    sys_mutex_unlock(bq->lock);

    bq->out_transfers++;
    bq->out_messages++;
    pn_link_advance(link);
    pn_link_offered(link, size);

    printf("[Basic Queue %s: Message Dequeued, depth=%d in=%ld out=%ld]\n",
           bq->desc.name, (int) size, bq->in_messages, bq->out_messages);
}


static void bq_rx_handler(void* context, pn_delivery_t *delivery)
{
    basic_queue_t  *bq = (basic_queue_t*) context;
    pn_link_t      *link = pn_delivery_link(delivery);
    nx_message_t   *msg;
    nx_link_item_t *item;

    //
    // Receive the message into a local representation.  If the returned message
    // pointer is NULL, we have not yet received a complete message.
    //
    sys_mutex_lock(bq->lock);
    msg = nx_message_receive(delivery);
    if (msg) {
        //
        // Find the appropriate FIFO to put the message in and enqueue it.
        //
        size_t size = enqueue_message_LH(bq, msg);

        item = DEQ_HEAD(bq->out_links);
        while (item) {
            pn_link_offered(item->link, size);
            item = item->next;
        }
    }
    sys_mutex_unlock(bq->lock);

    bq->in_transfers++;

    if (!msg)
        return;

    bq->in_messages++;

    // TODO - add more smarts to the acceptance of messages.
    pn_link_advance(link);
    pn_link_flow(link, 1);
    pn_delivery_update(delivery, PN_ACCEPTED);
    pn_delivery_settle(delivery);
}


static void bq_disp_handler(void* context, pn_delivery_t *delivery)
{
    //basic_queue_t *bq = (basic_queue_t*) context;
    //pn_link_t     *link = pn_link(delivery);

    pn_delivery_settle(delivery);
}


static void bq_incoming_link_handler(void* context, pn_link_t *link)
{
    basic_queue_t  *bq    = (basic_queue_t*) context;
    const char     *name  = pn_link_name(link);
    const char     *r_tgt = pn_link_remote_target(link);
    const char     *r_src = pn_link_remote_source(link);
    nx_link_item_t *item  = nx_link_item(link);

    sys_mutex_lock(bq->lock);
    if (item) {
        printf("[Basic Queue %s: Opening Incoming Link - name=%s source=%s target=%s]\n", 
               bq->desc.name, name, r_src, r_tgt);

        DEQ_INSERT_TAIL(bq->in_links, item);

        pn_link_set_target(link, r_tgt);
        pn_link_set_source(link, r_src);
        pn_link_flow(link, 8);
        pn_link_open(link);
    } else {
        pn_link_close(link);
    }
    sys_mutex_unlock(bq->lock);
}


static void bq_outgoing_link_handler(void* context, pn_link_t *link)
{
    basic_queue_t  *bq = (basic_queue_t*) context;
    const char     *name  = pn_link_name(link);
    const char     *r_tgt = pn_link_remote_target(link);
    const char     *r_src = pn_link_remote_source(link);
    nx_link_item_t *item  = nx_link_item(link);

    sys_mutex_lock(bq->lock);
    if (item) {
        printf("[Basic Queue %s: Opening Outgoing Link - name=%s source=%s target=%s]\n",
               bq->desc.name, name, r_src, r_tgt);

        DEQ_INSERT_TAIL(bq->out_links, item);

        pn_link_set_target(link, r_tgt);
        pn_link_set_source(link, r_src);
        pn_link_open(link);
    } else {
        pn_link_close(link);
    }
    sys_mutex_unlock(bq->lock);
}


static void bq_writable_link_handler(void* context, pn_link_t *link)
{
    basic_queue_t *bq = (basic_queue_t*) context;
    int            grant_delivery = 0;
    pn_delivery_t *delivery;

    sys_mutex_lock(bq->lock);
    if (DEQ_SIZE(bq->fifo) > 0)
        grant_delivery = 1;
    sys_mutex_unlock(bq->lock);

    if (grant_delivery) {
        pn_delivery(link, pn_dtag("delivery-xxx", 13)); // TODO - use a unique delivery tag
        delivery = pn_link_current(link);
        if (delivery)
            bq_tx_handler(context, delivery);
    }
}


static void bq_link_closed_handler(void* context, pn_link_t *link)
{
    basic_queue_t  *bq = (basic_queue_t*) context;
    const char     *name  = pn_link_name(link);
    const char     *r_tgt = pn_link_remote_target(link);
    const char     *r_src = pn_link_remote_source(link);
    nx_link_item_t *item;

    printf("[Basic Queue %s: Link Closed - name=%s source=%s target=%s]\n",
           bq->desc.name, name, r_src, r_tgt);

    sys_mutex_lock(bq->lock);
    if (pn_link_is_sender(link))
        item = DEQ_HEAD(bq->out_links);
    else
        item = DEQ_HEAD(bq->in_links);

    while (item) {
        if (item->link == link) {
            if (pn_link_is_sender(link)) {
                DEQ_REMOVE(bq->out_links, item);
            } else {
                DEQ_REMOVE(bq->in_links, item);
            }
            nx_link_item_free(item);
            break;
        }
        item = item->next;
    }

    sys_mutex_unlock(bq->lock);
}


basic_queue_t *basic_queue(char *name, basic_queue_configuration_t *config)
{
    basic_queue_t *bq = NEW(basic_queue_t);

    bq->desc.name                = name;
    bq->desc.context             = (void*) bq;
    bq->desc.rx_handler          = bq_rx_handler;
    bq->desc.tx_handler          = bq_tx_handler;
    bq->desc.disp_handler        = bq_disp_handler;
    bq->desc.incoming_handler    = bq_incoming_link_handler;
    bq->desc.outgoing_handler    = bq_outgoing_link_handler;
    bq->desc.writable_handler    = bq_writable_link_handler;
    bq->desc.link_closed_handler = bq_link_closed_handler;

    bq->node = container_register_node(bq->desc);
    if (!bq->node) {
        free(bq);
        return 0;
    }

    DEQ_INIT(bq->in_links);
    DEQ_INIT(bq->out_links);
    DEQ_INIT(bq->fifo);

    bq->config = config;
    bq->lock   = sys_mutex();

    bq->in_messages   = 0;
    bq->in_transfers  = 0;
    bq->out_messages  = 0;
    bq->out_transfers = 0;

    return bq;
}


void basic_queue_free(basic_queue_t *bq)
{
    sys_mutex_free(bq->lock);
    free(bq);
}


void bq_finalize(basic_queue_t *bq)
{
    container_unregister_node(bq->node);
    sys_mutex_free(bq->lock);
    // TODO - Close all attached links
    free(bq);
}

