add_executable(
    gdox_tests
    tests/test_disc.c
    tests/test_emulator.c
    tests/test_hash.c
    tests/test_hdd_cache.c
    tests/test_main.c
    tests/test_nbd.c
    tests/test_optical_monitor.c
    tests/test_preferences.c
    tests/test_preservation_naming.c
    tests/test_preserve.c
    tests/test_protocol.c
    tests/test_runtime_bundle.c
    tests/test_security.c
    tests/test_session.c
    tests/test_source.c
    tests/test_xdvdfs.c
)
target_link_libraries(gdox_tests PRIVATE gdox::core)
target_sources(
    gdox_tests
    PRIVATE $<TARGET_OBJECTS:gdox_application_support>
)
target_include_directories(
    gdox_tests
    PRIVATE
        tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    protocol
    disc
    emulator
    hash
    hdd_cache
    nbd
    optical_monitor
    preferences
    preservation_naming
    preserve
    runtime_bundle
    security
    session
    source
    xdvdfs
)
foreach(group IN LISTS GDOX_TEST_GROUPS)
    add_test(NAME core.${group} COMMAND gdox_tests ${group})
endforeach()
