#include "app/runtime_setup.h"

#include <stdio.h>
#include <string.h>

static bool setup_request(gdox_runtime_request_kind kind)
{
    return kind >= GDOX_RUNTIME_REQUEST_SET_XEMU
        && kind <= GDOX_RUNTIME_REQUEST_IMPORT_BIOS;
}

bool gdox_runtime_setup_flush_preferences(
    gdox_runtime *runtime, bool force, gdox_error *error
)
{
    gdox_preferences preferences;
    gdox_error_clear(error);
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not read pending settings");
        return false;
    }
    if (!runtime->preferences_dirty || (!force && !runtime->preferences_save_requested)) {
        gdox_mutex_unlock(&runtime->mutex);
        return true;
    }
    gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
    runtime->preferences_dirty = false;
    runtime->preferences_save_requested = false;
    gdox_mutex_unlock(&runtime->mutex);
    if (gdox_preferences_save(&preferences, error)) {
        return true;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        /* Retry after another edit or on shutdown, without an idle retry loop. */
        runtime->preferences_dirty = true;
        gdox_mutex_unlock(&runtime->mutex);
    }
    return false;
}

bool gdox_runtime_setup_take_request(
    gdox_runtime *runtime, gdox_runtime_request_entry *request
)
{
    bool found = false;
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    if (!atomic_load_explicit(&runtime->stopping, memory_order_acquire)) {
        gdox_runtime_request_queue *queue = &runtime->requests;
        for (size_t offset = 0U; offset < queue->count; ++offset) {
            const size_t index = (queue->head + offset) % GDOX_RUNTIME_REQUEST_CAPACITY;
            if (!setup_request(queue->entries[index].kind)) {
                continue;
            }
            *request = queue->entries[index];
            for (size_t next = offset + 1U; next < queue->count; ++next) {
                queue->entries[(queue->head + next - 1U) % GDOX_RUNTIME_REQUEST_CAPACITY] =
                    queue->entries[(queue->head + next) % GDOX_RUNTIME_REQUEST_CAPACITY];
            }
            if (--queue->count == 0U) queue->head = 0U;
            found = true;
            break;
        }
    }
    gdox_mutex_unlock(&runtime->mutex);
    return found;
}

