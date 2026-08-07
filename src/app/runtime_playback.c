#include "app/runtime_playback.h"

#include "app/runtime_xemu.h"
#include "app/runtime_xenia.h"

#include <stdio.h>
#include <string.h>

static bool owner_has_process(const gdox_runtime *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    switch (runtime->playback_owner) {
        case GDOX_RUNTIME_PLAYBACK_XEMU:
            return runtime->xemu != NULL;
        case GDOX_RUNTIME_PLAYBACK_XENIA:
            return runtime->xenia != NULL;
        case GDOX_RUNTIME_PLAYBACK_NONE:
            return false;
    }
    return false;
}

static void destroy_owned_process(gdox_runtime *runtime)
{
    switch (runtime->playback_owner) {
        case GDOX_RUNTIME_PLAYBACK_XEMU:
            gdox_emulator_process_destroy(runtime->xemu);
            runtime->xemu = NULL;
            break;
        case GDOX_RUNTIME_PLAYBACK_XENIA:
            gdox_xenia_process_destroy(runtime->xenia);
            runtime->xenia = NULL;
            break;
        case GDOX_RUNTIME_PLAYBACK_NONE:
            break;
    }
    runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
}

static void set_missing_process_error(gdox_error *error)
{
    gdox_error_set(
        error,
        GDOX_ERROR_INTERNAL,
        "playback ownership does not contain a process"
    );
}

bool gdox_runtime_playback_running(const gdox_runtime *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    if (runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_NONE) {
        return runtime->xemu != NULL || runtime->xenia != NULL;
    }
    return owner_has_process(runtime);
}

bool gdox_runtime_playback_ready(const gdox_runtime_snapshot *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    if (snapshot->media_backend == GDOX_MEDIA_BACKEND_XENIA) {
        return snapshot->xenia_ready;
    }
    return snapshot->media_backend == GDOX_MEDIA_BACKEND_XEMU
        && snapshot->xemu_ready;
}

void gdox_runtime_playback_prepare(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    gdox_error error;

    if (runtime != NULL && snapshot != NULL
        && snapshot->media_backend == GDOX_MEDIA_BACKEND_XENIA) {
        (void)gdox_runtime_xenia_prepare(runtime, snapshot, &error);
    }
}

void gdox_runtime_playback_reset_xenia(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    const bool running = gdox_runtime_playback_running(runtime);

    if (runtime != NULL && !running) {
        memset(&runtime->xenia_runtime, 0, sizeof(runtime->xenia_runtime));
    }
    if (snapshot == NULL || running) {
        return;
    }
    snapshot->xenia_ready = false;
    snapshot->bundled_xenia = false;
    snapshot->xenia_policy = NULL;
    snapshot->xenia_executable[0] = '\0';
    snapshot->xenia_setup[0] = '\0';
}

bool gdox_runtime_playback_start(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    gdox_runtime_playback_owner owner;
    bool started;

    gdox_error_clear(error);
    if (runtime == NULL || snapshot == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "runtime and playback state are required"
        );
        return false;
    }
    if (runtime->playback_owner != GDOX_RUNTIME_PLAYBACK_NONE
        || runtime->xemu != NULL || runtime->xenia != NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a playback process is already owned"
        );
        return false;
    }
    if (snapshot->media_backend != runtime->media.info.backend) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "playback state does not match the open media session"
        );
        return false;
    }
    if (snapshot->media_backend == GDOX_MEDIA_BACKEND_XEMU) {
        owner = GDOX_RUNTIME_PLAYBACK_XEMU;
    } else if (snapshot->media_backend == GDOX_MEDIA_BACKEND_XENIA) {
        owner = GDOX_RUNTIME_PLAYBACK_XENIA;
    } else {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "media backend is unavailable"
        );
        return false;
    }

    if (owner == GDOX_RUNTIME_PLAYBACK_XEMU) {
        snapshot->phase = GDOX_RUNTIME_PREPARING;
        gdox_runtime_copy_text(
            snapshot->status,
            sizeof(snapshot->status),
            "Preparing Original Xbox playback"
        );
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Checking persistent Xbox saved games"
        );
        gdox_runtime_set_controls(
            snapshot, runtime->optical_drive, false, false
        );
        gdox_runtime_publish(runtime, snapshot);
        if (!gdox_runtime_xemu_prepare_launch(runtime, error)) {
            return false;
        }
    }

    runtime->playback_owner = owner;
    started = owner == GDOX_RUNTIME_PLAYBACK_XEMU
        ? gdox_runtime_xemu_start(runtime, error)
        : gdox_runtime_xenia_start(runtime, error);
    if (!started) {
        if (!owner_has_process(runtime)) {
            runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        }
        return false;
    }
    if (!owner_has_process(runtime)) {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        set_missing_process_error(error);
        return false;
    }

    snapshot->phase = GDOX_RUNTIME_PLAYING;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        owner == GDOX_RUNTIME_PLAYBACK_XENIA
            ? "Xenia is running"
            : "xemu is running"
    );
    if (owner == GDOX_RUNTIME_PLAYBACK_XEMU
        && runtime->xemu_save_migration.retained_due_to_rejected_migration) {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Legacy Xbox hard disk retained because migration was rejected. Playback is using the included HDD and available saved games."
        );
    } else if (owner == GDOX_RUNTIME_PLAYBACK_XEMU
        && runtime->xemu_save_migration.retained_due_to_unclassified) {
        const uint32_t entries = runtime->xemu_save_migration
            .unclassified_tdata_entries;

        (void)snprintf(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Saved games ready. Legacy Xbox hard disk kept because %u title-data file%s (%llu bytes) could not be safely classified.",
            entries,
            entries == 1U ? "" : "s",
            (unsigned long long)runtime->xemu_save_migration
                .unclassified_tdata_bytes
        );
    } else if (owner == GDOX_RUNTIME_PLAYBACK_XEMU
        && runtime->xemu_save_migration.retained_due_to_save_conflict) {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Saved games ready. Legacy Xbox hard disk kept because some legacy saves conflict with existing saved games."
        );
    } else {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
                ? "Live physical-disc session is active"
                : "Read-only disc-image session is active"
        );
    }
    gdox_runtime_set_controls(
        snapshot, runtime->optical_drive, true, true
    );
    gdox_runtime_publish(runtime, snapshot);
    return true;
}

