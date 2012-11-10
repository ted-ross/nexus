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

#include <stdlib.h>
#include <stdio.h>
#include <nexus/alloc.h>
#include "context_pvt.h"

struct context_t {
    conn_state_t    state;
    int             owner_thread;
    int             enqueued;
    pn_connector_t *connector;
    void           *user_context;
};

ALLOC_DECLARE(context_t);
ALLOC_DEFINE(context_t);

context_t *context(conn_state_t initial, pn_connector_t *cxtr)
{
    context_t *c = new_context_t();
    if (!c)
        return 0;
    c->state          = initial;
    c->owner_thread   = CONTEXT_NO_OWNER;
    c->enqueued       = 0;
    c->connector      = cxtr;
    c->user_context   = 0;
    return c;
}


void context_free(context_t *context)
{
    if (context)
        free_context_t(context);
}


void context_set_state(context_t *context, conn_state_t state)
{
    context->state = state;
}


conn_state_t context_get_state(context_t *context)
{
    return context->state;
}


void context_set_owner_thread(context_t *context, int owner)
{
    context->owner_thread = owner;
}


int context_get_owner_thread(context_t *context)
{
    return context->owner_thread;
}


void context_set_enqueued(context_t *context, int value)
{
    context->enqueued = value;
}


int context_get_enqueued(context_t *context)
{
    return context ? context->enqueued : 0;
}


void context_set_user_context(context_t *context, void* data)
{
    if (context)
        context->user_context = data;
}


void* context_get_user_context(context_t *context)
{
    return context ? context->user_context : 0;
}


pn_connector_t *context_get_connector(context_t *context)
{
    return context ? context->connector : 0;
}

