add_executable(
    gdox_test_xemu_helper
    tests/test_xemu_helper_capabilities.c
    tests/test_xemu_helper_gameplay.c
    tests/test_xemu_helper_main.c
    tests/test_xemu_helper_save.c
)
target_include_directories(
    gdox_test_xemu_helper
    PRIVATE
        tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_test_xemu_helper)
gdox_enable_test_crt(gdox_test_xemu_helper)

add_executable(
    gdox_tests
    tests/test_background_lifecycle.c
    tests/test_default_xbe_cache_source.c
    tests/test_disc.c
    tests/test_emulator.c
    tests/test_file_readahead_source.c
    tests/test_gamepad_input_policy.c
    tests/test_hash.c
    tests/test_main.c
    tests/test_nbd.c
    tests/test_nbd_wire.c
    tests/test_optical_monitor.c
    tests/test_playback_labels.c
    tests/test_preferences.c
    tests/test_process_support.c
    tests/test_preservation_naming.c
    tests/test_preserve.c
    tests/test_runtime_bundle.c
    tests/test_runtime_commands.c
    tests/test_scsi_transport.c
    tests/test_security.c
    tests/test_session_storage.c
    tests/test_source.c
    tests/test_x360.c
    tests/test_xbe_patch_source.c
    tests/test_xdvdfs.c
    tests/test_xdvdfs_directory_cache.c
    tests/test_xemu_capabilities.c
    tests/test_xemu_performance.c
    tests/test_xemu_save_storage.c
    tests/test_xenia_patches.c
    tests/test_xenia_policy.c
    tests/test_xenia_storage.c
    src/app/runtime_bundle.c
    src/ui/gamepad_input_policy.c
    src/ui/playback_labels.c
    src/app/background_lifecycle.c
    src/platform/scsi_transport.c
)
add_dependencies(gdox_tests gdox_test_xemu_helper)
target_link_libraries(gdox_tests PRIVATE gdox::services)
target_sources(
    gdox_tests
    PRIVATE $<TARGET_OBJECTS:gdox_application_support>
)
if(WIN32)
    target_sources(
        gdox_tests
        PRIVATE src/platform/background_host_windows.c
    )
    target_link_libraries(gdox_tests PRIVATE shell32)
endif()
target_include_directories(
    gdox_tests
    PRIVATE
        tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(
    gdox_tests
    PRIVATE GDOX_RUNTIME_BUNDLE_TESTING=1
)
if(MSVC)
    target_compile_definitions(
        gdox_tests
        PRIVATE _CRT_SECURE_NO_WARNINGS
    )
endif()
gdox_enable_c_warnings(gdox_tests)

set(
    GDOX_TEST_GROUPS
    background_lifecycle
    default_xbe_cache_source
    disc
    emulator
    file_readahead_source
    hash
    nbd
    nbd_wire
    optical_monitor
    preferences
    preservation_naming
    preserve
    runtime_bundle
    runtime_commands
    scsi_transport
    security
    session_storage
    source
    x360
    xbe_patch_source
    xdvdfs
    xdvdfs_directory_cache
    xemu_capabilities
    xemu_performance
    xemu_save_storage
    xenia_patches
    xenia_policy
    xenia_storage
)
foreach(group IN LISTS GDOX_TEST_GROUPS)
    add_test(NAME core.${group} COMMAND gdox_tests ${group})
    gdox_label_tests(core core.${group})
endforeach()

add_test(
    NAME ui.gamepad_input_policy
    COMMAND gdox_tests gamepad_input_policy
)
add_test(
    NAME ui.playback_labels
    COMMAND gdox_tests playback_labels
)
gdox_label_tests(
    ui
    ui.gamepad_input_policy
    ui.playback_labels
)

add_executable(gdox_media_image_tests tests/test_media_image.c)
target_link_libraries(gdox_media_image_tests PRIVATE gdox::services)
gdox_enable_c_warnings(gdox_media_image_tests)
gdox_enable_test_crt(gdox_media_image_tests)
add_test(NAME core.media_image COMMAND gdox_media_image_tests)
gdox_label_tests(core core.media_image)
