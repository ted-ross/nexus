#ifndef __server_private_h__
#define __server_private_h__ 1
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

#include <nexus/server.h>
#include <nexus/timer.h>
#include <proton/driver.h>

void nx_server_timer_pending_LH(nx_timer_t *timer);
void nx_server_timer_cancel_LH(nx_timer_t *timer);


struct nx_server_listener_t {
    nx_server_config_t *config;
    void               *context;
    pn_listener_t      *pn_listener;
};


struct nx_server_connector_t {
    nx_server_config_t *config;
    void               *context;
    pn_connector_t     *pn_connector;
};


#endif