bool gdox_runtime_setup_submit(
    gdox_runtime *runtime, gdox_runtime_request_kind kind, const char *path
)
{
    gdox_runtime_request_entry request = {.kind = kind};
    const char *selected = path != NULL ? path : "";

    if (runtime == NULL || !gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    if (!setup_request(kind) || strlen(selected) >= sizeof(request.path)
        || strchr(selected, '\n') != NULL || strchr(selected, '\r') != NULL
        || (kind != GDOX_RUNTIME_REQUEST_SET_XEMU && selected[0] == '\0')) {
        gdox_runtime_copy_text(runtime->snapshot.notice, sizeof(runtime->snapshot.notice),
            "Selected xemu or firmware path is invalid");
        gdox_mutex_unlock(&runtime->mutex);
        return false;
    }
    if ((runtime->setup_request_pending && kind == GDOX_RUNTIME_REQUEST_SET_XEMU)
        || runtime->device_selection_requested
        || atomic_load_explicit(&runtime->stopping, memory_order_acquire)
        || runtime->requests.count >= GDOX_RUNTIME_REQUEST_CAPACITY
        || runtime->snapshot.can_close
        || runtime->snapshot.phase == GDOX_RUNTIME_PLAYING
        || runtime->snapshot.phase == GDOX_RUNTIME_PRESERVING
        || runtime->snapshot.phase == GDOX_RUNTIME_PREPARING) {
        gdox_runtime_copy_text(runtime->snapshot.notice,
            sizeof(runtime->snapshot.notice),
            "Runtime is busy; finish the current action before changing xemu setup");
        gdox_mutex_unlock(&runtime->mutex);
        return false;
    }
    gdox_runtime_copy_text(request.path, sizeof(request.path), selected);
    if (kind == GDOX_RUNTIME_REQUEST_SET_XEMU) {
        gdox_runtime_copy_text(runtime->snapshot.settings.xemu_override,
            sizeof(runtime->snapshot.settings.xemu_override), selected);
        gdox_runtime_setup_mark_preferences_dirty(runtime);
    }
    if (!gdox_runtime_request_enqueue(&runtime->requests, &request)) {
        gdox_mutex_unlock(&runtime->mutex);
        return false;
    }
    runtime->setup_request_pending = true;
    gdox_runtime_setup_describe_pending(runtime, &runtime->snapshot);
    gdox_runtime_copy_text(runtime->snapshot.notice, sizeof(runtime->snapshot.notice),
        kind == GDOX_RUNTIME_REQUEST_SET_XEMU ? "Preparing selected xemu"
                                            : "Importing firmware");
    gdox_mutex_unlock(&runtime->mutex);
    return true;
}

bool gdox_runtime_setup_execute(
    gdox_runtime *runtime, gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request
)
{
    gdox_runtime_bundle_status bundle;
    gdox_firmware_kind firmware = GDOX_FIRMWARE_MCPX;
    char selected[GDOX_EMULATOR_PATH_CAPACITY];
    gdox_error error;
    bool prepared;

    if (!setup_request(request->kind)) {
        return false;
    }
    if (atomic_load_explicit(&runtime->stopping, memory_order_acquire)
        || !gdox_mutex_lock(&runtime->mutex)) {
        return true;
    }
    bundle = runtime->bundle;
    gdox_runtime_copy_text(selected, sizeof(selected),
        runtime->snapshot.settings.xemu_override);
    gdox_mutex_unlock(&runtime->mutex);

    if (runtime->playback_owner != GDOX_RUNTIME_PLAYBACK_NONE) {
        gdox_error_set(&error, GDOX_ERROR_INVALID_ARGUMENT,
            "Close playback before changing xemu setup, then retry");
        prepared = false;
        if (request->kind == GDOX_RUNTIME_REQUEST_SET_XEMU) {
            bundle.xemu_available = false;
            bundle.configuration_ready = false;
            gdox_runtime_copy_text(bundle.executable, sizeof(bundle.executable),
                strcmp(request->path, GDOX_XEMU_INCLUDED_SELECTION) == 0 ? "" : request->path);
            bundle.setup_error = error;
        }
    } else if (request->kind == GDOX_RUNTIME_REQUEST_SET_XEMU) {
        gdox_error save_error;
        /* A force-close during preparation must retain the requested choice. */
        if (gdox_runtime_setup_flush_preferences(runtime, true, &save_error)) {
            prepared = gdox_runtime_bundle_prepare(request->path, &bundle, &error);
        } else {
            bundle.xemu_available = false;
            bundle.configuration_ready = false;
            gdox_runtime_copy_text(bundle.executable, sizeof(bundle.executable),
                strcmp(request->path, GDOX_XEMU_INCLUDED_SELECTION) == 0 ? "" : request->path);
            gdox_error_set(&error, save_error.code, "Could not save xemu selection");
            (void)snprintf(error.message, sizeof(error.message),
                "Could not save xemu selection: %.320s", save_error.message);
            bundle.setup_error = error;
            prepared = false;
        }
    } else if (request->kind == GDOX_RUNTIME_REQUEST_IMPORT_FIRMWARE) {
        prepared = gdox_runtime_bundle_import_firmware_auto(
            request->path, selected, &firmware, &bundle, &error);
    } else {
        firmware = request->kind == GDOX_RUNTIME_REQUEST_IMPORT_MCPX
            ? GDOX_FIRMWARE_MCPX : GDOX_FIRMWARE_FLASH;
        prepared = gdox_runtime_bundle_import_firmware(
            firmware, request->path, selected, &bundle, &error);
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return true;
    }
    /* Failed explicit choices still describe that choice and its actual error. */
    if (prepared || request->kind == GDOX_RUNTIME_REQUEST_SET_XEMU) {
        runtime->bundle = bundle;
    }
    runtime->setup_request_pending = false;
    for (size_t offset = 0U; offset < runtime->requests.count; ++offset) {
        if (setup_request(runtime->requests.entries[
                (runtime->requests.head + offset) % GDOX_RUNTIME_REQUEST_CAPACITY].kind)) {
            runtime->setup_request_pending = true;
            break;
        }
    }
    gdox_runtime_copy_bundle_status(snapshot, &runtime->bundle);
    gdox_runtime_set_controls(snapshot, runtime->optical_drive,
        runtime->media.open, snapshot->can_close);
    if (!prepared) {
        (void)snprintf(snapshot->notice, sizeof(snapshot->notice),
            "%s: %.140s", request->kind == GDOX_RUNTIME_REQUEST_SET_XEMU
                ? "xemu selection" : "Firmware import", error.message);
    } else {
        gdox_runtime_copy_text(snapshot->notice, sizeof(snapshot->notice),
            request->kind == GDOX_RUNTIME_REQUEST_SET_XEMU
                ? (strcmp(request->path, GDOX_XEMU_INCLUDED_SELECTION) == 0
                    ? "Using the xemu included with GDOX" : "Using selected xemu")
                : firmware == GDOX_FIRMWARE_MCPX ? "MCPX boot ROM imported"
                                                : "Xbox BIOS imported");
    }
    gdox_mutex_unlock(&runtime->mutex);
    gdox_runtime_publish(runtime, snapshot);
    return true;
}
