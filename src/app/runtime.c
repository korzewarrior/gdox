#include "app/runtime_internal.h"
#include "app/xemu_performance.h"
#include "app/runtime_actions.h"
#include "app/runtime_playback.h"
#include "app/runtime_physical.h"
#include "app/runtime_session.h"
#include "app/optical_monitor.h"
#include "platform/user_storage.h"

#include "gdox/optical.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool take_request(
    gdox_runtime *runtime,
    gdox_runtime_request_entry *request
)
{
    bool available = false;

    if (gdox_mutex_lock(&runtime->mutex)) {
        available = gdox_runtime_request_dequeue(&runtime->requests, request);
        gdox_mutex_unlock(&runtime->mutex);
    }
    return available;
}

static void publish_missing_drive(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *notice
)
{
    gdox_runtime_mark_drive_unavailable(snapshot, notice);
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, false, false);
    snapshot->can_eject = false;
    gdox_runtime_publish(runtime, snapshot);
}

static void publish_empty_drive(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    gdox_runtime_playback_reset_xenia(runtime, snapshot);
    gdox_runtime_reset_media_snapshot(
        snapshot, GDOX_MEDIA_PHYSICAL_DISC
    );
    snapshot->phase = GDOX_RUNTIME_EMPTY;
    gdox_runtime_copy_text(
        snapshot->drive,
        sizeof(snapshot->drive),
        gdox_optical_drive_name(runtime->optical_drive)
    );
    gdox_runtime_copy_text(
        snapshot->disc, sizeof(snapshot->disc), "No Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status, sizeof(snapshot->status), "Waiting for an Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->notice, sizeof(snapshot->notice), "Drive is ready"
    );
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, false, false);
    gdox_runtime_publish(runtime, snapshot);
}

typedef struct gdox_runtime_loop {
    gdox_runtime_snapshot snapshot;
    gdox_optical_monitor optical_monitor;
    gdox_runtime_physical_state physical;
    uint32_t observation_delay;
    uint32_t read_stats_delay;
    uint32_t cleanup_delay;
    uint32_t unavailable_checks;
    bool force_launch;
    gdox_error error;
} gdox_runtime_loop;

static bool run_cleanup_cycle(
    gdox_runtime *runtime,
    gdox_runtime_loop *loop
)
{
    if (runtime->media.open
        || !gdox_runtime_media_is_owned(&runtime->media)) {
        loop->cleanup_delay = 0U;
        return false;
    }
    if (loop->cleanup_delay > 0U) {
        --loop->cleanup_delay;
        return true;
    }
    gdox_runtime_physical_validate_cleanup(runtime, &loop->physical);
    if (!gdox_runtime_media_close(&runtime->media, &loop->error)) {
        gdox_runtime_attention(
            runtime,
            &loop->snapshot,
            "Could not restore the optical drive",
            &loop->error,
            false,
            false
        );
        loop->cleanup_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS;
        return true;
    }
    loop->observation_delay = 0U;
    if (loop->snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC) {
        publish_empty_drive(runtime, &loop->snapshot);
    } else {
        gdox_runtime_copy_text(
            loop->snapshot.notice,
            sizeof(loop->snapshot.notice),
            "Media cleanup completed"
        );
        gdox_runtime_set_controls(
            &loop->snapshot, runtime->optical_drive, false, false
        );
        gdox_runtime_publish(runtime, &loop->snapshot);
    }
    gdox_runtime_physical_cleanup_completed(
        runtime,
        &loop->snapshot,
        &loop->physical,
        &loop->optical_monitor,
        &loop->error
    );
    return true;
}

