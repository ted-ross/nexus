#ifndef __context_pvt_h__
#define __context_pvt_h__ 1
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

#include <nexus/context.h>
#include <proton/driver.h>

typedef enum {
    CONN_STATE_INITIAL = 0,
    CONN_STATE_AUTHENTICATING,
    CONN_STATE_OPERATIONAL,
    CONN_STATE_FAILED
} conn_state_t;

#define CONTEXT_NO_OWNER -1

context_t *context(conn_state_t initial_state, pn_connector_t *cxtr);
void context_free(context_t *context);
void context_set_state(context_t *context, conn_state_t state);
conn_state_t context_get_state(context_t *context);
void context_set_owner_thread(context_t *context, int owner);
int context_get_owner_thread(context_t *context);
void context_set_enqueued(context_t *context, int value);
int context_get_enqueued(context_t *context);
pn_connector_t *context_get_connector(context_t *context);

#endif
