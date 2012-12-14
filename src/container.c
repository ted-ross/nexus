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
#include <string.h>
#include <nexus/container.h>
#include <nexus/message.h>
#include <proton/engine.h>
#include <proton/message.h>
#include <nexus/ctools.h>
#include <nexus/hash.h>
#include <nexus/threading.h>
#include <nexus/iterator.h>
#include <nexus/link_allocator.h>

struct container_node_t {
    node_descriptor_t desc;
};

static hash_t           *node_map;
static sys_mutex_t      *lock;
static container_node_t *default_node;

static void container_setup_outgoing_link(pn_link_t *link)
{
    sys_mutex_lock(lock);
    container_node_t    *node;
    int                  result;
    const char          *source = pn_terminus_get_address(pn_link_remote_source(link));
    nx_field_iterator_t *iter;
    // TODO - Extract the name from the structured source

    if (source) {
        iter   = nx_field_iterator_string(source, ITER_VIEW_NODE_ID);
        result = hash_retrieve(node_map, iter, (void*) &node);
        nx_field_iterator_free(iter);
    } else
        result = -1;
    sys_mutex_unlock(lock);

    if (result < 0) {
        if (default_node)
            node = default_node;
        else {
            // Reject the link
            // TODO - When the API allows, add an error message for "no available node"
            pn_link_close(link);
            return;
        }
    }

    nx_link_item_t *item = nx_link_item(link);
    item->container_context = node;

    pn_link_set_context(link, item);
    node->desc.outgoing_handler(node->desc.context, link);
}


static void container_setup_incoming_link(pn_link_t *link)
{
    sys_mutex_lock(lock);
    container_node_t    *node;
    int                  result;
    const char          *target = pn_terminus_get_address(pn_link_remote_target(link));
    nx_field_iterator_t *iter;
    // TODO - Extract the name from the structured target

    if (target) {
        iter   = nx_field_iterator_string(target, ITER_VIEW_NODE_ID);
        result = hash_retrieve(node_map, iter, (void*) &node);
        nx_field_iterator_free(iter);
    } else
        result = -1;
    sys_mutex_unlock(lock);

    if (result < 0) {
        if (default_node)
            node = default_node;
        else {
            // Reject the link
            // TODO - When the API allows, add an error message for "no available node"
            pn_link_close(link);
            return;
        }
    }

    nx_link_item_t *item = nx_link_item(link);
    item->container_context = node;

    pn_link_set_context(link, item);
    node->desc.incoming_handler(node->desc.context, link);
}


static int container_do_writable(pn_link_t *link)
{
    nx_link_item_t *item = (nx_link_item_t*) pn_link_get_context(link);
    if (!item)
        return 0;

    container_node_t *node = (container_node_t*) item->container_context;
    if (!node)
        return 0;

    return node->desc.writable_handler(node->desc.context, link);
}


static void container_process_receive(pn_delivery_t *delivery)
{
    pn_link_t      *link = pn_delivery_link(delivery);
    nx_link_item_t *item = (nx_link_item_t*) pn_link_get_context(link);

    if (item) {
        container_node_t *node = (container_node_t*) item->container_context;
        if (node) {
            node->desc.rx_handler(node->desc.context, delivery, item->node_context);
            return;
        }
    }

    //
    // Reject the delivery if we couldn't find a node to handle it
    //
    pn_link_advance(link);
    pn_link_flow(link, 1);
    pn_delivery_update(delivery, PN_REJECTED);
    pn_delivery_settle(delivery);
}


static void container_do_send(pn_delivery_t *delivery)
{
    pn_link_t      *link = pn_delivery_link(delivery);
    nx_link_item_t *item = (nx_link_item_t*) pn_link_get_context(link);

    if (item) {
        container_node_t *node = (container_node_t*) item->container_context;
        if (node) {
            node->desc.tx_handler(node->desc.context, delivery, item->node_context);
            return;
        }
    }

    // TODO - Cancel the delivery
}


