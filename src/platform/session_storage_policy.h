#ifndef GDOX_PLATFORM_SESSION_STORAGE_POLICY_H
#define GDOX_PLATFORM_SESSION_STORAGE_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#define GDOX_SESSION_MARKER_CAPACITY 512U

typedef enum gdox_session_lock_observation {
    GDOX_SESSION_LOCK_NOT_INSPECTED = 0,
    GDOX_SESSION_LOCK_ACQUIRED,
    GDOX_SESSION_LOCK_CONTENDED,
    GDOX_SESSION_LOCK_FAILED,
} gdox_session_lock_observation;

typedef enum gdox_session_recovery_state {
    GDOX_SESSION_RECOVERY_PRESERVE = 0,
    GDOX_SESSION_RECOVERY_ACTIVE,
    GDOX_SESSION_RECOVERY_STALE,
    GDOX_SESSION_RECOVERY_ERROR,
} gdox_session_recovery_state;

bool gdox_session_relative_path_is_safe(const char *relative);

bool gdox_session_owner_marker_format(
    const char *name,
    char output[GDOX_SESSION_MARKER_CAPACITY],
    size_t *bytes
);

gdox_session_recovery_state gdox_session_recovery_decide(
    bool trusted_owner,
    gdox_session_lock_observation lock
);

#endif
