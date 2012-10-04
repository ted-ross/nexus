#ifndef __ctools_h__
#define __ctools_h__ 1
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
#include <assert.h>

#define CT_ASSERT(exp) { assert(exp); }

#define NEW(t)             (t*)  malloc(sizeof(t))
#define NEW_ARRAY(t,n)     (t*)  malloc(sizeof(t)*(n))
#define NEW_PTR_ARRAY(t,n) (t**) malloc(sizeof(t*)*(n))

#define DEQ(t) struct { \
    t      *head;       \
    t      *tail;       \
    size_t  size;       \
    }

#define DEQ_LINKS(t) t *prev; t *next

#define DEQ_INIT(d) { d.head = 0; d.tail = 0; d.size = 0; }
#define DEQ_HEAD(d) (d.head)
#define DEQ_TAIL(d) (d.tail)
#define DEQ_SIZE(d) (d.size)

#define DEQ_INSERT_HEAD(d,i)      \
{                                 \
    if (d.head) {                 \
        (i)->next = d.head;       \
        d.head->prev = i;         \
    } else {                      \
        d.tail = i;               \
        (i)->next = 0;            \
        CT_ASSERT(d.size == 0);   \
    }                             \
    (i)->prev = 0;                \
    d.head = i;                   \
    d.size++;                     \
}

#define DEQ_INSERT_TAIL(d,i)      \
{                                 \
    if (d.tail) {                 \
        (i)->prev = d.tail;       \
        d.tail->next = i;         \
    } else {                      \
        d.head = i;               \
        (i)->prev = 0;            \
        CT_ASSERT(d.size == 0);   \
    }                             \
    (i)->next = 0;                \
    d.tail = i;                   \
    d.size++;                     \
}

#define DEQ_REMOVE_HEAD(d)      \
{                               \
    CT_ASSERT(d.head);          \
    if (d.head) {               \
        d.head = d.head->next;  \
        if (d.head == 0) {      \
            d.tail = 0;         \
            CT_ASSERT(d.size == 1); \
        }                       \
        d.size--;               \
    }                           \
}

#define DEQ_REMOVE_TAIL(d)      \
{                               \
    CT_ASSERT(d.tail);          \
    if (d.tail) {               \
        d.tail = d.tail->prev;  \
        if (d.tail == 0)        \
            d.head = 0;         \
        d.size--;               \
    }                           \
}

#define DEQ_INSERT_AFTER(d,i,a) \
{                               \
    if ((a)->next)              \
        (a)->next->prev = (i);  \
    else                        \
        d.tail = (i);           \
    (i)->next = (a)->next;      \
    (i)->prev = (a);            \
    (a)->next = (i);            \
    d.size++;                   \
}

#define DEQ_REMOVE(d,i)                        \
{                                              \
    if ((i)->next)                             \
        (i)->next->prev = (i)->prev;           \
    else                                       \
        d.tail = (i)->prev;                    \
    if ((i)->prev)                             \
        (i)->prev->next = (i)->next;           \
    else                                       \
        d.head = (i)->next;                    \
    d.size--;                                  \
    CT_ASSERT(d.size || (!d.head && !d.tail)); \
}

#endif
