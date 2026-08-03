#include "app/runtime_internal.h"
#include "app/xemu_performance.h"
#include "app/runtime_playback.h"
#include "app/runtime_xenia.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool enqueue_request(
    gdox_runtime *runtime,
    const gdox_runtime_request_entry *request
)
{
    if (gdox_runtime_request_enqueue(&runtime->requests, request)) {
        return true;
    }
    gdox_runtime_copy_text(
        runtime->snapshot.notice,
        sizeof(runtime->snapshot.notice),
        "Runtime is busy; try that action again"
    );
    return false;
}

static bool enqueue_simple_request(
    gdox_runtime *runtime,
    gdox_runtime_request_kind kind
)
{
    const gdox_runtime_request_entry request = {.kind = kind};
    return enqueue_request(runtime, &request);
}

static bool persist_preferences(
    gdox_runtime *runtime,
    const gdox_preferences *preferences
)
{
    gdox_error error;

    if (gdox_preferences_save(preferences, &error)) {
        return true;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        (void)snprintf(
            runtime->snapshot.notice,
            sizeof(runtime->snapshot.notice),
            "Could not save settings: %.132s",
            error.message
        );
        gdox_mutex_unlock(&runtime->mutex);
    }
    return false;
}

static bool import_firmware(
    gdox_runtime *runtime,
    const char *path,
    bool detect_kind,
    gdox_firmware_kind requested_kind
)
{
    gdox_runtime_bundle_status bundle;
    gdox_firmware_kind kind = requested_kind;
    gdox_error error;
    char executable_override[GDOX_EMULATOR_PATH_CAPACITY];
    bool imported;

    if (runtime == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    gdox_runtime_copy_text(
        executable_override,
        sizeof(executable_override),
        runtime->snapshot.settings.xemu_override
    );
    gdox_mutex_unlock(&runtime->mutex);
    imported = detect_kind
        ? gdox_runtime_bundle_import_firmware_auto(
              path, executable_override, &kind, &bundle, &error
          )
        : gdox_runtime_bundle_import_firmware(
              requested_kind,
              path,
              executable_override,
              &bundle,
              &error
          );
    if (!imported) {
        if (gdox_mutex_lock(&runtime->mutex)) {
            (void)snprintf(
                runtime->snapshot.notice,
                sizeof(runtime->snapshot.notice),
                "Firmware import: %.140s",
                error.message
            );
            gdox_mutex_unlock(&runtime->mutex);
        }
        return false;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        runtime->bundle = bundle;
        gdox_runtime_copy_bundle_status(&runtime->snapshot, &bundle);
        gdox_runtime_describe_bundle(&runtime->snapshot, &bundle);
        gdox_runtime_copy_text(
            runtime->snapshot.notice,
            sizeof(runtime->snapshot.notice),
            kind == GDOX_FIRMWARE_MCPX ? "MCPX boot ROM imported"
                                       : "Xbox BIOS imported"
        );
        gdox_mutex_unlock(&runtime->mutex);
    }
    return true;
}

bool gdox_runtime_import_firmware(gdox_runtime *runtime, const char *path)
{
    return import_firmware(runtime, path, true, GDOX_FIRMWARE_MCPX);
}

bool gdox_runtime_import_mcpx(gdox_runtime *runtime, const char *path)
{
    return import_firmware(runtime, path, false, GDOX_FIRMWARE_MCPX);
}

bool gdox_runtime_import_bios(gdox_runtime *runtime, const char *path)
{
    return import_firmware(runtime, path, false, GDOX_FIRMWARE_FLASH);
}

gdox_runtime_destroy_result gdox_runtime_destroy(
    gdox_runtime *runtime,
    gdox_error *error
)
{
    gdox_error cleanup_error;
    gdox_error terminal_error;
    gdox_runtime_playback_owner cleanup_owner;
    bool terminal_failure;

    gdox_error_clear(error);
    if (runtime == NULL) {
        return GDOX_RUNTIME_DESTROYED;
    }
    atomic_store_explicit(&runtime->stopping, true, memory_order_release);
    atomic_store_explicit(
        &runtime->preservation_cancelled, true, memory_order_release
    );
    if (runtime->thread_started) {
        if (!gdox_thread_join(&runtime->thread)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "runtime worker could not be joined; ownership retained"
            );
            return GDOX_RUNTIME_DESTROY_RETRY;
        }
        runtime->thread_started = false;
    }
    terminal_failure = runtime->terminal_shutdown_failed;
    terminal_error = runtime->terminal_shutdown_error;
    cleanup_owner = runtime->playback_owner;
    if (!gdox_runtime_playback_shutdown(runtime, &cleanup_error)
        && !gdox_runtime_playback_running(runtime)
        && (cleanup_owner == GDOX_RUNTIME_PLAYBACK_XEMU
            || (cleanup_owner == GDOX_RUNTIME_PLAYBACK_XENIA
                && !runtime->xenia_storage.session.active))) {
        terminal_failure = true;
        terminal_error = cleanup_error;
        runtime->terminal_shutdown_failed = true;
        runtime->terminal_shutdown_error = cleanup_error;
    }
    if (gdox_runtime_playback_running(runtime)) {
        if (gdox_error_is_set(&cleanup_error)) {
            *error = cleanup_error;
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "playback cleanup is incomplete; ownership retained"
            );
        }
        return GDOX_RUNTIME_DESTROY_RETRY;
    }
    if (!gdox_runtime_xenia_cleanup(runtime, &cleanup_error)) {
        *error = cleanup_error;
        return GDOX_RUNTIME_DESTROY_RETRY;
    }
    if (gdox_runtime_media_is_owned(&runtime->media)) {
        if (!gdox_runtime_media_close(&runtime->media, &cleanup_error)
            && gdox_runtime_media_is_owned(&runtime->media)) {
            if (gdox_error_is_set(&cleanup_error)) {
                *error = cleanup_error;
            } else {
                gdox_error_set(
                    error,
                    GDOX_ERROR_IO,
                    "media cleanup is incomplete; ownership retained"
                );
            }
            return GDOX_RUNTIME_DESTROY_RETRY;
        }
    }
    gdox_mutex_destroy(&runtime->mutex);
    free(runtime);
    if (terminal_failure) {
        if (gdox_error_is_set(&terminal_error)) {
            *error = terminal_error;
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "playback ended without a verified save checkpoint"
            );
        }
        return GDOX_RUNTIME_DESTROYED_WITH_ERROR;
    }
    return GDOX_RUNTIME_DESTROYED;
}

