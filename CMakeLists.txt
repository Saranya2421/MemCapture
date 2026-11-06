# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2023 Sky UK
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

cmake_minimum_required(VERSION 3.10)

project(MemCapture)

# Add our local cmake directory to search for components
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

# The following disables an annoying warning "<blah> will change in GCC X.XX"
add_compile_options(-Wno-psabi)
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)
find_package(Breakpad QUIET)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(inja CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

add_executable(${PROJECT_NAME}
        main.cpp
        Measurement.cpp
        Procrank.cpp
        GroupManager.cpp
        Process.cpp
        Metadata.cpp

        FileParsers/MemInfo.cpp
        FileParsers/Smaps.cpp

        JsonReportGenerator.cpp

        ProcessMetric.cpp
        MemoryMetric.cpp
        CpuIdleMetric.cpp
)

set_property(SOURCE main.cpp
        APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/templates/template.html")

set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 17
)

target_include_directories(${PROJECT_NAME}
        PRIVATE
        3rdparty
        .
)

target_link_libraries(${PROJECT_NAME}
        Threads::Threads
        nlohmann_json::nlohmann_json
        pantor::inja
        re2::re2
)

if (BREAKPAD_FOUND)
    message(STATUS "Enabling breakpad support")
    add_definitions(-DUSE_BREAKPAD)

    target_link_libraries(${PROJECT_NAME}
            Breakpad::BreakpadWrapper
    )
else ()
    message(STATUS "Breakpad not found")
endif ()

option(ENABLE_CPU_IDLE_METRICS "Enable support for kernel-based AV metrics on supported platforms" OFF)
if (ENABLE_CPU_IDLE_METRICS)
    add_definitions(-DENABLE_CPU_IDLE_METRICS=1)
endif ()

option(BUILD_TESTS "Build security regression tests" OFF)
if (BUILD_TESTS)
	add_subdirectory(tests)
endif ()
