#include "platform/session_storage_policy.h"

#include <stdio.h>
#include <string.h>

#define GDOX_SESSION_MARKER_PREFIX "GDOX-SESSION-OWNER-V1\nname="

bool gdox_session_relative_path_is_safe(const char *relative)
{
    const char *cursor = relative;

    if (relative == NULL || relative[0] == '\0'
        || relative[0] == '/' || relative[0] == '\\') {
        return false;
    }
    while (*cursor != '\0') {
        const char *separator = strpbrk(cursor, "/\\");
        const size_t bytes = separator != NULL
            ? (size_t)(separator - cursor)
            : strlen(cursor);

        if (bytes == 0U
            || (bytes == 1U && cursor[0] == '.')
            || (bytes == 2U && cursor[0] == '.' && cursor[1] == '.')) {
            return false;
        }
        if (separator == NULL) {
            break;
        }
        if (separator[1] == '\0') {
            return false;
        }
        cursor = separator + 1U;
    }
    return true;
}

bool gdox_session_owner_marker_format(
    const char *name,
    char output[GDOX_SESSION_MARKER_CAPACITY],
    size_t *bytes
)
{
    int written;

    if (name == NULL || name[0] == '\0' || output == NULL || bytes == NULL
        || strchr(name, '\n') != NULL || strchr(name, '\r') != NULL) {
        return false;
    }
    written = snprintf(
        output,
        GDOX_SESSION_MARKER_CAPACITY,
        GDOX_SESSION_MARKER_PREFIX "%s\n",
        name
    );
    if (written < 0 || (size_t)written >= GDOX_SESSION_MARKER_CAPACITY) {
        return false;
    }
    *bytes = (size_t)written;
    return true;
}

gdox_session_recovery_state gdox_session_recovery_decide(
    bool trusted_owner,
    gdox_session_lock_observation lock
)
{
    if (!trusted_owner) {
        return GDOX_SESSION_RECOVERY_PRESERVE;
    }
    switch (lock) {
        case GDOX_SESSION_LOCK_ACQUIRED:
            return GDOX_SESSION_RECOVERY_STALE;
        case GDOX_SESSION_LOCK_CONTENDED:
            return GDOX_SESSION_RECOVERY_ACTIVE;
        case GDOX_SESSION_LOCK_FAILED:
            return GDOX_SESSION_RECOVERY_ERROR;
        case GDOX_SESSION_LOCK_NOT_INSPECTED:
        default:
            return GDOX_SESSION_RECOVERY_PRESERVE;
    }
}