static void observe_available_media(
    gdox_runtime *runtime,
    gdox_runtime_loop *loop
)
{
    gdox_optical_presence presence = {0};

    gdox_runtime_refresh_bundle_snapshot(runtime, &loop->snapshot);
    if (!gdox_optical_observe(&presence, &loop->error)) {
        gdox_optical_monitor_observation_failed(&loop->optical_monitor);
        runtime->optical_drive = GDOX_OPTICAL_DRIVE_NONE;
        if (gdox_optical_monitor_has_pending_failure(&loop->optical_monitor)) {
            gdox_runtime_publish_optical_failure(runtime, &loop->snapshot);
        } else {
            publish_missing_drive(
                runtime, &loop->snapshot, loop->error.message
            );
        }
        return;
    }
    if (!presence.drive_present) {
        runtime->optical_drive = GDOX_OPTICAL_DRIVE_NONE;
        (void)gdox_optical_monitor_observe(&loop->optical_monitor, &presence);
        if (loop->unavailable_checks < GDOX_MEDIA_REARM_OBSERVATIONS) {
            ++loop->unavailable_checks;
        }
        if (loop->unavailable_checks >= GDOX_MEDIA_REARM_OBSERVATIONS) {
            gdox_runtime_playback_reset_xenia(runtime, &loop->snapshot);
            gdox_runtime_reset_media_snapshot(
                &loop->snapshot, GDOX_MEDIA_PHYSICAL_DISC
            );
        }
        if (gdox_optical_monitor_has_pending_failure(&loop->optical_monitor)) {
            gdox_runtime_publish_optical_failure(runtime, &loop->snapshot);
        } else {
            publish_missing_drive(
                runtime,
                &loop->snapshot,
                "Connect the supported USB optical drive"
            );
        }
        return;
    }

    loop->unavailable_checks = 0U;
    runtime->optical_drive = presence.drive;
    if (presence.media_status_known && !presence.media_present) {
        (void)gdox_optical_monitor_observe(&loop->optical_monitor, &presence);
        if (gdox_optical_monitor_has_pending_failure(&loop->optical_monitor)) {
            gdox_runtime_publish_optical_failure(runtime, &loop->snapshot);
        } else {
            publish_empty_drive(runtime, &loop->snapshot);
        }
    } else if (
        gdox_optical_monitor_observe(&loop->optical_monitor, &presence)
    ) {
        const gdox_runtime_live_prepare_result prepared =
            gdox_runtime_session_prepare_live(
                runtime, &loop->snapshot, loop->force_launch, &loop->error
            );

        if (prepared != GDOX_RUNTIME_LIVE_PREPARED) {
            gdox_optical_monitor_fail(
                &loop->optical_monitor,
                prepared == GDOX_RUNTIME_LIVE_RETRYABLE_FAILURE
                    ? GDOX_OPTICAL_MONITOR_FAILURE_TRANSIENT
                    : GDOX_OPTICAL_MONITOR_FAILURE_TERMINAL
            );
        }
        if (prepared != GDOX_RUNTIME_LIVE_RETRYABLE_FAILURE) {
            loop->force_launch = false;
        }
    } else if (gdox_optical_monitor_is_armed(&loop->optical_monitor)) {
        loop->snapshot.phase = GDOX_RUNTIME_DISCOVERING;
        gdox_runtime_copy_text(
            loop->snapshot.drive,
            sizeof(loop->snapshot.drive),
            gdox_optical_drive_name(runtime->optical_drive)
        );
        gdox_runtime_copy_text(
            loop->snapshot.status,
            sizeof(loop->snapshot.status),
            "Waiting for the disc to become ready"
        );
        gdox_runtime_publish(runtime, &loop->snapshot);
    }
}

static void run_idle_cycle(gdox_runtime *runtime, gdox_runtime_loop *loop)
{
    if (loop->snapshot.media_source == GDOX_MEDIA_DISC_IMAGE
        || runtime->preservation_hold) {
        return;
    }
    if (loop->observation_delay > 0U) {
        --loop->observation_delay;
        return;
    }
    observe_available_media(runtime, loop);
    loop->observation_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS;
}

