#ifndef __container_h__
#define __container_h__ 1
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

#include <proton/engine.h>
#include <nexus/server.h>

typedef struct container_node_t container_node_t;
typedef void (*container_delivery_handler_t)    (void* context, pn_delivery_t *delivery, void *link_context);
typedef int  (*container_link_handler_t)        (void* context, pn_link_t *link);
typedef int  (*container_link_detach_handler_t) (void* context, pn_link_t *link, int closed);

typedef struct {
    char                            *name;
    void                            *context;
    container_delivery_handler_t     rx_handler;
    container_delivery_handler_t     tx_handler;
    container_delivery_handler_t     disp_handler;
    container_link_handler_t         incoming_handler;
    container_link_handler_t         outgoing_handler;
    container_link_handler_t         writable_handler;
    container_link_detach_handler_t  link_detach_handler;
} node_descriptor_t;

void container_init(void);
int  container_handler(void* context, nx_conn_event_t event, pn_connection_t *conn);

container_node_t *container_register_node(node_descriptor_t desc);
int container_unregister_node(container_node_t *node);
void container_set_link_context(pn_link_t *link, void *link_context);
void *container_get_link_context(pn_link_t *link);

#endif
