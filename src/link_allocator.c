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

#include <nexus/link_allocator.h>
#include <nexus/threading.h>

static nx_link_list_t  link_free_list;
static sys_mutex_t    *lock;


void nx_link_allocator_initialize(void)
{
    DEQ_INIT(link_free_list);
    lock = sys_mutex();
}


void nx_link_allocator_finalize(void)
{
}


nx_link_item_t *nx_link_item(pn_link_t *link)
{
    nx_link_item_t *item;
    sys_mutex_lock(lock);

    //
    // If the free list is empty, allocate a batch of objects and add them to the
    // free list.
    //
    if (DEQ_SIZE(link_free_list) == 0) {
        nx_link_item_t *batch = NEW_ARRAY(nx_link_item_t, 32);  // TODO - Get batch size from config
        if (batch) {
            int i;
            for (i = 0; i < 8; i++) {
                batch[i].next = 0;
                batch[i].prev = 0;
                batch[i].link = 0;
                DEQ_INSERT_TAIL(link_free_list, &batch[i]);
            }
        }
    }

    //
    // If the free list is still empty, we're out of memory.
    //
    if (DEQ_SIZE(link_free_list) == 0) {
        sys_mutex_unlock(lock);
        return 0;
    }

    //
    // Remove an object from the head of the free list, initialize it, and return it.
    //
    item = DEQ_HEAD(link_free_list);
    DEQ_REMOVE_HEAD(link_free_list);

    item->link              = link;
    item->node_context      = 0;
    item->container_context = 0;

    sys_mutex_unlock(lock);
    return item;
}


void nx_link_item_free(nx_link_item_t *item)
{
    sys_mutex_lock(lock);
    DEQ_INSERT_TAIL(link_free_list, item);
    sys_mutex_unlock(lock);
}
