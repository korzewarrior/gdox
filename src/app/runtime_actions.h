#ifndef GDOX_APP_RUNTIME_ACTIONS_H
#define GDOX_APP_RUNTIME_ACTIONS_H

#include "app/runtime_session.h"

bool gdox_runtime_actions_execute(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    bool *force_launch,
    gdox_error *error
);

#endif
