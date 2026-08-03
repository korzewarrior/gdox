#ifndef GDOX_WINDOWS_COMMAND_H
#define GDOX_WINDOWS_COMMAND_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct gdox_windows_command {
    wchar_t *text;
    size_t length;
    size_t capacity;
} gdox_windows_command;

bool gdox_windows_command_add_wide(
    gdox_windows_command *command,
    const wchar_t *argument,
    gdox_error *error
);
bool gdox_windows_command_add_utf8(
    gdox_windows_command *command,
    const char *argument,
    gdox_error *error
);
void gdox_windows_command_destroy(gdox_windows_command *command);

#endif
