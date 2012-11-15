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

project(nexus C)
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
    src/alloc.c
    src/auth.c
    src/basic_queue.c
    src/container.c
    src/hash.c
    src/iterator.c
    src/link_allocator.c
    src/message.c
    src/posix/threading.c
    src/server.c
    src/timer.c
    src/work_queue.c
    )

add_library(nexus-server SHARED ${server_SOURCES})
target_link_libraries(nexus-server ${proton_lib} ${pthread_lib})

##
## Build the Nodes Library
##
set(nodes_SOURCES
    src/router/router_control.c
    src/router/router_node.c
    )

add_library(nexus-nodes SHARED ${nodes_SOURCES})
target_link_libraries(nexus-nodes nexus-server)

##
## Build the server application
##
set(router_SOURCES
    applications/nexus-router.c
    )

add_executable(nexus-router ${router_SOURCES})
target_link_libraries(nexus-router nexus-nodes)

##
## Build Tests
##
add_subdirectory(tests)