static void poll_playback(
    gdox_runtime *runtime,
    gdox_runtime_loop *loop,
    bool physical
)
{
    bool running;
    int exit_code;

    if (!gdox_runtime_playback_running(runtime)) {
        return;
    }
    if (!gdox_runtime_playback_poll(
            runtime, &running, &exit_code, &loop->error
        )) {
        gdox_runtime_attention(
            runtime,
            &loop->snapshot,
            "Could not monitor playback",
            &loop->error,
            true,
            gdox_runtime_playback_running(runtime)
        );
        return;
    }
    if (running) {
        return;
    }
    loop->snapshot.phase = GDOX_RUNTIME_READY;
    gdox_runtime_copy_text(
        loop->snapshot.status,
        sizeof(loop->snapshot.status),
        physical ? "Disc ready" : "Disc image ready"
    );
    if (exit_code != 0) {
        (void)snprintf(
            loop->snapshot.notice,
            sizeof(loop->snapshot.notice),
            "%s exited with status %d",
            loop->snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA
                ? "Xenia"
                : "xemu",
            exit_code
        );
    }
    gdox_runtime_set_controls(
        &loop->snapshot, runtime->optical_drive, true, false
    );
    gdox_runtime_publish(runtime, &loop->snapshot);
}

static void run_active_cycle(gdox_runtime *runtime, gdox_runtime_loop *loop)
{
    const bool physical =
        loop->snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC;

    poll_playback(runtime, loop, physical);
    if (physical) {
        if (gdox_runtime_physical_poll(
                runtime,
                &loop->snapshot,
                &loop->physical,
                &loop->optical_monitor,
                &loop->observation_delay,
                &loop->error
            )) {
            return;
        }
    } else {
        gdox_runtime_physical_reset(&loop->physical);
    }
    if (!physical && runtime->media.exported != NULL
        && gdox_nbd_runtime_error(
            runtime->media.exported, &loop->error
        )) {
        gdox_runtime_attention(
            runtime,
            &loop->snapshot,
            "Disc image failed",
            &loop->error,
            true,
            gdox_runtime_playback_running(runtime)
        );
    }
    if (physical && loop->read_stats_delay == 0U) {
        (void)gdox_runtime_session_refresh_read_stats(
            runtime, &loop->snapshot, true
        );
        loop->read_stats_delay = GDOX_RUNTIME_READ_STATS_INTERVAL_TICKS;
    } else if (physical) {
        --loop->read_stats_delay;
    }
}

static void run_runtime_cycle(gdox_runtime *runtime, gdox_runtime_loop *loop)
{
    gdox_runtime_request_entry request = {0};

    if (run_cleanup_cycle(runtime, loop)) {
        return;
    }
    (void)take_request(runtime, &request);
    if (gdox_runtime_actions_execute(
            runtime,
            &loop->snapshot,
            &request,
            &loop->optical_monitor,
            &loop->observation_delay,
            &loop->force_launch,
            &loop->error
        )) {
        return;
    }
    if (!runtime->media.open) {
        gdox_runtime_physical_reset(&loop->physical);
        run_idle_cycle(runtime, loop);
    } else {
        run_active_cycle(runtime, loop);
    }
}

static void runtime_thread(void *raw_runtime)
{
    gdox_runtime *runtime = raw_runtime;
    gdox_runtime_loop loop = {0};
    gdox_error shutdown_error;
    gdox_runtime_playback_owner shutdown_owner;

    gdox_optical_monitor_initialize(&loop.optical_monitor);
    gdox_runtime_physical_initialize(&loop.physical);
    gdox_runtime_copy_snapshot(runtime, &loop.snapshot);
    while (!atomic_load_explicit(&runtime->stopping, memory_order_acquire)) {
        run_runtime_cycle(runtime, &loop);
        gdox_sleep_ms(100U);
    }
    shutdown_owner = runtime->playback_owner;
    if (!gdox_runtime_playback_shutdown(runtime, &shutdown_error)
        && !gdox_runtime_playback_running(runtime)
        && (shutdown_owner == GDOX_RUNTIME_PLAYBACK_XEMU
            || (shutdown_owner == GDOX_RUNTIME_PLAYBACK_XENIA
                && !runtime->xenia_storage.session.active))) {
        runtime->terminal_shutdown_failed = true;
        runtime->terminal_shutdown_error = shutdown_error;
        if (gdox_mutex_lock(&runtime->mutex)) {
            runtime->snapshot.phase = GDOX_RUNTIME_ATTENTION;
            gdox_runtime_copy_text(
                runtime->snapshot.status,
                sizeof(runtime->snapshot.status),
                "GDOX closed with an emulator error"
            );
            gdox_runtime_copy_text(
                runtime->snapshot.notice,
                sizeof(runtime->snapshot.notice),
                shutdown_error.message
            );
            gdox_mutex_unlock(&runtime->mutex);
        }
    }
    if (!gdox_runtime_playback_running(runtime)
        && gdox_runtime_media_is_owned(&runtime->media)) {
        (void)gdox_runtime_session_close(
            runtime, &loop.snapshot, &loop.error
        );
    }
}

