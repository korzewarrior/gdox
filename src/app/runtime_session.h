#ifndef GDOX_APP_RUNTIME_SESSION_H
#define GDOX_APP_RUNTIME_SESSION_H

#include "app/runtime_internal.h"
#include "app/optical_monitor.h"

enum {
    GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS = 5U,
    GDOX_RUNTIME_LIVE_DEVICE_INTERVAL_TICKS = 9U,
    GDOX_RUNTIME_LIVE_MEDIA_INTERVAL_TICKS = 9U,
    GDOX_RUNTIME_READ_STATS_INTERVAL_TICKS = 10U,
};

typedef enum gdox_runtime_live_prepare_result {
    GDOX_RUNTIME_LIVE_PREPARED = 0,
    GDOX_RUNTIME_LIVE_RETRYABLE_FAILURE,
    GDOX_RUNTIME_LIVE_TERMINAL_FAILURE,
} gdox_runtime_live_prepare_result;

typedef enum gdox_runtime_physical_end_reason {
    GDOX_RUNTIME_PHYSICAL_MEDIA_CHANGED = 0,
    GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED,
    GDOX_RUNTIME_PHYSICAL_DRIVE_DISCONNECTED,
    GDOX_RUNTIME_PHYSICAL_SESSION_FAILED,
} gdox_runtime_physical_end_reason;

bool gdox_runtime_session_refresh_read_stats(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    bool publish
);
bool gdox_runtime_session_close(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
);
gdox_runtime_live_prepare_result gdox_runtime_session_prepare_live(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    bool force_launch,
    gdox_error *error
);
bool gdox_runtime_session_prepare_image(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *path,
    bool launch,
    gdox_error *error
);
void gdox_runtime_session_select_physical(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor
);
bool gdox_runtime_session_end_physical(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_end_reason reason,
    uint64_t eject_generation,
    bool *eject_authorized,
    const gdox_error *observation_error,
    gdox_optical_monitor *optical_monitor
);

#endif
