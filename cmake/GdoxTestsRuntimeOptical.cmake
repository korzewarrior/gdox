add_executable(
    gdox_emulator_process_tests
    tests/test_emulator_process.c
)
target_link_libraries(gdox_emulator_process_tests PRIVATE gdox::platform)
target_include_directories(
    gdox_emulator_process_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_emulator_process_tests)
gdox_enable_test_crt(gdox_emulator_process_tests)
add_test(NAME runtime.emulator_process COMMAND gdox_emulator_process_tests)
gdox_label_tests(runtime runtime.emulator_process)

add_executable(
    gdox_xemu_process_stop_tests
    tests/test_xemu_process_stop.c
    src/app/xemu_process_stop.c
    src/core/error.c
)
target_include_directories(
    gdox_xemu_process_stop_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_xemu_process_stop_tests)
add_test(NAME runtime.xemu_process_stop COMMAND gdox_xemu_process_stop_tests)
gdox_label_tests(runtime runtime.xemu_process_stop)

add_executable(
    gdox_xenia_process_stop_tests
    tests/test_xenia_process_stop.c
    src/app/xenia_process_stop.c
    src/app/runtime_xenia.c
    src/core/error.c
)
target_include_directories(
    gdox_xenia_process_stop_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_xenia_process_stop_tests)
if(NOT WIN32)
    target_link_libraries(
        gdox_xenia_process_stop_tests
        PRIVATE Threads::Threads
    )
endif()
add_test(
    NAME runtime.xenia_process_stop
    COMMAND gdox_xenia_process_stop_tests
)
gdox_label_tests(runtime runtime.xenia_process_stop)

