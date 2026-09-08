if(GDOX_BUILD_OPTICAL)
    add_executable(gdox_runtime_drives_tests
        tests/test_runtime_drives.c
        src/app/runtime_drives.c
        src/core/error.c
    )
    target_include_directories(gdox_runtime_drives_tests PRIVATE
        tests ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if(NOT WIN32)
        target_link_libraries(gdox_runtime_drives_tests PRIVATE Threads::Threads)
    endif()
    gdox_enable_c_warnings(gdox_runtime_drives_tests)
    gdox_enable_test_crt(gdox_runtime_drives_tests)
    add_test(NAME runtime.drives COMMAND gdox_runtime_drives_tests)
    gdox_label_tests(runtime runtime.drives)
endif()

if(GDOX_BUILD_OPTICAL)
    add_executable(gdox_runtime_drive_loop_tests tests/test_runtime_drive_loop.c)
    target_include_directories(gdox_runtime_drive_loop_tests PRIVATE src)
    target_link_libraries(gdox_runtime_drive_loop_tests PRIVATE gdox::runtime)
    gdox_enable_c_warnings(gdox_runtime_drive_loop_tests)
    gdox_enable_test_crt(gdox_runtime_drive_loop_tests)
    add_test(NAME runtime.drive_loop COMMAND gdox_runtime_drive_loop_tests
        "${CMAKE_CURRENT_BINARY_DIR}/drive-loop-config")
    set_tests_properties(runtime.drive_loop PROPERTIES TIMEOUT 15)
    gdox_label_tests(runtime runtime.drive_loop)
endif()
