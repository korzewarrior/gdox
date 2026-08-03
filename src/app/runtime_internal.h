#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef GDOX_APP_RUNTIME_INTERNAL_H
#define GDOX_APP_RUNTIME_INTERNAL_H

#include "app/preferences.h"
#include "app/runtime.h"
#include "app/runtime_bundle.h"
#include "app/runtime_commands.h"
#include "app/runtime_media.h"
#include "app/xemu_save_storage.h"
#include "app/xenia_storage.h"

#include "gdox/optical.h"
#include "gdox/xenia.h"
#include "platform/portable_sync.h"

#include <stdatomic.h>
#include <stdint.h>

typedef enum gdox_runtime_playback_owner {
    GDOX_RUNTIME_PLAYBACK_NONE = 0,
    GDOX_RUNTIME_PLAYBACK_XEMU,
    GDOX_RUNTIME_PLAYBACK_XENIA,
} gdox_runtime_playback_owner;

struct gdox_runtime {
    gdox_thread thread;
    gdox_mutex mutex;
    atomic_bool stopping;
    atomic_bool preservation_cancelled;
    bool thread_started;
    bool terminal_shutdown_failed;
    gdox_error terminal_shutdown_error;
    bool preservation_hold;
    gdox_runtime_request_queue requests;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_media_session media;
    gdox_runtime_playback_owner playback_owner;
    union {
        gdox_emulator_process *xemu;
        gdox_xenia_process *xenia;
    };
    gdox_xenia_runtime_descriptor xenia_runtime;
    gdox_xenia_storage xenia_storage;
    gdox_xemu_legacy_migration_outcome xemu_save_migration;
    gdox_host_profile host_profile;
    gdox_optical_drive optical_drive;
    gdox_runtime_bundle_status bundle;
};

void gdox_runtime_copy_text(
    char *output,
    size_t capacity,
    const char *text
);
void gdox_runtime_reset_media_snapshot(
    gdox_runtime_snapshot *snapshot,
    gdox_media_source source
);
bool gdox_runtime_apply_media_open_result(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_media_open_result *result
);
void gdox_runtime_mark_drive_unavailable(
    gdox_runtime_snapshot *snapshot,
    const char *notice
);
void gdox_runtime_publish(
    gdox_runtime *runtime,
    const gdox_runtime_snapshot *snapshot
);
void gdox_runtime_publish_optical_failure(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
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
void gdox_runtime_attention(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *operation,
    const gdox_error *error,
    bool has_session,
    bool emulator_running
);
bool gdox_runtime_media_open_can_retry(
    gdox_error_code code,
    bool media_owned,
    bool source_available
);
bool gdox_runtime_bundle_complete(
    const gdox_runtime_bundle_status *bundle
);
void gdox_runtime_copy_bundle_status(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
);
void gdox_runtime_refresh_bundle_snapshot(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
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
