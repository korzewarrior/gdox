#ifndef GDOX_PLATFORM_XEMU_RUNTIME_SESSION_H
#define GDOX_PLATFORM_XEMU_RUNTIME_SESSION_H

#include "gdox/error.h"
#include "platform/session_storage.h"

#include <stdbool.h>
#include <stddef.h>

#if defined(_WIN32)
#include <wchar.h>
#endif

typedef struct gdox_xemu_environment {
#if defined(_WIN32)
    wchar_t *block;
#else
    char **values;
    size_t count;
#endif
} gdox_xemu_environment;

bool gdox_xemu_runtime_session_open(
    gdox_session_storage *storage,
    gdox_error *error
);

bool gdox_xemu_environment_create(
    const char *session_root,
    gdox_xemu_environment *environment,
    gdox_error *error
);

void gdox_xemu_environment_destroy(gdox_xemu_environment *environment);

#endif
