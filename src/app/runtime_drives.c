#include "app/runtime_drives.h"

#include "app/runtime_playback.h"

#include <stdio.h>
#include <string.h>

static bool same_device(const char *left, const char *right)
{
    return left[0] != '\0' && strcmp(left, right) == 0;
}

static gdox_runtime_pending_cleanup *pending_for(
    gdox_runtime *runtime, const char *id
)
{
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        gdox_runtime_pending_cleanup *pending = &runtime->pending_cleanup[index];
        if (gdox_runtime_media_is_owned(&pending->media)
            && same_device(pending->device.id, id)) {
            return pending;
        }
    }
    return NULL;
}

bool gdox_runtime_drives_cleanup_pending(const gdox_runtime *runtime)
{
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        if (gdox_runtime_media_is_owned(&runtime->pending_cleanup[index].media)) {
            return true;
        }
    }
    return false;
}

void gdox_runtime_drives_describe(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot
)
{
    snapshot->active_optical_device = runtime->optical_device;
    snapshot->can_select_drive = !gdox_runtime_playback_running(runtime)
        && snapshot->phase != GDOX_RUNTIME_PRESERVING
        && snapshot->phase != GDOX_RUNTIME_PREPARING;
    snapshot->pending_cleanup_count = 0U;
    snapshot->pending_cleanup_notice[0] = '\0';
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        const gdox_runtime_pending_cleanup *pending = &runtime->pending_cleanup[index];
        if (!gdox_runtime_media_is_owned(&pending->media)) {
            continue;
        }
        ++snapshot->pending_cleanup_count;
        if (snapshot->pending_cleanup_notice[0] == '\0') {
            (void)snprintf(
                snapshot->pending_cleanup_notice,
                sizeof(snapshot->pending_cleanup_notice),
                "%.150s (%.90s) still needs restoration. %.90s",
                pending->device.name, pending->device.location,
                pending->error.message
            );
        }
    }
    for (size_t index = 0U; index < snapshot->optical_device_count; ++index) {
        snapshot->optical_devices[index].cleanup_pending = pending_for(
            runtime, snapshot->optical_devices[index].device.id
        ) != NULL;
    }
}

bool gdox_runtime_drives_refresh(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot, gdox_error *error
)
{
    gdox_optical_device devices[GDOX_OPTICAL_MAX_DEVICES];
    size_t count = 0U;
    const bool query_media = !gdox_runtime_media_is_owned(&runtime->media)
        && !gdox_runtime_drives_cleanup_pending(runtime)
        && snapshot->phase != GDOX_RUNTIME_PRESERVING
        && snapshot->phase != GDOX_RUNTIME_PREPARING;

    if (!gdox_optical_list_devices(
            devices, GDOX_OPTICAL_MAX_DEVICES, &count, query_media, error
        )) {
        gdox_runtime_copy_text(snapshot->optical_inventory_notice,
            sizeof(snapshot->optical_inventory_notice),
            error->message[0] != '\0' ? error->message
                : "Could not refresh the optical drive list");
        for (size_t index = 0U; index < snapshot->optical_device_count; ++index) {
            snapshot->optical_devices[index].connected = false;
        }
        return false;
    }
    snapshot->optical_inventory_notice[0] = '\0';
    if (count > GDOX_OPTICAL_MAX_DEVICES) {
        count = GDOX_OPTICAL_MAX_DEVICES;
    }
    snapshot->optical_device_count = count;
    for (size_t index = 0U; index < count; ++index) {
        snapshot->optical_devices[index] = (gdox_app_optical_device){
            .device = devices[index], .connected = true,
            .cleanup_pending = pending_for(runtime, devices[index].id) != NULL,
        };
    }
    /* Retain disconnected cleanup owners in the picker, clearly labelled. */
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        const gdox_runtime_pending_cleanup *pending = &runtime->pending_cleanup[index];
        bool found = false;
        if (!gdox_runtime_media_is_owned(&pending->media)) {
            continue;
        }
        for (size_t candidate = 0U; candidate < count; ++candidate) {
            found = found || same_device(devices[candidate].id, pending->device.id);
        }
        if (!found && snapshot->optical_device_count < GDOX_OPTICAL_MAX_DEVICES) {
            snapshot->optical_devices[snapshot->optical_device_count++] =
                (gdox_app_optical_device){
                    .device = pending->device, .cleanup_pending = true,
                };
        }
    }
    gdox_runtime_drives_describe(runtime, snapshot);
    return true;
}

