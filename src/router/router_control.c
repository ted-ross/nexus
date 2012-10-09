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

#include "router_control.h"
#include <nexus/ctools.h>
#include <nexus/threading.h>


struct nxr_domain_t {
    nx_message_list_t  incoming_messages;
    nx_message_list_t  outgoing_messages;
};


static nxr_domain_configuration_t  default_config;
static sys_mutex_t                *lock;


void nxr_control_initialize(void)
{
    //
    // Setup the default configuration.
    //
    default_config.hello_interval_seconds      = 1;
    default_config.hello_max_age_seconds       = 3;
    default_config.ra_interval_seconds         = 30;
    default_config.remote_ls_max_age_seconds   = 60;
    default_config.mobile_addr_max_age_seconds = 60;

    lock = sys_mutex();
}


void nxr_control_finalize(void)
{
    sys_mutex_free(lock);
}


nxr_domain_t *nxr_domain(const char *name, const nxr_domain_configuration_t *config)
{
    nxr_domain_t *domain = NEW(nxr_domain_t);
    if (!domain)
        return 0;

    DEQ_INIT(domain->incoming_messages);
    DEQ_INIT(domain->outgoing_messages);

    return domain;
}


void nxr_domain_free(nxr_domain_t *domain)
{
    free(domain);
}


const nxr_domain_configuration_t *nxr_domain_config_default(void)
{
    return &default_config;
}


void nxr_domain_receive(nxr_domain_t *domain, nx_message_t *msg)
{
    sys_mutex_lock(lock);
    DEQ_INSERT_TAIL(domain->incoming_messages, msg);
    // TODO - Signal Control Thread
    sys_mutex_unlock(lock);
}


nx_message_t *nxr_domain_message(nxr_domain_t *domain)
{
    sys_mutex_lock(lock);
    nx_message_t *msg = DEQ_HEAD(domain->outgoing_messages);
    if (msg)
        DEQ_REMOVE_HEAD(domain->outgoing_messages);
    sys_mutex_unlock(lock);
    return msg;
}


nxr_event_t nxr_domain_event(nxr_domain_t *domain)
{
    return NXR_EVENT_NONE;
}


void nxr_domain_event_advance(nxr_domain_t *domain)
{
}


int nxr_domain_event_offered(nxr_domain_t *domain)
{
    sys_mutex_lock(lock);
    int offered = DEQ_SIZE(domain->outgoing_messages);
    sys_mutex_unlock(lock);
    return offered;
}


const char *nxr_domain_event_address(nxr_domain_t *domain)
{
    return 0;
}


const char *nxr_domain_event_nexthop(nxr_domain_t *domain)
{
    return 0;
}

