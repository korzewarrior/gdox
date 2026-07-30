cmake_minimum_required(VERSION 3.25)

project(gdox_android_media LANGUAGES C)

if(NOT ANDROID)
    message(FATAL_ERROR "gdox_android_media must be built with the Android NDK")
endif()
if(NOT DEFINED GDOX_LIBUSB_SOURCE_DIR)
    message(
        FATAL_ERROR
        "Set GDOX_LIBUSB_SOURCE_DIR to the pinned libusb source checkout"
    )
endif()
if(NOT EXISTS "${GDOX_LIBUSB_SOURCE_DIR}/android/config.h")
    message(FATAL_ERROR "GDOX_LIBUSB_SOURCE_DIR is not an Android-capable libusb checkout")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(GDOX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${GDOX_ROOT}/cmake")
include(GdoxSources)

add_library(
    gdox_android_libusb
    STATIC
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/core.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/descriptor.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/hotplug.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/io.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/strerror.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/sync.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/os/events_posix.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/os/linux_netlink.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/os/linux_usbfs.c"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/os/threads_posix.c"
)
set_target_properties(
    gdox_android_libusb
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)
target_include_directories(
    gdox_android_libusb
    PUBLIC "${GDOX_LIBUSB_SOURCE_DIR}/libusb"
    PRIVATE
        "${GDOX_LIBUSB_SOURCE_DIR}/android"
        "${GDOX_LIBUSB_SOURCE_DIR}/libusb/os"
)
target_compile_options(
    gdox_android_libusb
    PRIVATE
        -fvisibility=hidden
        -w
)
target_link_libraries(gdox_android_libusb PRIVATE log)

add_library(
    gdox_android_media
    STATIC
        android_disc.c
        ${GDOX_LIVE_DISC_SOURCES}
        ${GDOX_HDD_CACHE_SOURCES}
        "${GDOX_ROOT}/src/platform/mt1887_source.c"
        "${GDOX_ROOT}/src/platform/mt1887_profile.c"
        "${GDOX_ROOT}/src/platform/random_access_file_posix.c"
        "${GDOX_ROOT}/src/platform/scsi_transport.c"
        "${GDOX_ROOT}/src/platform/usb_bot_identity.c"
        "${GDOX_ROOT}/src/platform/usb_bot_libusb.c"
)
add_library(gdox::android_media ALIAS gdox_android_media)
set_target_properties(
    gdox_android_media
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)
target_include_directories(
    gdox_android_media
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${GDOX_ROOT}/include"
    PRIVATE "${GDOX_ROOT}/src"
)
target_compile_options(
    gdox_android_media
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wstrict-prototypes
        -Wundef
        -Werror
)
target_link_libraries(gdox_android_media PUBLIC gdox_android_libusb)

add_library(
    gdox_disc_monitor
    SHARED
        android_monitor.c
)
target_compile_options(
    gdox_disc_monitor
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
)
target_link_libraries(
    gdox_disc_monitor
    PRIVATE
        gdox::android_media
        log
)
