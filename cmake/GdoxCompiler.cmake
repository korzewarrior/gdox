function(gdox_enable_c_warnings target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:/W4>
                $<$<COMPILE_LANGUAGE:C>:/permissive->
                $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>
        )
        if(GDOX_WARNINGS_AS_ERRORS)
            target_compile_options(
                ${target}
                PRIVATE $<$<COMPILE_LANGUAGE:C>:/WX>
            )
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-Wall>
                $<$<COMPILE_LANGUAGE:C>:-Wextra>
                $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
                $<$<COMPILE_LANGUAGE:C>:-Wconversion>
                $<$<COMPILE_LANGUAGE:C>:-Wshadow>
                $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
                $<$<COMPILE_LANGUAGE:C>:-Wundef>
        )
        if(GDOX_WARNINGS_AS_ERRORS)
            target_compile_options(
                ${target}
                PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror>
            )
        endif()
    endif()
endfunction()

function(gdox_enable_cxx_warnings target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:/W4>
                $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        )
        if(GDOX_WARNINGS_AS_ERRORS)
            target_compile_options(
                ${target}
                PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>
            )
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-Wall>
                $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
                $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
                $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
        )
        if(GDOX_WARNINGS_AS_ERRORS)
            target_compile_options(
                ${target}
                PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Werror>
            )
        endif()
    endif()
endfunction()
