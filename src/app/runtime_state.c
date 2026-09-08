#include "app/runtime_internal.h"
#include "app/runtime_setup.h"

#include <stdio.h>
#include <string.h>

void gdox_runtime_copy_text(char *output, size_t capacity, const char *text)
{
    const char *source = text != NULL ? text : "";
    size_t bytes;

    if (output == NULL || capacity == 0U) {
        return;
    }
    bytes = strlen(source);
    if (bytes >= capacity) {
        bytes = capacity - 1U;
    }
    memmove(output, source, bytes);
    output[bytes] = '\0';
}

void gdox_runtime_reset_media_snapshot(
    gdox_runtime_snapshot *snapshot,
    gdox_media_source source
)
{
    if (snapshot == NULL) {
        return;
    }
    snapshot->media_source = source;
    snapshot->media_platform = GDOX_MEDIA_PLATFORM_NONE;
    snapshot->media_backend = GDOX_MEDIA_BACKEND_NONE;
    snapshot->image_layout = GDOX_MEDIA_IMAGE_NONE;
    snapshot->x360_image_layout = GDOX_X360_IMAGE_LAYOUT_NONE;
    memset(&snapshot->x360_execution, 0, sizeof(snapshot->x360_execution));
    snapshot->xenia_policy = NULL;
    snapshot->image_source_sectors = 0U;
    snapshot->image_game_partition_lba = 0U;
    snapshot->disc_image_path[0] = '\0';
    snapshot->physical_read_commands = 0U;
    snapshot->physical_read_sectors = 0U;
    snapshot->physical_read_bytes = 0U;
    snapshot->physical_last_lba = 0U;
    memset(&snapshot->nbd_read_stats, 0, sizeof(snapshot->nbd_read_stats));
}

bool gdox_runtime_apply_media_open_result(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_media_open_result *result
)
{
    const gdox_runtime_media_info *info;
    bool valid_identity;

    if (snapshot == NULL || result == NULL
        || result->state < GDOX_RUNTIME_MEDIA_IDENTIFIED
        || result->state > GDOX_RUNTIME_MEDIA_READY) {
        return false;
    }
    info = &result->info;
    valid_identity =
        (info->platform == GDOX_MEDIA_PLATFORM_XBOX
            && info->backend == GDOX_MEDIA_BACKEND_XEMU)
        || (info->platform == GDOX_MEDIA_PLATFORM_XBOX_360
            && info->backend == GDOX_MEDIA_BACKEND_XENIA);
    if (!valid_identity) {
        return false;
    }
    snapshot->media_source = info->source;
    snapshot->media_platform = info->platform;
    snapshot->media_backend = info->backend;
    snapshot->image_layout = info->image_layout;
    snapshot->x360_image_layout = info->x360.layout;
    snapshot->x360_execution = info->x360.execution;
    snapshot->xenia_policy = info->xenia_policy;
    snapshot->image_source_sectors = info->source_sectors;
    snapshot->image_game_partition_lba = info->game_partition_lba;
    gdox_runtime_copy_text(
        snapshot->disc, sizeof(snapshot->disc), info->title
    );
    return true;
}

void gdox_runtime_mark_drive_unavailable(
    gdox_runtime_snapshot *snapshot,
    const char *notice
)
{
    const bool identified = snapshot != NULL
        && ((snapshot->media_platform == GDOX_MEDIA_PLATFORM_XBOX
                && snapshot->media_backend == GDOX_MEDIA_BACKEND_XEMU)
            || (snapshot->media_platform == GDOX_MEDIA_PLATFORM_XBOX_360
                && snapshot->media_backend == GDOX_MEDIA_BACKEND_XENIA));

    if (snapshot == NULL) {
        return;
    }
    if (!identified) {
        gdox_runtime_reset_media_snapshot(
            snapshot, GDOX_MEDIA_PHYSICAL_DISC
        );
    }
    snapshot->phase = identified ? GDOX_RUNTIME_ATTENTION : GDOX_RUNTIME_EMPTY;
    gdox_runtime_copy_text(
        snapshot->drive,
        sizeof(snapshot->drive),
        "Supported drive unavailable"
    );
    if (identified) {
        gdox_runtime_copy_text(
            snapshot->status,
            sizeof(snapshot->status),
            "GDOX needs attention"
        );
        return;
    }
    gdox_runtime_copy_text(
        snapshot->disc, sizeof(snapshot->disc), "No Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Waiting for a supported drive"
    );
    gdox_runtime_copy_text(snapshot->notice, sizeof(snapshot->notice), notice);
}

void gdox_runtime_publish(
    gdox_runtime *runtime,
    const gdox_runtime_snapshot *snapshot
)
{
    if (gdox_mutex_lock(&runtime->mutex)) {
        const gdox_app_settings settings = runtime->snapshot.settings;

        runtime->snapshot = *snapshot;
        runtime->snapshot.settings = settings;
        runtime->snapshot.can_select_drive =
            !atomic_load_explicit(&runtime->stopping, memory_order_acquire)
            && !runtime->device_selection_requested
            && !runtime->snapshot.can_close
            && runtime->snapshot.phase != GDOX_RUNTIME_PLAYING
            && runtime->snapshot.phase != GDOX_RUNTIME_PRESERVING
            && runtime->snapshot.phase != GDOX_RUNTIME_PREPARING;
        if (runtime->device_selection_requested) {
            runtime->snapshot.can_start = false;
            runtime->snapshot.can_preserve = false;
        }
        gdox_runtime_copy_bundle_status(&runtime->snapshot, &runtime->bundle);
        gdox_runtime_setup_describe_pending(runtime, &runtime->snapshot);
        if (atomic_load_explicit(&runtime->stopping, memory_order_acquire)) {
            runtime->snapshot.can_select_drive = false;
            runtime->snapshot.can_start = false;
            runtime->snapshot.can_restart = false;
            runtime->snapshot.can_preserve = false;
            runtime->snapshot.can_close = false;
            runtime->snapshot.can_eject = false;
            gdox_runtime_copy_text(runtime->snapshot.status,
                sizeof(runtime->snapshot.status), "Closing GDOX");
        }
        gdox_mutex_unlock(&runtime->mutex);
    }
}

