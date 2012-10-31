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
#include <proton/driver.h>
#include <nexus/server.h>
#include <nexus/container.h>
#include <nexus/link_allocator.h>
#include <nexus/router.h>

static void thread_start_handler(void* context, int thread_id)
{
    //printf("[Thread Started - id=%d]\n", thread_id);
}


int main(int argc, char **argv)
{
    nx_link_allocator_initialize();
    container_init();

    nx_server_initialize(4, container_handler, container_close_handler, thread_start_handler, 0);
    nx_router_t *router = nx_router("*", 0);
    nx_server_run();
    nx_router_free(router);
    nx_server_finalize();

    return 0;
}
