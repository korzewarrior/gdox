#ifndef GDOX_APP_XENIA_STORAGE_H
#define GDOX_APP_XENIA_STORAGE_H

#include "gdox/error.h"
#include "gdox/xenia_policy.h"
#include "platform/session_storage.h"

#include <stdbool.h>

typedef struct gdox_xenia_storage {
    gdox_session_storage session;
    char storage[GDOX_SESSION_PATH_CAPACITY];
    char cache[GDOX_SESSION_PATH_CAPACITY];
    char content[GDOX_SESSION_PATH_CAPACITY];
    char log_file[GDOX_SESSION_PATH_CAPACITY];
} gdox_xenia_storage;

/* Removes abandoned sessions and legacy rebuildable Xenia artifacts. */
bool gdox_xenia_storage_recover(gdox_error *error);

/* Creates all paths needed by one Xenia process. */
bool gdox_xenia_storage_open(
    const gdox_xenia_launch_policy *policy,
    gdox_xenia_storage *storage,
    gdox_error *error
);

/* Removes all session-derived state after the process tree is gone. */
bool gdox_xenia_storage_close(
    gdox_xenia_storage *storage,
    gdox_error *error
);

#endif
