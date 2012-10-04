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

#include <nexus/message.h>
#include <nexus/ctools.h>
#include <nexus/threading.h>
#include <string.h>
#include <stdio.h>


//
// Per-Queue allocator (protected by the queue's lock)
//
typedef struct nx_allocator_t {
    nx_message_list_t  message_free_list;
    nx_buffer_list_t   buffer_free_list;
} nx_allocator_t;

//
// Global allocator (protected by a global lock)
//
typedef struct {
    nx_message_list_t  message_free_list;
    nx_buffer_list_t   buffer_free_list;
    sys_mutex_t       *lock;
} nx_global_allocator_t;

static nx_global_allocator_t  global;
static nx_allocator_config_t  default_config;
static const nx_allocator_config_t *config;


static nx_allocator_t *nx_get_allocator(void)
{
    static __thread nx_allocator_t *alloc = 0;

    if (!alloc) {
        alloc = NEW(nx_allocator_t);
        printf("Created thread-local allocator\n");

        if (!alloc)
            return 0;

        DEQ_INIT(alloc->message_free_list);
        DEQ_INIT(alloc->buffer_free_list);
    }

    return alloc;
}


const nx_allocator_config_t *nx_allocator_default_config(void)
{
    default_config.buffer_size                     = 1024;
    default_config.buffer_preallocation_count      = 512;
    default_config.buffer_rebalancing_batch_count  = 16;
    default_config.buffer_local_storage_max        = 64;
    default_config.buffer_free_list_max            = 1000000;
    default_config.message_allocation_batch_count  = 256;
    default_config.message_rebalancing_batch_count = 64;
    default_config.message_local_storage_max       = 256;

    return &default_config;
}


void nx_allocator_initialize(const nx_allocator_config_t *c)
{
    config = c;

    // Initialize the fields in the global structure.
    DEQ_INIT(global.message_free_list);
    DEQ_INIT(global.buffer_free_list);
    global.lock = sys_mutex();

    // Pre-allocate buffers according to the configuration
    int          i;
    nx_buffer_t *buf;

    printf("[NX_MESSAGE - Pre-allocating %ld buffers]\n", config->buffer_preallocation_count);
    for (i = 0; i < config->buffer_preallocation_count; i++) {
        buf = (nx_buffer_t*) malloc (sizeof(nx_buffer_t) + config->buffer_size);
        DEQ_INSERT_TAIL(global.buffer_free_list, buf);
    }
}


void nx_allocator_finalize(void)
{
    // TODO - Free buffers and messages
}


nx_message_t *nx_allocate_message(void)
{
    nx_allocator_t *alloc = nx_get_allocator();
    nx_message_t   *msg;
    int             i;

    if (DEQ_SIZE(alloc->message_free_list) == 0) {
        //
        // The local free list is empty, rebalance a batch of objects from the global
        // free list.
        //
        sys_mutex_lock(global.lock);
        if (DEQ_SIZE(global.message_free_list) >= config->message_rebalancing_batch_count) {
            for (i = 0; i < config->message_rebalancing_batch_count; i++) {
                msg = DEQ_HEAD(global.message_free_list);
                DEQ_REMOVE_HEAD(global.message_free_list);
                DEQ_INSERT_TAIL(alloc->message_free_list, msg);
            }
        }
        sys_mutex_unlock(global.lock);
    }

    if (DEQ_SIZE(alloc->message_free_list) == 0) {
        //
        // The local free list is still empty.  This means there were not enough objects on the
        // global free list to make up a batch.  Allocate new objects from the heap and store
        // them in the local free list.
        //
        nx_message_t *batch = NEW_ARRAY(nx_message_t, config->message_allocation_batch_count);
        memset(batch, 0, sizeof(nx_message_t) * config->message_allocation_batch_count);
        for (i = 0; i < config->message_allocation_batch_count; i++) {
            DEQ_INSERT_TAIL(alloc->message_free_list, &batch[i]);
        }
    }

    //
    // If the local free list is still empty, we're out of memory.
    //
    if (DEQ_SIZE(alloc->message_free_list) == 0)
        return 0;

    msg = DEQ_HEAD(alloc->message_free_list);
    DEQ_REMOVE_HEAD(alloc->message_free_list);

    DEQ_INIT(msg->buffers);
    msg->in_delivery = NULL;
    msg->out_delivery = NULL;
    return msg;
}


nx_buffer_t *nx_allocate_buffer(void)
{
    nx_allocator_t *alloc = nx_get_allocator();
    nx_buffer_t    *buf;
    int             i;

    if (DEQ_SIZE(alloc->buffer_free_list) == 0) {
        sys_mutex_lock(global.lock);
        if (DEQ_SIZE(global.buffer_free_list) >= config->buffer_rebalancing_batch_count) {
            // Rebalance a batch of free descriptors to the local free list.
            for (i = 0; i < config->buffer_rebalancing_batch_count; i++) {
                buf = DEQ_HEAD(global.buffer_free_list);
                DEQ_REMOVE_HEAD(global.buffer_free_list);
                DEQ_INSERT_TAIL(alloc->buffer_free_list, buf);
            }
        }
        sys_mutex_unlock(global.lock);
    }

    if (DEQ_SIZE(alloc->buffer_free_list) == 0) {
        // Allocate a buffer from the heap
        buf = (nx_buffer_t*) malloc (sizeof(nx_buffer_t) + config->buffer_size);
        DEQ_INSERT_TAIL(alloc->buffer_free_list, buf);
    }

    if (DEQ_SIZE(alloc->buffer_free_list) == 0)
        return 0;

    buf = DEQ_HEAD(alloc->buffer_free_list);
    DEQ_REMOVE_HEAD(alloc->buffer_free_list);

    buf->size = 0;

    return buf;
}


