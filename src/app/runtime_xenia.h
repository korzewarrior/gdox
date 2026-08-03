#ifndef GDOX_APP_RUNTIME_XENIA_H
#define GDOX_APP_RUNTIME_XENIA_H

#include "app/runtime_internal.h"

bool gdox_runtime_xenia_prepare(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
);
bool gdox_runtime_xenia_start(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_xenia_stop(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_xenia_cleanup(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_xenia_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
);

#endif
