if(UNIX)
    add_executable(
        gdox_termination_signal_tests
        tests/test_termination_signal.c
        src/platform/termination_signal_posix.c
        src/core/error.c
    )
    target_include_directories(
        gdox_termination_signal_tests
        PRIVATE
            tests
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    gdox_enable_c_warnings(gdox_termination_signal_tests)
    add_test(
        NAME platform.termination_signal
        COMMAND gdox_termination_signal_tests
    )
    gdox_label_tests(platform platform.termination_signal)
endif()

add_executable(
    gdox_session_storage_policy_tests
    tests/test_session_storage_policy.c
)
target_include_directories(
    gdox_session_storage_policy_tests
    PRIVATE
        tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    gdox_session_storage_policy_tests
    PRIVATE gdox::platform
)
gdox_enable_c_warnings(gdox_session_storage_policy_tests)
add_test(
    NAME platform.session_storage_policy
    COMMAND gdox_session_storage_policy_tests
)
gdox_label_tests(
    platform
    platform.session_storage_policy
)

add_executable(
    gdox_instance_guard_tests
    tests/test_instance_guard.c
)
if(WIN32)
    target_sources(
        gdox_instance_guard_tests
        PRIVATE src/platform/instance_guard_windows.c
    )
else()
    target_sources(
        gdox_instance_guard_tests
        PRIVATE src/platform/instance_guard_posix.c
    )
endif()
target_include_directories(
    gdox_instance_guard_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
gdox_enable_c_warnings(gdox_instance_guard_tests)
add_test(NAME platform.instance_guard COMMAND gdox_instance_guard_tests)
gdox_label_tests(platform platform.instance_guard)

if(WIN32)
    add_executable(
        gdox_windows_command_tests
        tests/test_windows_command.c
        src/platform/windows_command.c
        src/platform/windows_support.c
        src/core/error.c
    )
    target_include_directories(
        gdox_windows_command_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gdox_windows_command_tests PRIVATE advapi32)
    gdox_enable_c_warnings(gdox_windows_command_tests)
    add_test(
        NAME platform.windows_command
        COMMAND gdox_windows_command_tests
    )
    gdox_label_tests(platform platform.windows_command)
endif()
