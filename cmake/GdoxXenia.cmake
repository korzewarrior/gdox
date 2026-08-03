add_library(
    gdox_xenia_runtime
    STATIC
        src/core/xenia_launch.c
)
add_library(gdox::xenia ALIAS gdox_xenia_runtime)

if(WIN32)
    target_sources(
        gdox_xenia_runtime
        PRIVATE
            src/platform/xenia_process_windows.c
            src/platform/xenia_runtime_windows.c
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(
        gdox_xenia_runtime
        PRIVATE
            src/platform/xenia_process_posix.c
            src/platform/xenia_runtime_posix.c
            src/platform/xenia_bridge_linux.c
            src/platform/xenia_bridge_tools_linux.c
    )
else()
    target_sources(
        gdox_xenia_runtime
        PRIVATE src/platform/xenia_unsupported.c
    )
endif()

target_include_directories(
    gdox_xenia_runtime
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    gdox_xenia_runtime
    PUBLIC
        gdox::emulator_configuration
        gdox::core
        gdox::platform
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(gdox_xenia_runtime PRIVATE Threads::Threads)
endif()
gdox_enable_c_warnings(gdox_xenia_runtime)
