#ifndef __nexus_link_allocator_h__
#define __nexus_link_allocator_h__ 1
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
#include <nexus/message.h>
#include <nexus/ctools.h>

typedef struct nx_link_item_t {
    DEQ_LINKS(struct nx_link_item_t);
    pn_link_t         *link;
    nx_message_list_t  message_fifo;
} nx_link_item_t;

typedef DEQ(nx_link_item_t) nx_link_list_t;

void            nx_link_allocator_initialize(void);
void            nx_link_allocator_finalize(void);
nx_link_item_t *nx_link_item(pn_link_t *link);
void            nx_link_item_free(nx_link_item_t *item);

#endif
