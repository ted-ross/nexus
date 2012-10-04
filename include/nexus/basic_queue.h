#ifndef __nodes_basic_queue_h__
#define __nodes_basic_queue_h__ 1
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
#include <nexus/container.h>

typedef struct basic_queue_t basic_queue_t;

typedef enum {
    ACK_POLICY_LOCAL,      // Accept and settle messages once enqueued
    ACK_POLICY_EXTENDED,   // Delay settlement until extensions also settle
    ACK_POLICY_TRANSITIVE  // Delay settlement until downstream settles, use downstream disposition
} nx_queue_ack_policy_t;

typedef struct {
    size_t                 message_limit;
    size_t                 memory_limit;
    nx_queue_ack_policy_t  ack_policy;
    // delivery policy (one consumer, round-robin, best-effort, message-group)
    //   message-group-header-name
    // distribution-modes (copy-only, move-only, both)
    // number of priorities
    // priority policy (weighted, strict)
    // enqueue policy (tail, head, last-value)
    //   last-value-header-name
    // limit policy (flow-control, replace-oldest)
    // retransmission limit
} basic_queue_configuration_t;

basic_queue_t *basic_queue(char *name, basic_queue_configuration_t *config);
void           basic_queue_free(basic_queue_t *bq);

#endif
