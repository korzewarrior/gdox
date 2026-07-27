#define WIN32_LEAN_AND_MEAN

#include "platform/windows_support.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void gdox_windows_io_error(
    gdox_error *error,
    const char *operation,
    DWORD code
)
{
    char detail[160] = {0};
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        0U,
        detail,
        (DWORD)sizeof(detail),
        NULL
    );

    while (length > 0U
        && (detail[length - 1U] == '\r'
            || detail[length - 1U] == '\n'
            || detail[length - 1U] == ' ')) {
        detail[--length] = '\0';
    }
    (void)snprintf(
        message,
        sizeof(message),
        "%s: %s (Windows error %lu)",
        operation,
        length != 0U ? detail : "operating-system request failed",
        (unsigned long)code
    );
    gdox_error_set(error, GDOX_ERROR_IO, message);
}

wchar_t *gdox_windows_wide_path(
    const char *path,
    gdox_error *error
)
{
    int characters;
    wchar_t *output;

    if (path == NULL || path[0] == '\0') {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "path is required");
        return NULL;
    }
    characters = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path,
        -1,
        NULL,
        0
    );
    if (characters <= 0) {
        gdox_windows_io_error(error, "path is not valid UTF-8", GetLastError());
        return NULL;
    }
    if ((size_t)characters > SIZE_MAX / sizeof(*output)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "path is too long");
        return NULL;
    }
    output = malloc((size_t)characters * sizeof(*output));
    if (output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate Windows path");
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            path,
            -1,
            output,
            characters
        ) != characters) {
        const DWORD code = GetLastError();
        free(output);
        gdox_windows_io_error(error, "could not convert Windows path", code);
        return NULL;
    }
    return output;
}
