#ifndef GDOX_WINDOWS_SUPPORT_H
#define GDOX_WINDOWS_SUPPORT_H

#include "gdox/error.h"

#include <windows.h>

#include <stdbool.h>
#include <wchar.h>

void gdox_windows_io_error(
    gdox_error *error,
    const char *operation,
    DWORD code
);
wchar_t *gdox_windows_wide_text(
    const char *text,
    gdox_error *error
);
wchar_t *gdox_windows_wide_path(
    const char *path,
    gdox_error *error
);
bool gdox_windows_verify_private_directory(
    const wchar_t *path,
    gdox_error *error
);
bool gdox_windows_ensure_private_directory(
    const wchar_t *path,
    bool *created,
    gdox_error *error
);

#endif
