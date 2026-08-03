#ifndef GDOX_APP_RUNTIME_PLAYBACK_H
#define GDOX_APP_RUNTIME_PLAYBACK_H

#include "app/runtime_internal.h"

bool gdox_runtime_playback_running(const gdox_runtime *runtime);
bool gdox_runtime_playback_ready(const gdox_runtime_snapshot *snapshot);
void gdox_runtime_playback_prepare(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
);
void gdox_runtime_playback_reset_xenia(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
);
bool gdox_runtime_playback_start(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
);
bool gdox_runtime_playback_stop(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
);
bool gdox_runtime_playback_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
);
bool gdox_runtime_playback_shutdown(
    gdox_runtime *runtime,
    gdox_error *error
);

#endif
