#ifndef GDOX_APP_RUNTIME_DRIVES_H
#define GDOX_APP_RUNTIME_DRIVES_H

#include "app/runtime_internal.h"

bool gdox_runtime_drives_refresh(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot, gdox_error *error
);
bool gdox_runtime_drives_resolve(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot,
    gdox_optical_presence *presence, gdox_error *error
);
bool gdox_runtime_drives_select(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot,
    const char *id, gdox_error *error
);
bool gdox_runtime_drives_cleanup_pending(const gdox_runtime *runtime);
void gdox_runtime_drives_retry_cleanup(gdox_runtime *runtime);
bool gdox_runtime_drives_close_pending(gdox_runtime *runtime, gdox_error *error);
void gdox_runtime_drives_describe(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot
);

#endif