void gdox_runtime_copy_snapshot(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    if (runtime == NULL || snapshot == NULL) {
        return;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        *snapshot = runtime->snapshot;
        gdox_mutex_unlock(&runtime->mutex);
    }
}

void gdox_runtime_set_auto_start(gdox_runtime *runtime, bool enabled)
{
    gdox_preferences preferences;

    if (runtime != NULL && gdox_mutex_lock(&runtime->mutex)) {
        runtime->snapshot.settings.auto_start = enabled;
        gdox_runtime_preferences_from_snapshot(
            &runtime->snapshot, &preferences
        );
        gdox_mutex_unlock(&runtime->mutex);
        (void)persist_preferences(runtime, &preferences);
    }
}

void gdox_runtime_request(gdox_runtime *runtime, gdox_runtime_command command)
{
    gdox_runtime_request_kind kind = GDOX_RUNTIME_REQUEST_NONE;
    if (runtime == NULL) {
        return;
    }
    switch (command) {
        case GDOX_RUNTIME_START:
            kind = GDOX_RUNTIME_REQUEST_START;
            break;
        case GDOX_RUNTIME_RESTART:
            kind = GDOX_RUNTIME_REQUEST_RESTART;
            break;
        case GDOX_RUNTIME_CLOSE:
            kind = GDOX_RUNTIME_REQUEST_CLOSE;
            break;
        case GDOX_RUNTIME_EJECT:
            kind = GDOX_RUNTIME_REQUEST_EJECT;
            break;
        case GDOX_RUNTIME_CANCEL_PRESERVATION:
            atomic_store_explicit(
                &runtime->preservation_cancelled, true, memory_order_release
            );
            return;
        case GDOX_RUNTIME_USE_PHYSICAL_DISC:
            kind = GDOX_RUNTIME_REQUEST_USE_PHYSICAL;
            break;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        (void)enqueue_simple_request(runtime, kind);
        gdox_mutex_unlock(&runtime->mutex);
    }
}

