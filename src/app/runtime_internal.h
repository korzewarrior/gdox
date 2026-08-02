#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef GDOX_APP_RUNTIME_INTERNAL_H
#define GDOX_APP_RUNTIME_INTERNAL_H

#include "app/preferences.h"
#include "app/runtime.h"
#include "app/runtime_bundle.h"
#include "app/runtime_commands.h"

#include "gdox/nbd.h"
#include "gdox/optical.h"
#include "platform/portable_sync.h"

#include <stdatomic.h>
#include <stdint.h>

struct gdox_runtime {
    gdox_thread thread;
    gdox_mutex mutex;
    atomic_bool stopping;
    atomic_bool preservation_cancelled;
    bool thread_started;
    bool preservation_hold;
    gdox_runtime_request_queue requests;
    gdox_runtime_snapshot snapshot;
    gdox_nbd_export *exported;
    gdox_emulator_process *emulator;
    gdox_optical_drive optical_drive;
    gdox_runtime_bundle_status bundle;
};

void gdox_runtime_copy_text(
    char *output,
    size_t capacity,
    const char *text
);
void gdox_runtime_publish(
    gdox_runtime *runtime,
    const gdox_runtime_snapshot *snapshot
);
void gdox_runtime_preferences_from_snapshot(
    const gdox_runtime_snapshot *snapshot,
    gdox_preferences *preferences
);
void gdox_runtime_set_controls(
    gdox_runtime_snapshot *snapshot,
    gdox_optical_drive optical_drive,
    bool has_session,
    bool emulator_running
);
bool gdox_runtime_bundle_complete(
    const gdox_runtime_bundle_status *bundle
);
void gdox_runtime_copy_bundle_status(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
);
void gdox_runtime_describe_bundle(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
);
bool gdox_runtime_run_preservation(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request,
    gdox_error *error
);

#endif