static void container_do_updated(pn_delivery_t *delivery)
{
    pn_link_t      *link = pn_delivery_link(delivery);
    nx_link_item_t *item = (nx_link_item_t*) pn_link_get_context(link);

    if (item) {
        container_node_t *node = (container_node_t*) item->container_context;
        if (node)
            node->desc.disp_handler(node->desc.context, delivery, item->node_context);
    }
}


static int container_close_handler(void* unused, pn_connection_t *conn)
{
    //
    // Close all links, passing False as the 'closed' argument.  These links are not
    // being properly 'detached'.  They are being orphaned.
    //
    pn_link_t *link = pn_link_head(conn, 0);
    while (link) {
        nx_link_item_t   *item = (nx_link_item_t*) pn_link_get_context(link);
        container_node_t *node = (container_node_t*) item->container_context;
        if (node)
            node->desc.link_detach_handler(node->desc.context, link, 0);
        pn_link_close(link);
        link = pn_link_next(link, 0);
    }

    // teardown all sessions
    pn_session_t *ssn = pn_session_head(conn, 0);
    while (ssn) {
        pn_session_close(ssn);
        ssn = pn_session_next(ssn, 0);
    }

    // teardown the connection
    pn_connection_close(conn);
    return 0;
}


static int container_process_handler(void* unused, pn_connection_t *conn)
{
    pn_session_t    *ssn;
    pn_link_t       *link;
    pn_delivery_t   *delivery;
    int              event_count = 0;

    // Step 1: setup the engine's connection, and any sessions and links
    // that may be pending.

    // initialize the connection if it's new
    if (pn_connection_state(conn) & PN_LOCAL_UNINIT) {
        pn_connection_open(conn);
        event_count++;
    }

    // open all pending sessions
    ssn = pn_session_head(conn, PN_LOCAL_UNINIT);
    while (ssn) {
        pn_session_open(ssn);
        ssn = pn_session_next(ssn, PN_LOCAL_UNINIT);
        event_count++;
    }

    // configure and open any pending links
    link = pn_link_head(conn, PN_LOCAL_UNINIT);
    while (link) {
        if (pn_link_is_sender(link))
            container_setup_outgoing_link(link);
        else
            container_setup_incoming_link(link);
        link = pn_link_next(link, PN_LOCAL_UNINIT);
        event_count++;
    }


    // Step 2: Now drain all the pending deliveries from the connection's
    // work queue and process them

    delivery = pn_work_head(conn);
    while (delivery) {
        if      (pn_delivery_readable(delivery))
            container_process_receive(delivery);
        else if (pn_delivery_writable(delivery))
            container_do_send(delivery);

        if (pn_delivery_updated(delivery)) 
            container_do_updated(delivery);

        delivery = pn_work_next(delivery);
        event_count++;
    }

    //
    // Step 2.5: Traverse all of the links on the connection looking for
    // outgoing links with non-zero credit.  Call the attached node's
    // writable handler for such links.
    //
    link = pn_link_head(conn, PN_LOCAL_ACTIVE | PN_REMOTE_ACTIVE);
    while (link) {
        assert(pn_session_connection(pn_link_session(link)) == conn);
        if (pn_link_is_sender(link) && pn_link_credit(link) > 0)
            event_count += container_do_writable(link);
        link = pn_link_next(link, PN_LOCAL_ACTIVE | PN_REMOTE_ACTIVE);
    }

    // Step 3: Clean up any links or sessions that have been closed by the
    // remote.  If the connection has been closed remotely, clean that up
    // also.

    // teardown any terminating links
    link = pn_link_head(conn, PN_LOCAL_ACTIVE | PN_REMOTE_CLOSED);
    while (link) {
        nx_link_item_t   *item = (nx_link_item_t*) pn_link_get_context(link);
        container_node_t *node = (container_node_t*) item->container_context;
        if (node)
            node->desc.link_detach_handler(node->desc.context, link, 1); // TODO - get 'closed' from detach message
        pn_link_close(link);
        link = pn_link_next(link, PN_LOCAL_ACTIVE | PN_REMOTE_CLOSED);
        event_count++;
    }

    // teardown any terminating sessions
    ssn = pn_session_head(conn, PN_LOCAL_ACTIVE | PN_REMOTE_CLOSED);
    while (ssn) {
        pn_session_close(ssn);
        ssn = pn_session_next(ssn, PN_LOCAL_ACTIVE | PN_REMOTE_CLOSED);
        event_count++;
    }

    // teardown the connection if it's terminating
    if (pn_connection_state(conn) == (PN_LOCAL_ACTIVE | PN_REMOTE_CLOSED)) {
        pn_connection_close(conn);
        event_count++;
    }

    return event_count;
}


