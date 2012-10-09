#ifndef __nexus_router_control_h__
#define __nexus_router_control_h__ 1
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

void nxr_control_initialize(void);
void nxr_control_finalize(void);

typedef struct nxr_domain_t nxr_domain_t;

typedef struct {
    int hello_interval_seconds;
    int hello_max_age_seconds;
    int ra_interval_seconds;
    int remote_ls_max_age_seconds;
    int mobile_addr_max_age_seconds;
} nxr_domain_configuration_t;

typedef enum {
    NXR_EVENT_NONE,
    NXR_EVENT_OFFER,
    NXR_EVENT_ROUTE_ADD,
    NXR_EVENT_ROUTE_DELETE
} nxr_event_t;

nxr_domain_t *nxr_domain(const char *name, const nxr_domain_configuration_t *config);
void nxr_domain_free(nxr_domain_t *domain);

const nxr_domain_configuration_t *nxr_domain_config_default(void);
void nxr_domain_receive(nxr_domain_t *domain, nx_message_t *msg);
nx_message_t *nxr_domain_message(nxr_domain_t *domain);

nxr_event_t nxr_domain_event(nxr_domain_t *domain);
void nxr_domain_event_advance(nxr_domain_t *domain);
int nxr_domain_event_offered(nxr_domain_t *domain);
const char *nxr_domain_event_address(nxr_domain_t *domain);
const char *nxr_domain_event_nexthop(nxr_domain_t *domain);

#endif

