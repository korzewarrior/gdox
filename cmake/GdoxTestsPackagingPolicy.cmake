find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(gdox_label_python_tests labels)
    gdox_label_tests("${labels}" ${ARGN})
    set_tests_properties(
        ${ARGN}
        PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
    )
endfunction()

add_test(
    NAME policy.xenia.generated
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_xenia_policy.py
        --check
)
set_tests_properties(
    policy.xenia.generated
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
gdox_label_python_tests("policy;host-neutral" policy.xenia.generated)

add_test(
    NAME policy.architecture
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_architecture_audit.py
)
gdox_label_python_tests("policy;host-neutral" policy.architecture)

set(
    GDOX_HOST_NEUTRAL_PACKAGING_TESTS
    packaging.elf_compatibility
    packaging.release_build
    packaging.android_source
    packaging.android_build_provenance
    packaging.release_audit
    packaging.release_archive_audit
    packaging.release_package
    packaging.release_notes
    packaging.source
    packaging.xenia_distribution
    packaging.xemu_integration
    packaging.xemu_macos_runtime
    packaging.platform_artwork
)
set(
    GDOX_HOST_NEUTRAL_PACKAGING_SCRIPTS
    test_elf_compatibility.py
    test_build_release.py
    test_android_source_packaging.py
    test_android_build_provenance.py
    test_release_audit.py
    test_release_archive_audit.py
    test_package_release.py
    test_release_notes.py
    test_source_packaging.py
    test_xenia_distribution.py
    test_xemu_integration.py
    test_xemu_macos_runtime.py
    test_steamdeck_artwork.py
)
list(LENGTH GDOX_HOST_NEUTRAL_PACKAGING_TESTS test_count)
math(EXPR last_test "${test_count} - 1")
foreach(index RANGE ${last_test})
    list(GET GDOX_HOST_NEUTRAL_PACKAGING_TESTS ${index} test_name)
    list(GET GDOX_HOST_NEUTRAL_PACKAGING_SCRIPTS ${index} test_script)
    add_test(
        NAME ${test_name}
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_script}
    )
    gdox_label_python_tests("packaging;host-neutral" ${test_name})
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(
        NAME packaging.linux_launcher
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_linux_launcher.py
    )
    add_test(
        NAME packaging.linux_installer
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_linux_installer.py
    )
    gdox_label_python_tests(
        "packaging;platform;linux"
        packaging.linux_launcher
        packaging.linux_installer
    )
endif()