static const gdox_app_optical_device *resolve_entry(
    gdox_runtime *runtime, const gdox_runtime_snapshot *snapshot,
    const char *selected
)
{
    const gdox_app_optical_device *best = NULL;
    unsigned int best_score = 0U;

    for (size_t index = 0U; index < snapshot->optical_device_count; ++index) {
        const gdox_app_optical_device *entry = &snapshot->optical_devices[index];
        unsigned int score;
        if (selected[0] != '\0') {
            if (same_device(entry->device.id, selected)) {
                return entry;
            }
            continue;
        }
        if (!entry->connected || !entry->device.accessible
            || entry->device.drive == GDOX_OPTICAL_DRIVE_NONE
            || entry->cleanup_pending) {
            continue;
        }
        score = entry->device.media_status_known
            ? (entry->device.media_present ? 4U : 0U) : 2U;
        if (same_device(entry->device.id, runtime->optical_device.id)) {
            ++score;
        }
        if (best == NULL || score > best_score) {
            best = entry;
            best_score = score;
        }
    }
    return best;
}

bool gdox_runtime_drives_resolve(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot,
    gdox_optical_presence *presence, gdox_error *error
)
{
    char selected[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    const gdox_app_optical_device *entry;

    *presence = (gdox_optical_presence){0};
    gdox_error_clear(error);
    /* An owned source keeps its original physical target until explicit close. */
    if (gdox_runtime_media_is_owned(&runtime->media)) {
        if (runtime->optical_device.id[0] == '\0') {
            gdox_error_set(error, GDOX_ERROR_INTERNAL,
                "owned optical media has no physical device identity");
            return false;
        }
        snapshot->active_optical_device = runtime->optical_device;
        *presence = (gdox_optical_presence){
            .drive_present = true, .drive = runtime->optical_device.drive,
            .media_status_known = runtime->optical_device.media_status_known,
            .media_present = runtime->optical_device.media_present,
        };
        return true;
    }
    if (snapshot->optical_inventory_notice[0] != '\0') {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT,
            snapshot->optical_inventory_notice);
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not read drive selection");
        return false;
    }
    gdox_runtime_copy_text(selected, sizeof(selected),
        runtime->snapshot.settings.optical_device_id);
    gdox_mutex_unlock(&runtime->mutex);
    entry = resolve_entry(runtime, snapshot, selected);
    if (entry == NULL || !entry->connected) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND,
            selected[0] != '\0' ? "The selected drive is disconnected"
                : gdox_runtime_drives_cleanup_pending(runtime)
                    ? "Waiting for drive restoration; another connected drive can be selected"
                    : snapshot->optical_device_count != 0U
                        ? "No accessible supported optical drive is available"
                        : "Connect a supported optical drive");
        return false;
    }
    runtime->optical_device = entry->device;
    runtime->optical_drive = entry->device.drive;
    snapshot->active_optical_device = entry->device;
    if (entry->cleanup_pending) {
        gdox_error_set(error, GDOX_ERROR_IO,
            "The selected drive still needs restoration; choose another drive or wait for cleanup");
        return false;
    }
    if (entry->device.drive == GDOX_OPTICAL_DRIVE_NONE) {
        gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
            "The selected drive does not have a supported firmware profile");
        return false;
    }
    if (!entry->device.accessible) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT,
            "The selected drive cannot be accessed; close other drive tools and check permissions");
        return false;
    }
    *presence = (gdox_optical_presence){
        .drive_present = true, .drive = entry->device.drive,
        .media_status_known = entry->device.media_status_known,
        .media_present = entry->device.media_present,
    };
    return true;
}

