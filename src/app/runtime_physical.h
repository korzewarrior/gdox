#ifndef GDOX_APP_RUNTIME_PHYSICAL_H
#define GDOX_APP_RUNTIME_PHYSICAL_H

#include "app/runtime_internal.h"
#include "app/optical_monitor.h"
#include "app/physical_media_monitor.h"

typedef enum gdox_runtime_physical_cleanup {
    GDOX_RUNTIME_PHYSICAL_CLEANUP_NONE = 0,
    GDOX_RUNTIME_PHYSICAL_CLEANUP_REARM,
    GDOX_RUNTIME_PHYSICAL_CLEANUP_REQUIRE_RETRY,
    GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT,
} gdox_runtime_physical_cleanup;

typedef struct gdox_runtime_physical_state {
    gdox_physical_media_monitor monitor;
    const gdox_nbd_export *watched_export;
    uint32_t device_delay;
    uint32_t media_delay;
    uint64_t eject_generation;
    gdox_optical_drive eject_drive;
    gdox_runtime_physical_cleanup cleanup;
} gdox_runtime_physical_state;

void gdox_runtime_physical_initialize(
    gdox_runtime_physical_state *state
);
void gdox_runtime_physical_reset(gdox_runtime_physical_state *state);
bool gdox_runtime_physical_poll(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    gdox_error *error
);
void gdox_runtime_physical_cleanup_completed(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *optical_monitor,
    gdox_error *error
);
void gdox_runtime_physical_validate_cleanup(
    gdox_runtime *runtime,
    gdox_runtime_physical_state *state
);

#endif
