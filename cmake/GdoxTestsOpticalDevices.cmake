if(GDOX_BUILD_OPTICAL)
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
