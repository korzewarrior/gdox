add_library(
    gdox_media_core
    STATIC
        ${GDOX_LIVE_DISC_SOURCES}
        ${GDOX_XBOX360_MEDIA_SOURCES}
)
add_library(gdox::media ALIAS gdox_media_core)
target_include_directories(
    gdox_media_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_media_core)

add_library(
    gdox_emulator_configuration
    STATIC
        src/core/emulator_configuration.c
        src/core/xemu_capabilities.c
        src/core/xemu_save_migration.c
        src/core/xenia_policy.c
)
add_library(
    gdox::emulator_configuration
    ALIAS gdox_emulator_configuration
)
target_include_directories(
    gdox_emulator_configuration
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(gdox_emulator_configuration PUBLIC gdox::media)
if(WIN32)
    target_compile_definitions(
        gdox_emulator_configuration
        PUBLIC GDOX_XENIA_CATALOG_WINDOWS=1
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(
        gdox_emulator_configuration
        PUBLIC GDOX_XENIA_CATALOG_LINUX=1
    )
endif()
gdox_enable_c_warnings(gdox_emulator_configuration)

set(
    GDOX_NBD_SOURCES
    src/platform/nbd_protocol.c
    src/platform/nbd_socket.c
    src/platform/nbd_tcp.c
    src/platform/nbd_telemetry.c
    src/platform/nbd_token.c
    src/platform/nbd_wire.c
)

add_library(
    gdox_platform_services
    STATIC
        src/platform/portable_thread.c
        src/platform/session_storage_policy.c
        src/platform/xemu_capability_probe.c
        src/platform/xemu_runtime_session.c
        src/platform/xemu_save_migration.c
)
if(WIN32)
    target_sources(
        gdox_platform_services
        PRIVATE
            src/platform/emulator_windows.c
            src/platform/xemu_environment_windows.c
            src/platform/xemu_helper_process_windows.c
            src/platform/file_source_windows.c
            src/platform/hash_bcrypt.c
            ${GDOX_NBD_SOURCES}
            src/platform/preservation_io_windows.c
            src/platform/session_storage_windows.c
            src/platform/windows_command.c
            src/platform/windows_support.c
    )
    target_link_libraries(
        gdox_platform_services
        PRIVATE advapi32 bcrypt ws2_32
    )
elseif(APPLE)
    target_sources(
        gdox_platform_services
        PRIVATE
            src/platform/emulator_posix.c
            src/platform/xemu_environment_posix.c
            src/platform/xemu_helper_process_posix.c
            src/platform/file_source_posix.c
            src/platform/hash_commoncrypto.c
            ${GDOX_NBD_SOURCES}
            src/platform/preservation_io_posix.c
            src/platform/session_storage_posix.c
    )
    set_source_files_properties(
        src/platform/hash_commoncrypto.c
        PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations"
    )
else()
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
    target_sources(
        gdox_platform_services
        PRIVATE
            src/platform/emulator_posix.c
            src/platform/xemu_environment_posix.c
            src/platform/xemu_helper_process_posix.c
            src/platform/file_source_posix.c
            src/platform/hash_openssl.c
            ${GDOX_NBD_SOURCES}
            src/platform/preservation_io_posix.c
            src/platform/session_storage_posix.c
    )
    target_link_libraries(gdox_platform_services PRIVATE OpenSSL::Crypto)
endif()
add_library(gdox::platform ALIAS gdox_platform_services)
target_include_directories(
    gdox_platform_services
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(NOT WIN32)
    target_link_libraries(gdox_platform_services PRIVATE Threads::Threads)
endif()
target_link_libraries(
    gdox_platform_services
    PUBLIC
        gdox::emulator_configuration
        gdox::media
)
gdox_enable_c_warnings(gdox_platform_services)

add_library(
    gdox_core
    STATIC
        ${GDOX_PRESERVATION_MEDIA_SOURCES}
        ${GDOX_DESKTOP_CORE_SOURCES}
)
add_library(gdox::core ALIAS gdox_core)
target_include_directories(
    gdox_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    gdox_core
    PUBLIC
        gdox::media
)
target_compile_definitions(
    gdox_core
    PUBLIC GDOX_VERSION="${PROJECT_VERSION}"
)
gdox_enable_c_warnings(gdox_core)

add_library(gdox_desktop_services INTERFACE)
add_library(gdox::services ALIAS gdox_desktop_services)
target_link_libraries(
    gdox_desktop_services
    INTERFACE
        gdox::core
        gdox::platform
)

add_library(
    gdox_application_support
    OBJECT
        src/app/optical_monitor.c
        src/app/preferences.c
        src/app/preservation_naming.c
        src/app/runtime_commands.c
        src/app/xemu_save_storage.c
        src/app/xemu_performance.c
        src/app/xenia_content_policy.c
        src/app/xenia_patches.c
        src/app/xenia_storage.c
)
if(WIN32)
    target_sources(
        gdox_application_support
        PRIVATE
            src/platform/user_storage_windows.c
            src/app/xenia_content_migration_windows.c
    )
else()
    target_sources(
        gdox_application_support
        PRIVATE
            src/platform/user_storage_posix.c
            src/app/xenia_content_migration_posix.c
    )
endif()
target_include_directories(
    gdox_application_support
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_application_support)
