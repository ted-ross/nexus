##
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
## 
##   http://www.apache.org/licenses/LICENSE-2.0
## 
## Unless required by applicable law or agreed to in writing,
## software distributed under the License is distributed on an
## "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
## KIND, either express or implied.  See the License for the
## specific language governing permissions and limitations
## under the License.
##

cmake_minimum_required(VERSION 2.6)
set(CMAKE_C_FLAGS "-pthread -Wall -Werror")

project(pc)
find_library(proton_lib qpid-proton)
find_library(pthread_lib pthread)
find_path(proton_include proton/driver.h)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${proton_include}
    )

##
## Build the Multi-Threaded Server Library
##
set(server_SOURCES
    src/posix/threading.c
    src/context.c
    src/server.c
    src/auth.c
    src/work_queue.c
    src/timer.c
    )

add_library(nexus-server SHARED ${server_SOURCES})
target_link_libraries(nexus-server ${proton_lib} ${pthread_lib})

##
## Build the Container Library
##
set(container_SOURCES
    src/container.c
    src/link_allocator.c
    src/message.c
    src/hash.c
    src/agent.c
    src/basic_queue.c
    src/router.c
    )

add_library(nexus-container SHARED ${container_SOURCES})
target_link_libraries(nexus-container nexus-server ${proton_lib})

##
## Build the server application
##
set(app_SOURCES
    applications/server.c
    )

add_executable(nexus ${app_SOURCES})
target_link_libraries(nexus nexus-container)

##
## Build test applications
##
set(timer_test_SOURCES
    tests/timer_test.c
    src/timer.c
    src/posix/threading.c
    )

add_executable(timer_test ${timer_test_SOURCES})
target_link_libraries(timer_test ${pthread_lib})

