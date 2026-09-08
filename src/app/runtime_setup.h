#ifndef GDOX_APP_RUNTIME_SETUP_H
#define GDOX_APP_RUNTIME_SETUP_H

#include "app/runtime_internal.h"

#include <string.h>

/* Called under the snapshot mutex; storage is owned by the runtime worker. */
static inline void gdox_runtime_setup_mark_preferences_dirty(gdox_runtime *runtime)
{
    runtime->preferences_dirty = true;
    runtime->preferences_save_requested = true;
}
bool gdox_runtime_setup_flush_preferences(
    gdox_runtime *runtime, bool force, gdox_error *error
);

/* Submission only copies the requested path; preparation runs on the worker. */
bool gdox_runtime_setup_submit(
    gdox_runtime *runtime, gdox_runtime_request_kind kind, const char *path
);
/* Sources recovery may pass stalled drive actions without consuming them. */
bool gdox_runtime_setup_take_request(
    gdox_runtime *runtime, gdox_runtime_request_entry *request
);
/* The existing runtime worker owns all slow firmware/runtime preparation. */
bool gdox_runtime_setup_execute(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request
);
/* Called with the runtime mutex held after copying authoritative bundle state. */
static inline void gdox_runtime_setup_describe_pending(
    const gdox_runtime *runtime, gdox_runtime_snapshot *snapshot
)
{
    if (!runtime->setup_request_pending) {
        return;
    }
    snapshot->xemu_ready = false;
    snapshot->can_start = false;
    snapshot->can_restart = false;
    gdox_runtime_copy_text(snapshot->xemu_setup, sizeof(snapshot->xemu_setup),
        "Preparing xemu setup");
    gdox_runtime_copy_text(snapshot->xemu_executable,
        sizeof(snapshot->xemu_executable),
        strcmp(snapshot->settings.xemu_override, GDOX_XEMU_INCLUDED_SELECTION) == 0
            ? "" : snapshot->settings.xemu_override);
}

#endif
