#include "app/runtime_actions.h"

#include "app/runtime_playback.h"

static void use_physical_source(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    gdox_error *error
)
{
    bool ready = true;

    runtime->preservation_hold = false;
    if (gdox_runtime_playback_running(runtime)) {
        ready = gdox_runtime_playback_stop(runtime, snapshot, error);
    }
    if (ready && gdox_runtime_media_is_owned(&runtime->media)) {
        ready = gdox_runtime_session_close(runtime, snapshot, error);
    }
    if (ready) {
        gdox_runtime_session_select_physical(
            runtime, snapshot, optical_monitor
        );
        *observation_delay = 0U;
    } else {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not switch to the physical disc",
            error,
            runtime->media.open,
            gdox_runtime_playback_running(runtime)
        );
    }
}

static void open_requested_image(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request,
    gdox_error *error
)
{
    bool ready = request->path[0] != '\0';

    gdox_error_clear(error);
    runtime->preservation_hold = false;
    if (ready && gdox_runtime_playback_running(runtime)) {
        ready = gdox_runtime_playback_stop(runtime, snapshot, error);
    }
    if (ready && gdox_runtime_media_is_owned(&runtime->media)) {
        ready = gdox_runtime_session_close(runtime, snapshot, error);
    }
    if (ready) {
        (void)gdox_runtime_session_prepare_image(
            runtime, snapshot, request->path, true, error
        );
        return;
    }
    if (!gdox_error_is_set(error)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "the selected image path is unavailable"
        );
    }
    gdox_runtime_attention(
        runtime,
        snapshot,
        "Could not switch to the disc image",
        error,
        runtime->media.open,
        gdox_runtime_playback_running(runtime)
    );
}

static void preserve_requested_disc(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request,
    uint32_t *observation_delay,
    gdox_error *error
)
{
    bool ready = true;

    if (gdox_runtime_playback_running(runtime)) {
        ready = gdox_runtime_playback_stop(runtime, snapshot, error);
    }
    if (ready && gdox_runtime_media_is_owned(&runtime->media)) {
        ready = gdox_runtime_session_close(runtime, snapshot, error);
    }
    if (!ready) {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not close the live session",
            error,
            runtime->media.open,
            gdox_runtime_playback_running(runtime)
        );
        return;
    }
    if (!gdox_runtime_run_preservation(runtime, snapshot, request, error)) {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not preserve disc",
            error,
            false,
            false
        );
        *observation_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS;
    } else {
        *observation_delay = 0U;
    }
}

static bool eject_physical_disc(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    gdox_error *error
)
{
    const bool had_session = gdox_runtime_media_is_owned(&runtime->media);
    const bool was_held = runtime->preservation_hold;
    bool ejected = true;

    runtime->preservation_hold = false;
    if (gdox_runtime_playback_running(runtime)) {
        ejected = gdox_runtime_playback_stop(runtime, snapshot, error);
    }
    if (ejected && gdox_runtime_media_is_owned(&runtime->media)) {
        ejected = gdox_runtime_session_close(runtime, snapshot, error);
    }
    if (ejected) {
        ejected = gdox_optical_eject(runtime->optical_drive, error);
    }
    if (!ejected) {
        gdox_runtime_attention(
            runtime,
            snapshot,
            had_session ? "Could not eject disc" : "Eject failed",
            error,
            runtime->media.open,
            gdox_runtime_playback_running(runtime)
        );
    }
    if (ejected) {
        gdox_optical_monitor_eject_completed(
            optical_monitor,
            GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED
        );
    } else {
        gdox_optical_monitor_session_ended(optical_monitor);
    }
    *observation_delay = had_session || was_held
        ? GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS
        : 0U;
    return had_session || was_held;
}

static void restart_playback(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    if (!gdox_runtime_playback_stop(runtime, snapshot, error)
        || !gdox_runtime_playback_start(runtime, snapshot, error)) {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not restart playback",
            error,
            true,
            gdox_runtime_playback_running(runtime)
        );
    }
}

bool gdox_runtime_actions_execute(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    bool *force_launch,
    gdox_error *error
)
{
    const gdox_runtime_command_state state = {
        .media_source = snapshot->media_source,
        .has_session = runtime->media.open,
        .emulator_running = gdox_runtime_playback_running(runtime),
        .has_saved_image = snapshot->disc_image_path[0] != '\0',
    };
    const gdox_runtime_action action =
        gdox_runtime_plan_request(request, &state);

    switch (action) {
        case GDOX_RUNTIME_ACTION_USE_PHYSICAL:
            use_physical_source(
                runtime, snapshot, optical_monitor, observation_delay, error
            );
            return true;
        case GDOX_RUNTIME_ACTION_OPEN_IMAGE:
            open_requested_image(runtime, snapshot, request, error);
            return true;
        case GDOX_RUNTIME_ACTION_PRESERVE_PHYSICAL:
            preserve_requested_disc(
                runtime, snapshot, request, observation_delay, error
            );
            return true;
        case GDOX_RUNTIME_ACTION_APPLY_DISPLAY:
            if (!gdox_runtime_playback_stop(runtime, snapshot, error)
                || !gdox_runtime_playback_start(runtime, snapshot, error)) {
                gdox_runtime_attention(
                    runtime,
                    snapshot,
                    "Could not apply display settings",
                    error,
                    true,
                    gdox_runtime_playback_running(runtime)
                );
            }
            return false;
        case GDOX_RUNTIME_ACTION_REOPEN_IMAGE:
        {
            char image_path[GDOX_EMULATOR_PATH_CAPACITY];

            gdox_runtime_copy_text(
                image_path, sizeof(image_path), snapshot->disc_image_path
            );
            (void)gdox_runtime_session_prepare_image(
                runtime, snapshot, image_path, true, error
            );
            return true;
        }
        case GDOX_RUNTIME_ACTION_DISCOVER_PHYSICAL:
            runtime->preservation_hold = false;
            *force_launch = true;
            gdox_optical_monitor_retry(optical_monitor);
            *observation_delay = 0U;
            return false;
        case GDOX_RUNTIME_ACTION_EJECT_PHYSICAL:
            return eject_physical_disc(
                runtime, snapshot, optical_monitor, observation_delay, error
            );
        case GDOX_RUNTIME_ACTION_STOP_EMULATOR:
            if (!gdox_runtime_playback_stop(runtime, snapshot, error)) {
                gdox_runtime_attention(
                    runtime,
                    snapshot,
                    "Could not close playback",
                    error,
                    true,
                    gdox_runtime_playback_running(runtime)
                );
            }
            return false;
        case GDOX_RUNTIME_ACTION_RESTART_EMULATOR:
            restart_playback(runtime, snapshot, error);
            return false;
        case GDOX_RUNTIME_ACTION_START_EMULATOR:
            if (!gdox_runtime_playback_start(runtime, snapshot, error)) {
                gdox_runtime_attention(
                    runtime,
                    snapshot,
                    "Could not start playback",
                    error,
                    true,
                    gdox_runtime_playback_running(runtime)
                );
            }
            return false;
        case GDOX_RUNTIME_ACTION_NONE:
            return false;
    }
    return false;
}
