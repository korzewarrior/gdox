#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef GDOX_APP_RUNTIME_INTERNAL_H
#define GDOX_APP_RUNTIME_INTERNAL_H

#include "app/preferences.h"
#include "app/runtime.h"
#include "app/runtime_bundle.h"

#include "gdox/nbd.h"
#include "gdox/optical.h"
#include "platform/portable_sync.h"

#include <stdatomic.h>
#include <stdint.h>

#define GDOX_RUNTIME_COMMAND_START UINT32_C(0x01)
#define GDOX_RUNTIME_COMMAND_RESTART UINT32_C(0x02)
#define GDOX_RUNTIME_COMMAND_CLOSE UINT32_C(0x04)
#define GDOX_RUNTIME_COMMAND_EJECT UINT32_C(0x08)
#define GDOX_RUNTIME_COMMAND_PRESERVE UINT32_C(0x10)
#define GDOX_RUNTIME_COMMAND_APPLY_DISPLAY UINT32_C(0x20)
#define GDOX_RUNTIME_COMMAND_OPEN_IMAGE UINT32_C(0x40)
#define GDOX_RUNTIME_COMMAND_USE_PHYSICAL UINT32_C(0x80)

struct gdox_runtime {
    gdox_thread thread;
    gdox_mutex mutex;
    atomic_bool stopping;
    atomic_bool preservation_cancelled;
    bool thread_started;
    bool preservation_hold;
    uint32_t commands;
    gdox_preservation_format pending_preservation_format;
    bool pending_preservation_verify;
    char pending_preservation_path[GDOX_EMULATOR_PATH_CAPACITY];
    char pending_image_path[GDOX_EMULATOR_PATH_CAPACITY];
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
    gdox_error *error
);

#endif