static bool retain_failed_cleanup(gdox_runtime *runtime, gdox_error *error)
{
    if (runtime->optical_device.id[0] == '\0'
        || runtime->media.info.source == GDOX_MEDIA_DISC_IMAGE
        || pending_for(runtime, runtime->optical_device.id) != NULL) {
        return false;
    }
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        gdox_runtime_pending_cleanup *pending = &runtime->pending_cleanup[index];
        if (!gdox_runtime_media_is_owned(&pending->media)) {
            pending->device = runtime->optical_device;
            pending->error = *error;
            pending->media = runtime->media;
            memset(&runtime->media, 0, sizeof(runtime->media));
            return true;
        }
    }
    gdox_error_set(error, GDOX_ERROR_IO,
        "Pending drive cleanup is full; restore a previous drive before switching");
    return false;
}

bool gdox_runtime_drives_select(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot,
    const char *id, gdox_error *error
)
{
    gdox_preferences preferences;
    const gdox_app_optical_device *selected = NULL;

    gdox_error_clear(error);
    if (id == NULL || strlen(id) >= GDOX_OPTICAL_DEVICE_ID_CAPACITY
        || strchr(id, '\n') != NULL || strchr(id, '\r') != NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "drive selection is invalid");
        return false;
    }
    if (gdox_runtime_playback_running(runtime)
        || snapshot->phase == GDOX_RUNTIME_PRESERVING) {
        gdox_error_set(error, GDOX_ERROR_IO,
            "Close playback or cancel preservation before switching drives");
        return false;
    }
    if (gdox_runtime_media_is_owned(&runtime->media)
        && (!runtime->media.open
            || (id[0] != '\0' && !same_device(id, runtime->optical_device.id))
            || snapshot->media_source == GDOX_MEDIA_DISC_IMAGE)) {
        if (!gdox_runtime_media_close(&runtime->media, error)
            && !retain_failed_cleanup(runtime, error)) {
            return false;
        }
    }
    for (size_t index = 0U; index < snapshot->optical_device_count; ++index) {
        if (same_device(id, snapshot->optical_devices[index].device.id)) {
            selected = &snapshot->optical_devices[index];
            break;
        }
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not save drive selection");
        return false;
    }
    gdox_runtime_copy_text(runtime->snapshot.settings.optical_device_id,
        sizeof(runtime->snapshot.settings.optical_device_id), id);
    if (selected != NULL) {
        (void)snprintf(runtime->snapshot.settings.optical_device_label,
            sizeof(runtime->snapshot.settings.optical_device_label),
            "%s (%s)", selected->device.name, selected->device.location);
    } else if (id[0] == '\0') {
        runtime->snapshot.settings.optical_device_label[0] = '\0';
    }
    preferences = runtime->snapshot.settings;
    snapshot->settings = preferences;
    gdox_mutex_unlock(&runtime->mutex);
    if (!gdox_preferences_save(&preferences, error)) {
        gdox_runtime_copy_text(snapshot->notice, sizeof(snapshot->notice),
            "Drive selection changed, but settings could not be saved");
    }
    gdox_runtime_drives_describe(runtime, snapshot);
    return true;
}

void gdox_runtime_drives_retry_cleanup(gdox_runtime *runtime)
{
    if (runtime->media.open || gdox_runtime_playback_running(runtime)) {
        return;
    }
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        gdox_runtime_pending_cleanup *pending = &runtime->pending_cleanup[index];
        if (gdox_runtime_media_is_owned(&pending->media)) {
            (void)gdox_runtime_media_close(&pending->media, &pending->error);
        }
    }
}

bool gdox_runtime_drives_close_pending(gdox_runtime *runtime, gdox_error *error)
{
    bool success = true;
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        gdox_runtime_pending_cleanup *pending = &runtime->pending_cleanup[index];
        if (gdox_runtime_media_is_owned(&pending->media)
            && !gdox_runtime_media_close(&pending->media, &pending->error)
            && gdox_runtime_media_is_owned(&pending->media)) {
            if (success) {
                *error = pending->error;
            }
            success = false;
        }
    }
    return success;
}
