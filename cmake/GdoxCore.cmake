add_library(
    gdox_media_core
    STATIC
        ${GDOX_LIVE_DISC_SOURCES}
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
gdox_enable_c_warnings(gdox_emulator_configuration)

add_library(
    gdox_platform_services
    STATIC
        src/platform/portable_thread.c
)
if(WIN32)
    target_sources(
        gdox_platform_services
        PRIVATE
            src/platform/emulator_windows.c
            src/platform/file_source_windows.c
            src/platform/hash_bcrypt.c
            src/platform/nbd_tcp.c
            src/platform/preservation_io_windows.c
            src/platform/random_access_file_windows.c
            src/platform/windows_support.c
    )
    target_link_libraries(gdox_platform_services PRIVATE bcrypt ws2_32)
elseif(APPLE)
    target_sources(
        gdox_platform_services
        PRIVATE
            src/platform/emulator_posix.c
            src/platform/file_source_posix.c
            src/platform/hash_commoncrypto.c
            src/platform/nbd_tcp.c
            src/platform/preservation_io_posix.c
            src/platform/random_access_file_posix.c
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
            src/platform/file_source_posix.c
            src/platform/hash_openssl.c
            src/platform/nbd_tcp.c
            src/platform/preservation_io_posix.c
            src/platform/random_access_file_posix.c
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
        ${GDOX_HDD_CACHE_SOURCES}
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
        src/app/runtime_bundle.c
)
if(WIN32)
    target_sources(
        gdox_application_support
        PRIVATE src/platform/user_storage_windows.c
    )
else()
    target_sources(
        gdox_application_support
        PRIVATE src/platform/user_storage_posix.c
    )
endif()
target_include_directories(
    gdox_application_support
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_application_support)