bool gdox_runtime_open_disc_image(gdox_runtime *runtime, const char *path)
{
    gdox_runtime_request_entry request = {
        .kind = GDOX_RUNTIME_REQUEST_OPEN_IMAGE,
    };
    size_t path_bytes;
    bool accepted = false;

    if (runtime == NULL || path == NULL) {
        return false;
    }
    path_bytes = strlen(path);
    if (path_bytes == 0U || path_bytes >= sizeof(request.path)) {
        return false;
    }
    memcpy(request.path, path, path_bytes + 1U);
    if (gdox_mutex_lock(&runtime->mutex)) {
        if (runtime->snapshot.phase != GDOX_RUNTIME_PRESERVING) {
            accepted = enqueue_request(runtime, &request);
        }
        gdox_mutex_unlock(&runtime->mutex);
    }
    return accepted;
}

bool gdox_runtime_begin_preservation(
    gdox_runtime *runtime,
    gdox_preservation_format format,
    const char *output_path,
    bool verify
)
{
    gdox_runtime_request_entry request = {
        .kind = GDOX_RUNTIME_REQUEST_PRESERVE,
        .preservation_format = format,
        .preservation_verify = verify,
    };
    size_t path_bytes;
    bool accepted = false;

    if (runtime == NULL || output_path == NULL
        || (format != GDOX_PRESERVATION_XISO_COMPACT
            && format != GDOX_PRESERVATION_REDUMP)) {
        return false;
    }
    path_bytes = strlen(output_path);
    if (path_bytes == 0U || path_bytes >= sizeof(request.path)) {
        return false;
    }
    memcpy(request.path, output_path, path_bytes + 1U);
    if (gdox_mutex_lock(&runtime->mutex)) {
        if (runtime->snapshot.can_preserve
            && runtime->snapshot.phase != GDOX_RUNTIME_PRESERVING) {
            accepted = enqueue_request(runtime, &request);
            if (accepted) {
                atomic_store_explicit(
                    &runtime->preservation_cancelled,
                    false,
                    memory_order_release
                );
                runtime->snapshot.can_preserve = false;
                runtime->snapshot.can_cancel_preservation = true;
            }
        }
        gdox_mutex_unlock(&runtime->mutex);
    }
    return accepted;
}

void gdox_runtime_set_display(
    gdox_runtime *runtime,
    uint8_t internal_resolution_scale,
    gdox_emulator_aspect aspect,
    gdox_emulator_fit fit,
    bool fullscreen,
    uint16_t window_width,
    uint16_t window_height
)
{
    gdox_preferences preferences;
    uint8_t effective_scale;

    if (runtime == NULL || internal_resolution_scale < 1U
        || internal_resolution_scale > 10U
        || (aspect != GDOX_EMULATOR_ASPECT_AUTOMATIC
            && aspect != GDOX_EMULATOR_ASPECT_WIDESCREEN
            && aspect != GDOX_EMULATOR_ASPECT_FOUR_THREE
            && aspect != GDOX_EMULATOR_ASPECT_NATIVE)
        || (fit != GDOX_EMULATOR_FIT_CENTER && fit != GDOX_EMULATOR_FIT_SCALE
            && fit != GDOX_EMULATOR_FIT_STRETCH)
        || window_width < 640U || window_width > 7680U || window_height < 480U
        || window_height > 4320U) {
        return;
    }
    effective_scale = gdox_xemu_effective_resolution_scale(
        runtime->host_profile, internal_resolution_scale
    );
    if (gdox_mutex_lock(&runtime->mutex)) {
        runtime->snapshot.settings.internal_resolution_scale =
            effective_scale;
        runtime->snapshot.settings.display_aspect = aspect;
        runtime->snapshot.settings.display_fit = fit;
        runtime->snapshot.settings.fullscreen = fullscreen;
        runtime->snapshot.settings.window_width = window_width;
        runtime->snapshot.settings.window_height = window_height;
        (void)enqueue_simple_request(
            runtime, GDOX_RUNTIME_REQUEST_APPLY_DISPLAY
        );
        gdox_runtime_preferences_from_snapshot(
            &runtime->snapshot, &preferences
        );
        gdox_mutex_unlock(&runtime->mutex);
        (void)persist_preferences(runtime, &preferences);
    }
}