void nx_free_message(nx_message_t *msg)
{
    nx_allocator_t *alloc = nx_get_allocator();

    // Free any buffers in the message
    int          i;
    nx_buffer_t *buf = DEQ_HEAD(msg->buffers);
    while (buf) {
        DEQ_REMOVE_HEAD(msg->buffers);
        nx_free_buffer(buf);
        buf = DEQ_HEAD(msg->buffers);
    }

    DEQ_INSERT_TAIL(alloc->message_free_list, msg);
    if (DEQ_SIZE(alloc->message_free_list) > config->message_local_storage_max) {
        //
        // The local free list has exceeded the threshold for local storage.
        // Rebalance a batch of free objects to the global free list.
        //
        sys_mutex_lock(global.lock);
        for (i = 0; i < config->message_rebalancing_batch_count; i++) {
            msg = DEQ_HEAD(alloc->message_free_list);
            DEQ_REMOVE_HEAD(alloc->message_free_list);
            DEQ_INSERT_TAIL(global.message_free_list, msg);
        }
        sys_mutex_unlock(global.lock);
    }
}


void nx_free_buffer(nx_buffer_t *buf)
{
    nx_allocator_t *alloc = nx_get_allocator();
    int             i;

    DEQ_INSERT_TAIL(alloc->buffer_free_list, buf);
    if (DEQ_SIZE(alloc->buffer_free_list) > config->buffer_local_storage_max) {
        // Rebalance a batch of free descriptors to the global free list.
        sys_mutex_lock(global.lock);
        for (i = 0; i < config->buffer_rebalancing_batch_count; i++) {
            buf = DEQ_HEAD(alloc->buffer_free_list);
            DEQ_REMOVE_HEAD(alloc->buffer_free_list);
            DEQ_INSERT_TAIL(global.buffer_free_list, buf);
        }
        sys_mutex_unlock(global.lock);
    }
}


void nx_message_add_buffer(nx_message_t *msg, nx_buffer_t *buf)
{
    DEQ_INSERT_TAIL(msg->buffers, buf);
}


nx_message_t *nx_message_receive(pn_delivery_t *delivery)
{
    pn_link_t    *link = pn_delivery_link(delivery);
    nx_message_t *msg  = (nx_message_t*) pn_delivery_get_context(delivery);
    ssize_t       rc;
    nx_buffer_t  *buf;

    //
    // If there is no message associated with the delivery, this is the first time
    // we've received anything on this delivery.  Allocate a message descriptor and 
    // link it and the delivery together.
    //
    if (!msg) {
        msg = nx_allocate_message();
        pn_delivery_set_context(delivery, (void*) msg);
        msg->in_delivery = delivery;
    }

    //
    // Get a reference to the tail buffer on the message.  This is the buffer into which
    // we will store incoming message data.  If there is no buffer in the message, allocate
    // an empty one and add it to the message.
    //
    buf = DEQ_TAIL(msg->buffers);
    if (!buf) {
        buf = nx_allocate_buffer();
        nx_message_add_buffer(msg, buf);
    }

    while (1) {
        //
        // Try to receive enough data to fill the remaining space in the tail buffer.
        //
        rc = pn_link_recv(link, nx_buffer_cursor(buf), nx_buffer_capacity(buf));

        //
        // If we receive PN_EOS, we have come to the end of the message.
        //
        if (rc == PN_EOS) {
            //
            // If the last buffer in the list is empty, remove it and free it.  This
            // will only happen if the size of the message content is an exact multiple
            // of the buffer size.
            //
            if (nx_buffer_size(buf) == 0) {
                DEQ_REMOVE_TAIL(msg->buffers);
                nx_free_buffer(buf);
            }
            return msg;
        }

        if (rc > 0) {
            //
            // We have received a positive number of bytes for the message.  Advance
            // the cursor in the buffer.
            //
            nx_buffer_insert(buf, rc);

            //
            // If the buffer is full, allocate a new empty buffer and append it to the
            // tail of the message's list.
            //
            if (nx_buffer_capacity(buf) == 0) {
                buf = nx_allocate_buffer();
                nx_message_add_buffer(msg, buf);
            }
        } else
            //
            // We received zero bytes, and no PN_EOS.  This means that we've received
            // all of the data available up to this point, but it does not constitute
            // the entire message.  We'll be back later to finish it up.
            //
            break;
    }

    return NULL;
}


char *nx_buffer_base(nx_buffer_t *buf)
{
    return (char*) &buf[1];
}


char *nx_buffer_cursor(nx_buffer_t *buf)
{
    return ((char*) &buf[1]) + buf->size;
}


size_t nx_buffer_capacity(nx_buffer_t *buf)
{
    return config->buffer_size - buf->size;
}


size_t nx_buffer_size(nx_buffer_t *buf)
{
    return buf->size;
}


void nx_buffer_insert(nx_buffer_t *buf, size_t len)
{
    buf->size += len;
    assert(buf->size <= config->buffer_size);
}