void gdox_runtime_preferences_from_snapshot(
    const gdox_runtime_snapshot *snapshot,
    gdox_preferences *preferences
)
{
    *preferences = snapshot->settings;
}

void gdox_runtime_set_controls(
    gdox_runtime_snapshot *snapshot,
    gdox_optical_drive optical_drive,
    bool has_session,
    bool emulator_running
)
{
    const bool backend_ready =
        snapshot->media_backend == GDOX_MEDIA_BACKEND_XENIA
        ? snapshot->xenia_ready
        : snapshot->media_backend == GDOX_MEDIA_BACKEND_XEMU
            && snapshot->xemu_ready;

    snapshot->can_start =
        has_session && !emulator_running && backend_ready;
    snapshot->can_restart = has_session && backend_ready;
    snapshot->can_close = emulator_running;
    snapshot->can_eject = snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
        && gdox_optical_drive_can_eject(optical_drive)
        && (has_session || snapshot->phase == GDOX_RUNTIME_EMPTY);
    snapshot->can_preserve =
        snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC && has_session
        && snapshot->media_platform == GDOX_MEDIA_PLATFORM_XBOX;
    snapshot->can_cancel_preservation = false;
}

void gdox_runtime_attention(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *operation,
    const gdox_error *error,
    bool has_session,
    bool emulator_running
)
{
    snapshot->phase = GDOX_RUNTIME_ATTENTION;
    gdox_runtime_copy_text(
        snapshot->status, sizeof(snapshot->status), "GDOX needs attention"
    );
    (void)snprintf(
        snapshot->notice,
        sizeof(snapshot->notice),
        "%.48s: %.106s",
        operation,
        error->message
    );
    gdox_runtime_set_controls(
        snapshot, runtime->optical_drive, has_session, emulator_running
    );
    snapshot->preservation_complete = false;
    gdox_runtime_publish(runtime, snapshot);
}

bool gdox_runtime_media_open_can_retry(
    gdox_error_code code,
    bool media_owned,
    bool source_available
)
{
    if (media_owned || !source_available) {
        return false;
    }
    return code == GDOX_ERROR_NOT_FOUND
        || code == GDOX_ERROR_TRANSPORT
        || code == GDOX_ERROR_IO;
}

void gdox_runtime_publish_optical_failure(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    gdox_runtime_set_controls(
        snapshot, runtime->optical_drive, false, false
    );
    snapshot->can_eject = false;
    gdox_runtime_publish(runtime, snapshot);
}

bool gdox_runtime_bundle_complete(const gdox_runtime_bundle_status *bundle)
{
    return bundle->xemu_available && bundle->full_hdd_isolation
        && bundle->persistent_save_export
        && bundle->configuration_ready
        && bundle->mcpx_ready && bundle->flash_ready && bundle->hdd_ready;
}

void gdox_runtime_copy_bundle_status(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
)
{
    snapshot->xemu_ready = gdox_runtime_bundle_complete(bundle);
    snapshot->bundled_xemu = bundle->bundled;
    snapshot->mcpx_ready = bundle->mcpx_ready;
    snapshot->flash_ready = bundle->flash_ready;
    snapshot->hdd_ready = bundle->hdd_ready;
    gdox_runtime_copy_text(
        snapshot->xemu_executable,
        sizeof(snapshot->xemu_executable),
        bundle->executable
    );
    gdox_runtime_copy_text(
        snapshot->xemu_configuration,
        sizeof(snapshot->xemu_configuration),
        bundle->configuration
    );
    gdox_runtime_copy_text(
        snapshot->mcpx_path, sizeof(snapshot->mcpx_path), bundle->mcpx
    );
    gdox_runtime_copy_text(
        snapshot->flash_path, sizeof(snapshot->flash_path), bundle->flash
    );
    gdox_runtime_copy_text(
        snapshot->hdd_path, sizeof(snapshot->hdd_path), bundle->hdd
    );
    gdox_runtime_describe_bundle(snapshot, bundle);
}

void gdox_runtime_refresh_bundle_snapshot(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    if (gdox_mutex_lock(&runtime->mutex)) {
        gdox_runtime_copy_bundle_status(snapshot, &runtime->bundle);
        gdox_mutex_unlock(&runtime->mutex);
    }
}

void gdox_runtime_describe_bundle(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
)
{
    if (gdox_runtime_bundle_complete(bundle)) {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "xemu is ready with persistent save export"
        );
    } else if (gdox_error_is_set(&bundle->setup_error)) {
        (void)snprintf(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "xemu setup: %.145s",
            bundle->setup_error.message
        );
    } else if (!bundle->xemu_available) {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "Bundled xemu runtime is unavailable"
        );
    } else if (!bundle->configuration_ready || !bundle->hdd_ready) {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "xemu private storage could not be prepared"
        );
    } else {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "Drop MCPX 1.0 and Xbox BIOS files here to finish setup"
        );
    }
}