static int container_handler(void* context, nx_conn_event_t event, nx_connection_t *nx_conn)
{
    pn_connection_t *conn = nx_connection_get_engine(nx_conn);

    switch (event) {
    case NX_CONN_EVENT_LISTENER_OPEN:  printf("L_OPEN\n");   break; // TODO - Propagate these up
    case NX_CONN_EVENT_CONNECTOR_OPEN: printf("C_OPEN\n");   break;
    case NX_CONN_EVENT_CLOSE:          return container_close_handler(context, conn);
    case NX_CONN_EVENT_PROCESS:        return container_process_handler(context, conn);
    }

    return 0;
}


void container_init(void)
{
    printf("[Container Initializing]\n");

    const nx_allocator_config_t *alloc_config = nx_allocator_default_config();
    nx_allocator_initialize(alloc_config);

    node_map = hash_initialize(10, 32); // 1K buckets, item batches of 32
    lock = sys_mutex();
    default_node = 0;
    nx_server_set_conn_handler(container_handler);
}


container_node_t *container_register_node(node_descriptor_t desc)
{
    container_node_t *node = NEW(container_node_t);
    node->desc.name = (char*) malloc(strlen(desc.name) + 1);
    strcpy(node->desc.name, desc.name);
    node->desc.context             = desc.context;
    node->desc.rx_handler          = desc.rx_handler;
    node->desc.tx_handler          = desc.tx_handler;
    node->desc.disp_handler        = desc.disp_handler;
    node->desc.incoming_handler    = desc.incoming_handler;
    node->desc.outgoing_handler    = desc.outgoing_handler;
    node->desc.writable_handler    = desc.writable_handler;
    node->desc.link_detach_handler = desc.link_detach_handler;

    if (strcmp("*", desc.name) == 0) {
        default_node = node;
        printf("[Container: Default Node Registered]\n");
    } else {
        nx_field_iterator_t *iter = nx_field_iterator_string(desc.name, ITER_VIEW_ALL);
        sys_mutex_lock(lock);
        if (hash_insert(node_map, iter, node) < 0) {
            free(node->desc.name);
            free(node);
            node = 0;
        }
        sys_mutex_unlock(lock);
        nx_field_iterator_free(iter);
        printf("[Container: Node Registered - %s]\n", desc.name);
    }

    return node;
}


int container_unregister_node(container_node_t *node)
{
    int result = 0;

    if (strcmp("*", node->desc.name) == 0)
        default_node = 0;
    else {
        nx_field_iterator_t *iter = nx_field_iterator_string(node->desc.name, ITER_VIEW_ALL);
        sys_mutex_lock(lock);
        result = hash_remove(node_map, iter);
        sys_mutex_unlock(lock);
        nx_field_iterator_free(iter);
    }

    free(node->desc.name);
    free(node);
    return result;
}


void container_set_link_context(pn_link_t *link, void *link_context)
{
    nx_link_item_t *item = (nx_link_item_t*) pn_link_get_context(link);
    if (item)
        item->node_context = link_context;
}


void *container_get_link_context(pn_link_t *link)
{
    nx_link_item_t *item = (nx_link_item_t*) pn_link_get_context(link);
    if (item)
        return item->node_context;
    return 0;
}


void container_activate_link(pn_link_t *link)
{
    if (!link)
        return;

    pn_session_t *sess = pn_link_session(link);
    if (!sess)
        return;

    pn_connection_t *conn = pn_session_connection(sess);
    if (!conn)
        return;

    nx_connection_t *ctx = pn_connection_get_context(conn);
    if (!ctx)
        return;

    nx_server_activate(ctx);
}

