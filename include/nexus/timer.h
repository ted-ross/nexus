#ifndef __nexus_timer_h__
#define __nexus_timer_h__ 1
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

#include <nexus/ctools.h>

typedef void (*nx_timer_cb_t)(void* context);
typedef struct nx_timer_t nx_timer_t;
DEQ_DECLARE(nx_timer_t, nx_timer_list_t);

nx_timer_t *nx_timer(nx_timer_cb_t cb, void* context);
void nx_timer_free(nx_timer_t *timer);
void nx_timer_schedule(nx_timer_t *timer, long msec);
void nx_timer_cancel(nx_timer_t *timer);


#endif
