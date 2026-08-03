#ifndef GDOX_APP_RUNTIME_XEMU_H
#define GDOX_APP_RUNTIME_XEMU_H

#include "app/runtime_internal.h"
#include "app/xemu_process_stop.h"

bool gdox_runtime_xemu_prepare_launch(
    gdox_runtime *runtime,
    gdox_error *error
);
bool gdox_runtime_xemu_start(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_xemu_stop(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_xemu_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
);

#endif
