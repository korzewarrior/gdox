#ifndef GDOX_WINDOWS_SUPPORT_H
#define GDOX_WINDOWS_SUPPORT_H

#include "gdox/error.h"

#include <windows.h>

#include <wchar.h>

void gdox_windows_io_error(
    gdox_error *error,
    const char *operation,
    DWORD code
);
wchar_t *gdox_windows_wide_path(
    const char *path,
    gdox_error *error
);

#endif
