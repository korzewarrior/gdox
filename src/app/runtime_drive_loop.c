#include "app/runtime_drive_loop.h"
#include "app/runtime_drives.h"
#include "app/runtime_playback.h"
#include "app/runtime_session.h"

static bool apply_drive_selection(gdox_runtime *runtime, gdox_runtime_loop *loop)
{
    char id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    bool requested = false;

    if (gdox_mutex_lock(&runtime->mutex)) {
        requested = runtime->device_selection_requested;
        if (requested) {
            gdox_runtime_copy_text(id, sizeof(id), runtime->requested_device_id);
            runtime->device_selection_requested = false;
        }
        gdox_mutex_unlock(&runtime->mutex);
    }
    if (!requested) {
        return false;
    }
    if (!gdox_runtime_drives_select(runtime, &loop->snapshot, id, &loop->error)) {
        gdox_runtime_attention(runtime, &loop->snapshot,
            "Could not change optical drive", &loop->error,
            runtime->media.open, gdox_runtime_playback_running(runtime));
        return true;
    }
    runtime->preservation_hold = false;
    if (!gdox_runtime_media_is_owned(&runtime->media)) {
        gdox_runtime_physical_reset(&loop->physical);
        gdox_runtime_session_select_physical(
            runtime, &loop->snapshot, &loop->optical_monitor);
        loop->observation_delay = 0U;
        loop->cleanup_delay = 0U;
        loop->force_launch = false;
    }
    loop->inventory_delay = 0U;
    loop->pending_cleanup_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS * 5U;
    gdox_runtime_drives_describe(runtime, &loop->snapshot);
    gdox_runtime_publish(runtime, &loop->snapshot);
    return true;
}

static void refresh_drive_inventory(gdox_runtime *runtime, gdox_runtime_loop *loop)
{
    if (loop->inventory_delay > 0U) {
        --loop->inventory_delay;
        return;
    }
    gdox_error inventory_error;
    (void)gdox_runtime_drives_refresh(runtime, &loop->snapshot, &inventory_error);
    gdox_runtime_publish(runtime, &loop->snapshot);
    loop->inventory_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS;
}

bool gdox_runtime_drive_loop_poll(gdox_runtime *runtime, gdox_runtime_loop *loop)
{
    refresh_drive_inventory(runtime, loop);
    if (apply_drive_selection(runtime, loop)) {
        return true;
    }
    if (loop->pending_cleanup_delay == 0U) {
        gdox_runtime_drives_retry_cleanup(runtime);
        gdox_runtime_drives_describe(runtime, &loop->snapshot);
        loop->pending_cleanup_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS;
    } else {
        --loop->pending_cleanup_delay;
    }
    return false;
}
