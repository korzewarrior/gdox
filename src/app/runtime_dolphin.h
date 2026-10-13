#ifndef GDOX_RUNTIME_DOLPHIN_H
#define GDOX_RUNTIME_DOLPHIN_H
#include "app/runtime_internal.h"
bool gdox_runtime_dolphin_prepare(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot, gdox_error *error);
bool gdox_runtime_dolphin_start(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_dolphin_stop(gdox_runtime *runtime, gdox_error *error);
bool gdox_runtime_dolphin_poll(gdox_runtime *runtime, bool *running, int *exit_code, gdox_error *error);
#endif
