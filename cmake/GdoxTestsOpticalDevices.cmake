if(GDOX_BUILD_OPTICAL)
    add_executable(gdox_runtime_setup_tests
        tests/test_runtime_setup.c
        src/app/runtime_setup.c
        src/app/runtime_state.c
        src/app/runtime_commands.c
        src/core/error.c
    )
    target_include_directories(gdox_runtime_setup_tests PRIVATE include src)
    if(NOT WIN32)
        target_link_libraries(gdox_runtime_setup_tests PRIVATE Threads::Threads)
    endif()
    gdox_enable_c_warnings(gdox_runtime_setup_tests)
    gdox_enable_test_crt(gdox_runtime_setup_tests)
    add_test(NAME runtime.setup COMMAND gdox_runtime_setup_tests)
    set_tests_properties(runtime.setup PROPERTIES TIMEOUT 10)
    gdox_label_tests(runtime runtime.setup)

    add_executable(gdox_optical_devices_tests
        tests/test_optical_devices.c
        src/platform/optical_devices.c
        src/platform/optical.c
        src/core/source.c
        src/core/error.c
    )
    target_include_directories(gdox_optical_devices_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    gdox_enable_c_warnings(gdox_optical_devices_tests)
    gdox_enable_test_crt(gdox_optical_devices_tests)
    add_test(NAME optical.devices COMMAND gdox_optical_devices_tests)
    gdox_label_tests(optical optical.devices)

    add_executable(gdox_optical_eject_gate_tests tests/test_optical_eject_gates.c)
    target_include_directories(gdox_optical_eject_gate_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gdox_optical_eject_gate_tests PRIVATE gdox::optical)
    gdox_enable_c_warnings(gdox_optical_eject_gate_tests)
    add_test(NAME optical.eject_gates COMMAND gdox_optical_eject_gate_tests)
    gdox_label_tests(optical optical.eject_gates)
endif()
