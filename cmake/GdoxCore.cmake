add_library(
    gdox_core
    STATIC
        ${GDOX_LIVE_DISC_SOURCES}
        ${GDOX_HDD_CACHE_SOURCES}
        ${GDOX_DESKTOP_CORE_SOURCES}
        src/platform/portable_thread.c
)
if(WIN32)
    target_sources(
        gdox_core
        PRIVATE
            src/platform/emulator_windows.c
            src/platform/file_source_windows.c
            src/platform/hash_bcrypt.c
            src/platform/nbd_tcp.c
            src/platform/preservation_io_windows.c
            src/platform/random_access_file_windows.c
            src/platform/windows_support.c
    )
    target_link_libraries(gdox_core PRIVATE bcrypt ws2_32)
elseif(APPLE)
    target_sources(
        gdox_core
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
        gdox_core
        PRIVATE
            src/platform/emulator_posix.c
            src/platform/file_source_posix.c
            src/platform/hash_openssl.c
            src/platform/nbd_tcp.c
            src/platform/preservation_io_posix.c
            src/platform/random_access_file_posix.c
    )
    target_link_libraries(gdox_core PRIVATE OpenSSL::Crypto)
endif()
add_library(gdox::core ALIAS gdox_core)
target_include_directories(
    gdox_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(NOT WIN32)
    target_link_libraries(gdox_core PRIVATE Threads::Threads)
endif()
target_compile_definitions(
    gdox_core
    PUBLIC GDOX_VERSION="${PROJECT_VERSION}"
)
gdox_enable_c_warnings(gdox_core)

add_library(
    gdox_application_support
    OBJECT
        src/app/optical_monitor.c
        src/app/preferences.c
        src/app/preservation_naming.c
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