static void apply_default_preservation_directory(gdox_preferences *preferences)
{
    char path[GDOX_STORAGE_PATH_CAPACITY];
    gdox_error error;

    if (preferences->preservation_directory[0] != '\0') {
        return;
    }
    if (gdox_user_data_path("preservations", path, &error)
        && gdox_storage_ensure_directory(path, &error)) {
        gdox_runtime_copy_text(
            preferences->preservation_directory,
            sizeof(preferences->preservation_directory),
            path
        );
    }
}

gdox_runtime *gdox_runtime_create(gdox_host_profile host_profile)
{
    gdox_runtime *runtime;
    gdox_preferences preferences;
    gdox_error preferences_error;
    gdox_error preferences_save_error;
    gdox_error bundle_error;
    bool preferences_loaded;
    bool preferences_saved = true;
    bool bundle_prepared;
    uint8_t requested_resolution_scale;

    if (host_profile != GDOX_HOST_PROFILE_DESKTOP
        && host_profile != GDOX_HOST_PROFILE_HANDHELD) {
        return NULL;
    }
    runtime = calloc(1U, sizeof(*runtime));
    if (runtime == NULL || !gdox_mutex_init(&runtime->mutex)) {
        free(runtime);
        return NULL;
    }
    runtime->host_profile = host_profile;
    preferences_loaded = gdox_preferences_load(
        &preferences,
        &preferences_error
    );
    if (!preferences_loaded) {
        gdox_preferences_defaults(&preferences);
    }
    requested_resolution_scale = preferences.internal_resolution_scale;
    preferences.internal_resolution_scale =
        gdox_xemu_effective_resolution_scale(
            host_profile, preferences.internal_resolution_scale
        );
    apply_default_preservation_directory(&preferences);
    if (preferences.internal_resolution_scale != requested_resolution_scale) {
        preferences_saved = gdox_preferences_save(
            &preferences, &preferences_save_error
        );
    }
    bundle_prepared = gdox_runtime_bundle_prepare(
        preferences.xemu_override,
        &runtime->bundle,
        &bundle_error
    );
    runtime->snapshot.phase = GDOX_RUNTIME_DISCOVERING;
    runtime->snapshot.settings = preferences;
    gdox_runtime_copy_bundle_status(&runtime->snapshot, &runtime->bundle);
    gdox_runtime_describe_bundle(&runtime->snapshot, &runtime->bundle);
    if (!bundle_prepared) {
        (void)snprintf(
            runtime->snapshot.xemu_setup,
            sizeof(runtime->snapshot.xemu_setup),
            "xemu setup: %.145s",
            bundle_error.message
        );
    }
    gdox_runtime_copy_text(
        runtime->snapshot.drive,
        sizeof(runtime->snapshot.drive),
        "Checking optical drive"
    );
    gdox_runtime_copy_text(
        runtime->snapshot.disc, sizeof(runtime->snapshot.disc), "No Xbox disc"
    );
    gdox_runtime_copy_text(
        runtime->snapshot.status,
        sizeof(runtime->snapshot.status),
        "Starting GDOX"
    );
    gdox_runtime_copy_text(
        runtime->snapshot.notice,
        sizeof(runtime->snapshot.notice),
        !preferences_loaded
            ? "Saved settings could not be loaded; defaults are active"
            : !preferences_saved
                ? "Handheld display profile is active; settings could not be saved"
                : "Native runtime is initializing"
    );
    atomic_init(&runtime->stopping, false);
    atomic_init(&runtime->preservation_cancelled, false);
    if (!gdox_thread_start(&runtime->thread, runtime_thread, runtime)) {
        gdox_mutex_destroy(&runtime->mutex);
        free(runtime);
        return NULL;
    }
    runtime->thread_started = true;
    return runtime;
}
