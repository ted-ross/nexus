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
#include <nexus/timer.h>
#include "timer_private.h"
#include "test_case.h"
#include <nexus/threading.h>


static unsigned long    fire_mask;
static nx_timer_list_t  pending_timers;
static sys_mutex_t     *lock;
static long             time;
static nx_timer_t      *timers[16];


void nx_server_timer_pending_LH(nx_timer_t *timer)
{
    DEQ_INSERT_TAIL(pending_timers, timer);
}


void nx_server_timer_cancel_LH(nx_timer_t *timer)
{
    if (timer->state == TIMER_PENDING)
        DEQ_REMOVE(pending_timers, timer);
}


static int fire_head()
{
    sys_mutex_lock(lock);
    int         result = DEQ_SIZE(pending_timers);
    nx_timer_t *timer  = DEQ_HEAD(pending_timers);
    if (timer) {
        DEQ_REMOVE_HEAD(pending_timers);
        nx_timer_idle_LH(timer);
        fire_mask |= (unsigned long) timer->context;
    }
    sys_mutex_unlock(lock);
    return result;
}


static char* test_quiet(void *context)
{
    fire_mask = 0;

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);

    while(fire_head());

    if (fire_mask != 0)
        return "Expected zero timers fired";
    return 0;
}

static char* test_immediate(void *context)
{
    while(fire_head());
    fire_mask = 0;

    nx_timer_schedule(timers[0], 0);

    if (fire_mask != 0)  return "Premature firing";
    if (fire_head() > 1) return "Too many firings";
    if (fire_mask != 1)  return "Incorrect fire mask";

    return 0;
}


static char* test_single(void *context)
{
    while(fire_head());
    fire_mask = 0;

    nx_timer_schedule(timers[0], 2);
    if (fire_head() > 0) return "Premature firing 1";

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    if (fire_head() > 0) return "Premature firing 2";

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    if (fire_head() < 1) return "Failed to fire";

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    if (fire_head() != 0) return "Spurious fires";

    if (fire_mask != 1)  return "Incorrect fire mask";
    if (timers[0]->state != TIMER_IDLE) return "Expected idle timer state";

    return 0;
}


static char* test_two_inorder(void *context)
{
    while(fire_head());
    fire_mask = 0;

    nx_timer_schedule(timers[0], 2);
    nx_timer_schedule(timers[1], 4);

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    int count = fire_head();
    if (count < 1) return "First failed to fire";
    if (count > 1) return "Second fired prematurely";
    if (fire_mask != 1) return "Incorrect fire mask 1";

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    if (fire_head() < 1) return "Second failed to fire";
    if (fire_mask != 3)  return "Incorrect fire mask 3";

    return 0;
}


static char* test_two_reverse(void *context)
{
    while(fire_head());
    fire_mask = 0;

    nx_timer_schedule(timers[0], 4);
    nx_timer_schedule(timers[1], 2);

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    int count = fire_head();
    if (count < 1) return "First failed to fire";
    if (count > 1) return "Second fired prematurely";
    if (fire_mask != 2) return "Incorrect fire mask 2";

    sys_mutex_lock(lock);
    nx_timer_visit_LH(time++);
    nx_timer_visit_LH(time++);
    sys_mutex_unlock(lock);
    if (fire_head() < 1) return "Second failed to fire";
    if (fire_mask != 3)  return "Incorrect fire mask 3";

    return 0;
}


static char* test_big(void *context)
{
    while(fire_head());
    fire_mask = 0;

    long durations[16] =
        { 5,  8,  7,  6,
         14, 10, 16, 15,
         11, 12,  9, 12,
          1,  2,  3,  4};
    unsigned long masks[18] = {
    0x1000,
    0x3000,
    0x7000,
    0xf000,
    0xf001,
    0xf009,
    0xf00d,
    0xf00f,
    0xf40f,
    0xf42f,
    0xf52f,
    0xff2f,
    0xff2f,
    0xff3f,
    0xffbf,
    0xffff,
    0xffff,
    0xffff
    };

    int i;
    for (i = 0; i < 16; i++)
        nx_timer_schedule(timers[i], durations[i]);
    for (i = 0; i < 18; i++) {
        sys_mutex_lock(lock);
        nx_timer_visit_LH(time++);
        sys_mutex_unlock(lock);
        while(fire_head());
        if (fire_mask != masks[i]) {
            static char error[100];
            sprintf(error, "Iteration %d: expected mask %04lx, got %04lx", i, masks[i], fire_mask);
            return error;
        }
    }

    return 0;
}


int main(int argc, char **argv)
{
    fire_mask = 0;
    DEQ_INIT(pending_timers);
    lock = sys_mutex();
    nx_timer_initialize(lock);
    time = 1;

    timers[0]  = nx_timer(0, (void*) 0x00000001);
    timers[1]  = nx_timer(0, (void*) 0x00000002);
    timers[2]  = nx_timer(0, (void*) 0x00000004);
    timers[3]  = nx_timer(0, (void*) 0x00000008);
    timers[4]  = nx_timer(0, (void*) 0x00000010);
    timers[5]  = nx_timer(0, (void*) 0x00000020);
    timers[6]  = nx_timer(0, (void*) 0x00000040);
    timers[7]  = nx_timer(0, (void*) 0x00000080);
    timers[8]  = nx_timer(0, (void*) 0x00000100);
    timers[9]  = nx_timer(0, (void*) 0x00000200);
    timers[10] = nx_timer(0, (void*) 0x00000400);
    timers[11] = nx_timer(0, (void*) 0x00000800);
    timers[12] = nx_timer(0, (void*) 0x00001000);
    timers[13] = nx_timer(0, (void*) 0x00002000);
    timers[14] = nx_timer(0, (void*) 0x00004000);
    timers[15] = nx_timer(0, (void*) 0x00008000);

    TEST_CASE(test_quiet, 0);
    TEST_CASE(test_immediate, 0);
    TEST_CASE(test_single, 0);
    TEST_CASE(test_two_inorder, 0);
    TEST_CASE(test_two_reverse, 0);
    TEST_CASE(test_big, 0);

    int i;
    for (i = 0; i < 16; i++)
        nx_timer_free(timers[i]);

    nx_timer_finalize();

    return 0;
}

