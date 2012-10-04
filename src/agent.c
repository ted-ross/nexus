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
#include <nexus/agent.h>
#include <nexus/ctools.h>
#include <nexus/hash.h>


typedef struct {
    container_node_t *agent_node;
    hash_t           *target_map;
} agent_context_t;


static agent_context_t agent;


void agent_initialize(void)
{
    node_descriptor_t desc;
    desc.name                = "qpid.container_agent";
    desc.context             = 0;
    desc.rx_handler          = agent_rx_handler;
    desc.tx_handler          = agent_tx_handler;
    desc.incoming_handler    = agent_incoming_link_handler;
    desc.outgoing_handler    = agent_outgoing_link_handler;
    desc.link_closed_handler = agent_link_closed_handler;

    agent.agent_node = container_register_node(desc);
    agent.target_map = hash_initialize(10, 32); // 1K buckets, item batches of 32
}


void agent_finalize(void)
{
    container_unregister_node(agent.agent_node);
}


void agent_incoming_link_handler(void* context, pn_link_t *link)
{
    const char *name  = pn_link_name(link);
    const char *r_tgt = pn_link_remote_target(link);
    const char *r_src = pn_link_remote_source(link);

    printf("[Agent: Opening Incoming Link - name=%s source=%s target=%s]\n", name, r_src, r_tgt);

    pn_link_set_target(link, r_tgt);
    pn_link_flow(link, 1);
    pn_link_open(link);
}


void agent_outgoing_link_handler(void* context, pn_link_t *link)
{
    const char *name  = pn_link_name(link);
    const char *r_tgt = pn_link_remote_target(link);
    const char *r_src = pn_link_remote_source(link);

    printf("[Agent: Opening Outgoing Link - name=%s source=%s target=%s]\n", name, r_src, r_tgt);

    // TODO - Put the target into a hash table of destinations to be matched against reply-tos

    pn_link_set_source(link, r_src);
    pn_link_open(link);
}


void agent_link_closed_handler(void* context, pn_link_t *link)
{
    const char *name  = pn_link_name(link);
    const char *r_tgt = pn_link_remote_target(link);
    const char *r_src = pn_link_remote_source(link);

    printf("[Agent: Link Closed - name=%s source=%s target=%s]\n", name, r_src, r_tgt);

    // TODO - If this is an outgoing link, remove its target from the destination map.
}


void agent_tx_handler(void* context, pn_delivery_t *delivery)
{
    // TODO - Send replies enqueued for this link.
}


void agent_rx_handler(void* context, pn_delivery_t *delivery)
{
#define BUFFER_SIZE 1024
    pn_link_t    *link = pn_delivery_link(delivery);
    ssize_t       rc;
    char          buffer[BUFFER_SIZE];
    pn_message_t *msg = pn_message();

    rc = pn_link_recv(link, buffer, BUFFER_SIZE);
    if (rc > 0) {
        pn_message_decode(msg, buffer, rc);
        printf("[Agent: Message Received - addr=%s reply_to=%s subject=%s content-type=%s]\n",
               pn_message_get_address(msg),
               pn_message_get_reply_to(msg),
               pn_message_get_subject(msg),
               pn_message_get_content_type(msg));
    }

    pn_delivery_update(delivery, PN_ACCEPTED);
    pn_delivery_settle(delivery);
    pn_link_advance(link);

    if (pn_link_credit(link) == 0)
        pn_link_flow(link, 1);

    // TODO - Process the query and generate a response set.
    // TODO - Using the reply-to in the message, find the outgoing link for the responses.
    // TODO - Enqueue the responses onto the outgoing link's buffer.
    // TODO - Notify the concurrent-driver layer that the link should be awakened for send.
}

