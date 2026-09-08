#ifndef GDOX_APP_RUNTIME_LOOP_H
#define GDOX_APP_RUNTIME_LOOP_H

#include "app/runtime_physical.h"

typedef struct gdox_runtime_loop {
    gdox_runtime_snapshot snapshot;
    gdox_optical_monitor optical_monitor;
    gdox_runtime_physical_state physical;
    uint32_t observation_delay;
    uint32_t read_stats_delay;
    uint32_t cleanup_delay;
    uint32_t unavailable_checks;
    uint32_t inventory_delay;
    uint32_t pending_cleanup_delay;
    bool force_launch;
    gdox_error error;
} gdox_runtime_loop;

#endif
