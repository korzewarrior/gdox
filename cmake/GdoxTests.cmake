function(gdox_label_tests labels)
    set_tests_properties(${ARGN} PROPERTIES LABELS "${labels}")
endfunction()

function(gdox_enable_test_crt target)
    if(MSVC)
        target_compile_definitions(
            ${target}
            PRIVATE _CRT_SECURE_NO_WARNINGS
        )
    endif()
endfunction()

include(GdoxTestsCoreUi)
include(GdoxTestsPlatform)
include(GdoxTestsRuntimeOptical)
include(GdoxTestsPackagingPolicy)
