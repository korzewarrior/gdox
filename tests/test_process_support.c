#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int gdox_test_failures = 0;
char gdox_test_program_path[4096];

bool gdox_test_initialize_program_path(const char *argument_zero)
{
    char executable[4096];
    char *separator;
    size_t directory_bytes;
#if defined(_WIN32)
    static const char helper_name[] = "gdox_test_xemu_helper.exe";
    const DWORD length = GetModuleFileNameA(
        NULL, executable, (DWORD)sizeof(executable)
    );

    (void)argument_zero;
    if (length == 0U || length >= sizeof(executable)) {
        return false;
    }
    separator = strrchr(executable, '\\');
    {
        char *forward_separator = strrchr(executable, '/');

        if (forward_separator != NULL
            && (separator == NULL || forward_separator > separator)) {
            separator = forward_separator;
        }
    }
#else
    static const char helper_name[] = "gdox_test_xemu_helper";

    if (argument_zero == NULL || realpath(argument_zero, executable) == NULL) {
        return false;
    }
    separator = strrchr(executable, '/');
#endif
    if (separator == NULL) {
        return false;
    }
    directory_bytes = (size_t)(separator - executable) + 1U;
    if (directory_bytes + sizeof(helper_name) > sizeof(gdox_test_program_path)) {
        return false;
    }
    memcpy(gdox_test_program_path, executable, directory_bytes);
    memcpy(
        gdox_test_program_path + directory_bytes,
        helper_name,
        sizeof(helper_name)
    );
    return true;
}