bool gdox_runtime_playback_stop(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    bool stopped;

    gdox_error_clear(error);
    if (runtime == NULL || snapshot == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "runtime and playback state are required"
        );
        return false;
    }
    if (runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_NONE) {
        if (runtime->xemu != NULL || runtime->xenia != NULL) {
            set_missing_process_error(error);
            return false;
        }
        return true;
    }
    if (!owner_has_process(runtime)) {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        set_missing_process_error(error);
        return false;
    }

    stopped = runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_XEMU
        ? gdox_runtime_xemu_stop(runtime, error)
        : gdox_runtime_xenia_stop(runtime, error);
    if (!stopped) {
        if (!owner_has_process(runtime)) {
            runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        }
        return false;
    }
    if (owner_has_process(runtime)) {
        destroy_owned_process(runtime);
    } else {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
    }

    snapshot->phase = GDOX_RUNTIME_READY;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
            ? "Disc ready"
            : "Disc image ready"
    );
    gdox_runtime_set_controls(
        snapshot, runtime->optical_drive, runtime->media.open, false
    );
    gdox_runtime_publish(runtime, snapshot);
    return true;
}

bool gdox_runtime_playback_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    bool polled;

    gdox_error_clear(error);
    if (runtime == NULL || running == NULL || exit_code == NULL
        || runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_NONE) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "playback process is not running"
        );
        return false;
    }
    if (!owner_has_process(runtime)) {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        set_missing_process_error(error);
        return false;
    }
    polled = runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_XEMU
        ? gdox_runtime_xemu_poll(runtime, running, exit_code, error)
        : gdox_runtime_xenia_poll(runtime, running, exit_code, error);
    if (!polled) {
        if (!owner_has_process(runtime)) {
            runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        }
        return false;
    }
    if (*running) {
        if (!owner_has_process(runtime)) {
            runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
            set_missing_process_error(error);
            return false;
        }
        return true;
    }
    if (owner_has_process(runtime)) {
        destroy_owned_process(runtime);
    } else {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
    }
    return true;
}

bool gdox_runtime_playback_shutdown(
    gdox_runtime *runtime,
    gdox_error *error
)
{
    gdox_error stop_error;
    bool stopped;

    gdox_error_clear(error);
    if (runtime == NULL) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "runtime is required"
        );
        return false;
    }
    if (runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_NONE) {
        if (runtime->xemu != NULL || runtime->xenia != NULL) {
            set_missing_process_error(error);
            return false;
        }
        return true;
    }
    if (!owner_has_process(runtime)) {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
        set_missing_process_error(error);
        return false;
    }

    gdox_error_clear(&stop_error);
    stopped = runtime->playback_owner == GDOX_RUNTIME_PLAYBACK_XEMU
        ? gdox_runtime_xemu_stop(runtime, &stop_error)
        : gdox_runtime_xenia_stop(runtime, &stop_error);
    if (stopped && owner_has_process(runtime)) {
        destroy_owned_process(runtime);
    } else if (!owner_has_process(runtime)) {
        runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
    }
    if (!stopped) {
        if (gdox_error_is_set(&stop_error)) {
            *error = stop_error;
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "playback required forced shutdown"
            );
        }
    }
    return stopped;
}
