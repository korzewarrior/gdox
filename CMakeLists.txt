cmake_minimum_required(VERSION 3.25)

project(
    gdox
    VERSION 0.2.2
    DESCRIPTION "Read, play, and preserve Xbox game discs"
    LANGUAGES C CXX
)

if(APPLE)
    enable_language(OBJC)
endif()

include(CTest)
include(FetchContent)

option(GDOX_BUILD_UI "Build the raylib + Dear ImGui desktop application" ON)
option(GDOX_BUILD_OPTICAL "Build optical-drive services" ON)
option(GDOX_FETCH_RAYLIB "Fetch raylib when no compatible system package exists" ON)
option(GDOX_REQUIRE_LIBUSB "Fail instead of building a Linux optical stub" OFF)
option(GDOX_USE_SYSTEM_RAYLIB "Prefer an installed raylib library" ON)
option(GDOX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(GDOX_BUILD_UI AND NOT GDOX_BUILD_OPTICAL)
    message(FATAL_ERROR "The desktop application requires optical services")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    # Match the ordinary 8 MiB POSIX main-thread reserve. Windows otherwise
    # defaults executables to 1 MiB, which is too small for bounded media and
    # session path validation using extended-length Windows paths.
    add_link_options(/STACK:8388608)
endif()
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(GDOX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(GdoxCompiler)
include(GdoxSources)
include(GdoxCore)
if(GDOX_BUILD_OPTICAL)
    include(GdoxOptical)
    include(GdoxXenia)
    include(GdoxRuntime)
endif()
if(BUILD_TESTING)
    include(GdoxTests)
endif()
if(GDOX_BUILD_UI)
    include(GdoxDesktop)
endif()