if(GDOX_BUILD_OPTICAL)
    add_executable(
        gdox_physical_media_monitor_tests
        tests/test_physical_media_monitor.c
        src/app/physical_media_monitor.c
    )
    target_include_directories(
        gdox_physical_media_monitor_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    gdox_enable_c_warnings(gdox_physical_media_monitor_tests)
    add_test(
        NAME runtime.physical_media_monitor
        COMMAND gdox_physical_media_monitor_tests
    )

    add_executable(
        gdox_runtime_physical_tests
        tests/test_runtime_physical.c
        src/app/runtime_physical.c
        src/app/physical_media_monitor.c
        src/app/optical_monitor.c
        src/core/error.c
    )
    target_include_directories(
        gdox_runtime_physical_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    gdox_enable_c_warnings(gdox_runtime_physical_tests)
    add_test(NAME runtime.physical COMMAND gdox_runtime_physical_tests)

    add_executable(
        gdox_runtime_actions_tests
        tests/test_runtime_actions.c
        src/app/runtime_actions.c
        src/app/optical_monitor.c
        src/app/runtime_commands.c
        src/core/error.c
    )
    target_include_directories(
        gdox_runtime_actions_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    gdox_enable_c_warnings(gdox_runtime_actions_tests)
    add_test(NAME runtime.actions COMMAND gdox_runtime_actions_tests)

    add_executable(
        gdox_runtime_playback_tests
        tests/test_runtime_playback.c
        src/app/runtime_playback.c
        src/app/runtime_state.c
        src/core/error.c
    )
    target_include_directories(
        gdox_runtime_playback_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if(NOT WIN32)
        target_link_libraries(
            gdox_runtime_playback_tests
            PRIVATE Threads::Threads
        )
    endif()
    gdox_enable_c_warnings(gdox_runtime_playback_tests)
    add_test(NAME runtime.playback COMMAND gdox_runtime_playback_tests)

    add_executable(
        gdox_xenia_runtime_tests
        tests/test_xenia_runtime.c
    )
    target_include_directories(
        gdox_xenia_runtime_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_xenia_runtime_tests
        PRIVATE gdox::xenia
    )
    gdox_enable_c_warnings(gdox_xenia_runtime_tests)
    gdox_enable_test_crt(gdox_xenia_runtime_tests)
    add_test(NAME runtime.xenia COMMAND gdox_xenia_runtime_tests)

    gdox_label_tests(
        runtime
        runtime.physical_media_monitor
        runtime.physical
        runtime.actions
        runtime.playback
        runtime.xenia
    )

    add_executable(
        gdox_mt1887_profile_tests
        tests/test_mt1887_profile.c
    )
    target_include_directories(
        gdox_mt1887_profile_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gdox_mt1887_profile_tests PRIVATE gdox::optical)
    gdox_enable_c_warnings(gdox_mt1887_profile_tests)
    add_test(
        NAME optical.mt1887_profile
        COMMAND gdox_mt1887_profile_tests
    )

    add_executable(
        gdox_mt1887_media_profile_tests
        tests/test_mt1887_media_profile.c
    )
    target_include_directories(
        gdox_mt1887_media_profile_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_mt1887_media_profile_tests
        PRIVATE gdox::optical
    )
    gdox_enable_c_warnings(gdox_mt1887_media_profile_tests)
    add_test(
        NAME optical.mt1887_media_profile
        COMMAND gdox_mt1887_media_profile_tests
    )

    add_executable(
        gdox_mt1887_source_tests
        tests/test_mt1887_source.c
    )
    target_include_directories(
        gdox_mt1887_source_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gdox_mt1887_source_tests PRIVATE gdox::optical)
    gdox_enable_c_warnings(gdox_mt1887_source_tests)
    add_test(
        NAME optical.mt1887_source
        COMMAND gdox_mt1887_source_tests
    )

    add_executable(
        gdox_usb_bot_identity_tests
        tests/test_usb_bot_identity.c
    )
    target_include_directories(
        gdox_usb_bot_identity_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_usb_bot_identity_tests
        PRIVATE gdox::optical
    )
    gdox_enable_c_warnings(gdox_usb_bot_identity_tests)
    add_test(
        NAME optical.usb_bot_identity
        COMMAND gdox_usb_bot_identity_tests
    )

    if(NOT APPLE AND NOT WIN32 AND TARGET PkgConfig::LIBUSB)
        add_executable(
            gdox_usb_bot_libusb_handoff_tests
            tests/test_usb_bot_libusb_handoff.c
        )
        target_include_directories(
            gdox_usb_bot_libusb_handoff_tests
            PRIVATE
                tests
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_link_libraries(
            gdox_usb_bot_libusb_handoff_tests
            PRIVATE gdox::optical PkgConfig::LIBUSB
        )
        gdox_enable_c_warnings(gdox_usb_bot_libusb_handoff_tests)
        add_test(
            NAME optical.usb_bot_libusb_handoff
            COMMAND gdox_usb_bot_libusb_handoff_tests
        )
        gdox_label_tests(optical optical.usb_bot_libusb_handoff)
    endif()

    add_executable(gdox_gp08_tests tests/test_gp08_source.c)
    target_include_directories(
        gdox_gp08_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gdox_gp08_tests PRIVATE gdox::optical)
    gdox_enable_c_warnings(gdox_gp08_tests)
    add_test(NAME optical.gp08 COMMAND gdox_gp08_tests)

    add_executable(
        gdox_asus_nr09_tests
        tests/test_asus_nr09_source.c
    )
    target_include_directories(
        gdox_asus_nr09_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_asus_nr09_tests
        PRIVATE gdox::optical
    )
    gdox_enable_c_warnings(gdox_asus_nr09_tests)
    add_test(
        NAME optical.asus_nr09
        COMMAND gdox_asus_nr09_tests
    )

    add_executable(
        gdox_mmc_commands_tests
        tests/test_mmc_commands.c
    )
    target_include_directories(
        gdox_mmc_commands_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_mmc_commands_tests
        PRIVATE gdox::optical
    )
    gdox_enable_c_warnings(gdox_mmc_commands_tests)
    add_test(
        NAME optical.mmc_commands
        COMMAND gdox_mmc_commands_tests
    )

    add_executable(
        gdox_optical_registry_tests
        tests/test_optical_registry.c
    )
    target_include_directories(
        gdox_optical_registry_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_optical_registry_tests
        PRIVATE gdox::optical
    )
    gdox_enable_c_warnings(gdox_optical_registry_tests)
    add_test(
        NAME optical.registry
        COMMAND gdox_optical_registry_tests
    )

    gdox_label_tests(
        optical
        optical.mt1887_profile
        optical.mt1887_media_profile
        optical.mt1887_source
        optical.usb_bot_identity
        optical.gp08
        optical.asus_nr09
        optical.mmc_commands
        optical.registry
    )

    add_executable(
        gdox_runtime_request_tests
        tests/test_runtime_requests.c
    )
    target_include_directories(
        gdox_runtime_request_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_runtime_request_tests
        PRIVATE gdox::runtime
    )
    gdox_enable_c_warnings(gdox_runtime_request_tests)
    add_test(
        NAME runtime.requests
        COMMAND gdox_runtime_request_tests
    )

    add_executable(
        gdox_runtime_shutdown_tests
        tests/test_runtime_shutdown.c
    )
    target_include_directories(
        gdox_runtime_shutdown_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_runtime_shutdown_tests
        PRIVATE gdox::runtime
    )
    gdox_enable_c_warnings(gdox_runtime_shutdown_tests)
    add_test(
        NAME runtime.shutdown
        COMMAND gdox_runtime_shutdown_tests
    )

    add_executable(
        gdox_runtime_media_tests
        tests/test_runtime_media.c
    )
    target_include_directories(
        gdox_runtime_media_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(
        gdox_runtime_media_tests
        PRIVATE gdox::runtime
    )
    gdox_enable_c_warnings(gdox_runtime_media_tests)
    gdox_enable_test_crt(gdox_runtime_media_tests)
    add_test(
        NAME runtime.media
        COMMAND gdox_runtime_media_tests
    )

    gdox_label_tests(
        runtime
        runtime.requests
        runtime.shutdown
        runtime.media
    )
endif()
