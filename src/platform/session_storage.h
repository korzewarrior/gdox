#ifndef GDOX_PLATFORM_SESSION_STORAGE_H
#define GDOX_PLATFORM_SESSION_STORAGE_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_SESSION_PATH_CAPACITY 4096U
#define GDOX_SESSION_OWNER_MARKER ".gdox-session-owner"
#define GDOX_SESSION_LOCK_FILE ".gdox-session-lock"

typedef struct gdox_session_storage {
    char root[GDOX_SESSION_PATH_CAPACITY];
    intptr_t lock_handle;
    bool active;
    bool memory_backed;
} gdox_session_storage;

/* Removes abandoned GDOX session directories left by an interrupted process. */
bool gdox_session_storage_recover(gdox_error *error);

/* Removes abandoned sessions from the platform's verified memory filesystem. */
bool gdox_session_storage_recover_memory(gdox_error *error);

/* Creates one uniquely named session directory under the selected temp root. */
bool gdox_session_storage_create(
    gdox_session_storage *storage,
    gdox_error *error
);

/*
 * Creates a session only on a verified memory-backed filesystem. Platforms
 * without such a facility fail closed with GDOX_ERROR_UNSUPPORTED.
 */
bool gdox_session_storage_create_memory(
    gdox_session_storage *storage,
    gdox_error *error
);

/* Builds a safe path below an active session directory. */
bool gdox_session_storage_path(
    const gdox_session_storage *storage,
    const char *relative,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
);

/* Recursively removes an active session without following links. */
bool gdox_session_storage_cleanup(
    gdox_session_storage *storage,
    gdox_error *error
);

/*
 * Removes one known relative tree below a trusted, application-selected root
 * without following links. Missing paths are accepted. POSIX verifies the
 * root owner and write permissions. Windows relies on the root's existing
 * access control and rejects reparse points at every traversed directory.
 */
bool gdox_session_storage_remove_relative(
    const char *root,
    const char *relative,
    gdox_error *error
);

#endif
