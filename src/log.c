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

#include <nexus/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char *cls_prefix(int cls)
{
    switch (cls) {
    case LOG_TRACE : return "TRACE";
    case LOG_ERROR : return "ERROR";
    case LOG_INFO  : return "INFO";
    }

    return "";
}

void nx_log(char *module, int cls, char *fmt, ...)
{
    va_list ap;
    char    line[128];

    va_start(ap, fmt);
    vsnprintf(line, 127, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s (%s): %s\n", module, cls_prefix(cls), line);
}


