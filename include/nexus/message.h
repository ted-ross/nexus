#ifndef __nexus_message_h__
#define __nexus_message_h__ 1
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
#include <nexus/ctools.h>

typedef struct nx_message_t nx_message_t;
typedef struct nx_buffer_t  nx_buffer_t;

typedef DEQ(nx_buffer_t)  nx_buffer_list_t;
typedef DEQ(nx_message_t) nx_message_list_t;

struct nx_message_t {
    DEQ_LINKS(nx_message_t);
    nx_buffer_list_t  buffers;
    pn_delivery_t    *in_delivery;
    pn_delivery_t    *out_delivery;
};

struct nx_buffer_t {
    DEQ_LINKS(nx_buffer_t);
    unsigned int size;
};

typedef struct {
    size_t        buffer_size;
    unsigned long buffer_preallocation_count;
    unsigned long buffer_rebalancing_batch_count;
    unsigned long buffer_local_storage_max;
    unsigned long buffer_free_list_max;
    unsigned long message_allocation_batch_count;
    unsigned long message_rebalancing_batch_count;
    unsigned long message_local_storage_max;
} nx_allocator_config_t;

const nx_allocator_config_t *nx_allocator_default_config(void);

void nx_allocator_initialize(const nx_allocator_config_t *config);
void nx_allocator_finalize(void);

//
// Functions for per-thread allocators.
//
nx_message_t *nx_allocate_message(void);
nx_buffer_t  *nx_allocate_buffer(void);
void          nx_free_message(nx_message_t *msg);
void          nx_free_buffer(nx_buffer_t *buf);

//
// Append a buffer to the message's buffer chain
//
void          nx_message_add_buffer(nx_message_t *msg, nx_buffer_t *buf);
nx_message_t *nx_message_receive(pn_delivery_t *delivery);

char   *nx_buffer_base(nx_buffer_t *buf);      // Pointer to the first octet in the buffer
char   *nx_buffer_cursor(nx_buffer_t *buf);    // Pointer to the first free octet in the buffer
size_t  nx_buffer_capacity(nx_buffer_t *buf);  // Size of free space in the buffer in octets
size_t  nx_buffer_size(nx_buffer_t *buf);      // Number of octets in the buffer
void    nx_buffer_insert(nx_buffer_t *buf, size_t len);  // Notify the buffer that 'len' octets were written at cursor

#endif