bool gdox_runtime_set_xemu_override(gdox_runtime *runtime, const char *path)
{
    gdox_runtime_bundle_status bundle;
    gdox_preferences preferences;
    gdox_error error;
    const char *selected = path != NULL ? path : "";

    gdox_error_clear(&error);
    if (runtime == NULL) {
        return false;
    }
    if (strlen(selected) >= GDOX_EMULATOR_PATH_CAPACITY
        || strchr(selected, '\n') != NULL || strchr(selected, '\r') != NULL
        || !gdox_runtime_bundle_prepare(
            selected, &bundle, &error
        )) {
        if (error.code == GDOX_ERROR_NONE) {
            gdox_error_set(
                &error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "selected xemu path is invalid"
            );
        }
        if (gdox_mutex_lock(&runtime->mutex)) {
            (void)snprintf(
                runtime->snapshot.notice,
                sizeof(runtime->snapshot.notice),
                "xemu selection: %.140s",
                error.message
            );
            gdox_mutex_unlock(&runtime->mutex);
        }
        return false;
    }
    if (!bundle.xemu_available || !bundle.configuration_ready) {
        gdox_error_set(
            &error, GDOX_ERROR_NOT_FOUND, "selected xemu could not be prepared"
        );
        if (gdox_mutex_lock(&runtime->mutex)) {
            gdox_runtime_copy_text(
                runtime->snapshot.notice,
                sizeof(runtime->snapshot.notice),
                error.message
            );
            gdox_mutex_unlock(&runtime->mutex);
        }
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    runtime->bundle = bundle;
    gdox_runtime_copy_text(
        runtime->snapshot.settings.xemu_override,
        sizeof(runtime->snapshot.settings.xemu_override),
        selected
    );
    gdox_runtime_copy_bundle_status(&runtime->snapshot, &bundle);
    gdox_runtime_describe_bundle(&runtime->snapshot, &bundle);
    if (runtime->snapshot.can_preserve) {
        runtime->snapshot.can_start =
            !runtime->snapshot.can_close && runtime->snapshot.xemu_ready;
        runtime->snapshot.can_restart = runtime->snapshot.xemu_ready;
    }
    gdox_runtime_copy_text(
        runtime->snapshot.notice,
        sizeof(runtime->snapshot.notice),
        selected[0] == '\0' ? "Using the xemu included with GDOX"
                            : "Using your selected xemu"
    );
    (void)enqueue_simple_request(runtime, GDOX_RUNTIME_REQUEST_APPLY_DISPLAY);
    gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
    gdox_mutex_unlock(&runtime->mutex);
    return persist_preferences(runtime, &preferences);
}

bool gdox_runtime_set_preservation_directory(
    gdox_runtime *runtime,
    const char *path
)
{
    gdox_preferences preferences;
    gdox_error error;

    if (runtime == NULL || path == NULL || path[0] == '\0'
        || strlen(path) >= GDOX_EMULATOR_PATH_CAPACITY
        || strchr(path, '\n') != NULL || strchr(path, '\r') != NULL
        || !gdox_storage_ensure_directory(path, &error)
        || !gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    gdox_runtime_copy_text(
        runtime->snapshot.settings.preservation_directory,
        sizeof(runtime->snapshot.settings.preservation_directory),
        path
    );
    gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
    gdox_mutex_unlock(&runtime->mutex);
    return persist_preferences(runtime, &preferences);
}
