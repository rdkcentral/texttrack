# SPDX-License-Identifier: Apache-2.0
#
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2024 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
cmake_minimum_required(VERSION 3.3)
project(TextTrack)

find_package(WPEFramework)

set(NAMESPACE WPEFramework)
set(PLUGIN_NAME TextTrack)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})
set(PLUGIN_IMPLEMENTATION ${MODULE_NAME}Implementation)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined" )

# Plugin configuration parameters; go into TextTrack.json
set(TEXTTRACK_AUTOSTART "false" CACHE STRING "Should the plugin register for autostart (true/false)")
set(TEXTTRACK_STARTUPORDER "51" CACHE STRING "To configure startup order of TextTrack plugin")
set(TEXTTRACK_STANDARD_DISPLAY "" CACHE STRING "Which display to use for standard session, or blank to disable")
set(TEXTTRACK_STANDARD_SOCKET "/run/subttx/pes_data_main" CACHE STRING "Which socket to open for standard session, or blank to disable")
set(TEXTTRACK_CONFIG_FILE_PATH "" CACHE STRING "Which config file to load")
option(TEXTTRACK_WITH_SESSIONS "Compile with support for sessions" OFF)
option(TEXTTRACK_WITH_RDKSHELL "Compile with support for RDKShell" OFF)
option(TEXTTRACK_WITH_CHOWN_DOBBYAPP "Session sockets will be chown'd to dobbyapp" OFF)
option(TEXTTRACK_WITH_CCHAL "Compile with support for closedcaption-hal" OFF)

string(TOLOWER ${NAMESPACE} STORAGE_DIRECTORY)
include(CmakeHelperFunctions)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)
pkg_check_modules(LIBSUBTTXRENDCTRL REQUIRED subttxrend-ctrl)
pkg_check_modules(LIBSUBTTXRENDCOMMON REQUIRED subttxrend-common)
pkg_check_modules(LIBSUBTTXRENDPROTOCOL REQUIRED subttxrend-protocol)
pkg_check_modules(LIBSUBTTXRENDSOCKSRC REQUIRED subttxrend-socksrc)
pkg_check_modules(LIBSUBTTXRENDGFX REQUIRED subttxrend-gfx)

add_library(${MODULE_NAME} SHARED
        TextTrack.cpp
        Module.cpp)

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES)

target_link_libraries(${MODULE_NAME}
    PRIVATE
    ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    ${NAMESPACE}Definitions::${NAMESPACE}Definitions
)

install(TARGETS ${MODULE_NAME}
        DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

add_library(${PLUGIN_IMPLEMENTATION} SHARED
        Module.cpp
        RenderSession.cpp
        TextTrackImplementation.cpp
)
set_target_properties(${PLUGIN_IMPLEMENTATION} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
)
target_include_directories(${PLUGIN_IMPLEMENTATION}
        PRIVATE
        ${LIBSUBTTXRENDCTRL_INCLUDE_DIRS}
        ${LIBSUBTTXRENDCOMMON_INCLUDE_DIRS}
        ${LIBSUBTTXRENDPROTOCOL_INCLUDE_DIRS}
        ${LIBSUBTTXRENDSOCKSRC_INCLUDE_DIRS}
        ${LIBSUBTTXRENDGFX_INCLUDE_DIRS}
)
target_link_libraries(${PLUGIN_IMPLEMENTATION}
        PRIVATE
        ${CMAKE_THREAD_LIBS_INIT}
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${LIBSUBTTXRENDCTRL_LINK_LIBRARIES}
        ${LIBSUBTTXRENDCOMMON_LINK_LIBRARIES}
        ${LIBSUBTTXRENDPROTOCOL_LINK_LIBRARIES}
        ${LIBSUBTTXRENDSOCKSRC_LINK_LIBRARIES}
        ${LIBSUBTTXRENDGFX_LINK_LIBRARIES}
)
target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE -DTEXTTRACK_CONFIG_FILE_PATH="${TEXTTRACK_CONFIG_FILE_PATH}")
if(TEXTTRACK_WITH_SESSIONS)
target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE -DTEXTTRACK_WITH_SESSIONS=1)
endif()
if(TEXTTRACK_WITH_RDKSHELL)
find_package(${NAMESPACE}SecurityUtil REQUIRED)
target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE -DTEXTTRACK_WITH_RDKSHELL=1)
target_link_libraries(${PLUGIN_IMPLEMENTATION}
        PRIVATE
        ${NAMESPACE}SecurityUtil::${NAMESPACE}SecurityUtil
)
endif()
if(TEXTTRACK_WITH_CHOWN_DOBBYAPP)
target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE -DTEXTTRACK_WITH_CHOWN_DOBBYAPP=1)
endif()
if(TEXTTRACK_WITH_CCHAL)
target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE -DTEXTTRACK_WITH_CCHAL=1)
target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE -lrdkCCReader)
endif()
install(TARGETS ${PLUGIN_IMPLEMENTATION}
        DESTINATION lib/${STORAGE_DIRECTORY}/plugins
)

write_config(${PLUGIN_NAME})
