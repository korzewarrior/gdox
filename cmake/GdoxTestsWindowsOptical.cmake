if(WIN32 AND GDOX_BUILD_OPTICAL)
    add_executable(
        gdox_usb_bot_windows_tests
        tests/test_usb_bot_windows.c
        src/platform/scsi_transport.c
        src/platform/usb_bot_identity.c
        src/core/error.c
    )
    target_include_directories(
        gdox_usb_bot_windows_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    gdox_enable_c_warnings(gdox_usb_bot_windows_tests)
    gdox_enable_test_crt(gdox_usb_bot_windows_tests)
    add_test(NAME optical.windows_devices COMMAND gdox_usb_bot_windows_tests)
    gdox_label_tests(optical optical.windows_devices)
endif()
