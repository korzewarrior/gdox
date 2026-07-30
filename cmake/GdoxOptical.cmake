find_package(PkgConfig QUIET)

add_library(
    gdox_optical
    STATIC
        src/platform/gp08_source.c
        src/platform/mt1887_source.c
        src/platform/mt1887_profile.c
        src/platform/optical.c
        src/platform/scsi_transport.c
        src/platform/usb_bot_identity.c
)
add_library(gdox::optical ALIAS gdox_optical)
target_include_directories(gdox_optical PRIVATE src)
target_link_libraries(gdox_optical PUBLIC gdox::core)
if(NOT WIN32)
    target_link_libraries(gdox_optical PRIVATE Threads::Threads)
endif()

if(APPLE)
    target_sources(
        gdox_optical
        PRIVATE
            src/platform/macos_scsi.c
            src/platform/usb_bot_macos.c
    )
    set_source_files_properties(
        src/platform/macos_scsi.c
        PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations"
    )
    target_link_libraries(
        gdox_optical
        PRIVATE
            "-framework CoreFoundation"
            "-framework DiskArbitration"
            "-framework IOKit"
    )
    target_compile_definitions(gdox_optical PUBLIC GDOX_HAS_LIBUSB=0)
elseif(WIN32)
    target_sources(gdox_optical PRIVATE src/platform/usb_bot_windows.c)
    target_link_libraries(gdox_optical PRIVATE cfgmgr32 setupapi)
    target_compile_definitions(gdox_optical PUBLIC GDOX_HAS_LIBUSB=0)
elseif(PkgConfig_FOUND)
    pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
endif()

if(NOT APPLE AND NOT WIN32 AND TARGET PkgConfig::LIBUSB)
    target_sources(gdox_optical PRIVATE src/platform/usb_bot_libusb.c)
    target_link_libraries(gdox_optical PRIVATE PkgConfig::LIBUSB)
    target_compile_definitions(gdox_optical PUBLIC GDOX_HAS_LIBUSB=1)
elseif(NOT APPLE AND NOT WIN32)
    if(GDOX_REQUIRE_LIBUSB)
        message(
            FATAL_ERROR
            "A release-capable Linux build requires libusb-1.0 development files"
        )
    endif()
    target_sources(gdox_optical PRIVATE src/platform/usb_bot_stub.c)
    target_compile_definitions(gdox_optical PUBLIC GDOX_HAS_LIBUSB=0)
endif()
gdox_enable_c_warnings(gdox_optical)
