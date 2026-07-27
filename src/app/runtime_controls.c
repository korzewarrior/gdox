#include "app/runtime_internal.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char hdd_override[GDOX_EMULATOR_PATH_CAPACITY];
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
        runtime->snapshot.xemu_override
    );
    gdox_runtime_copy_text(
        hdd_override,
        sizeof(hdd_override),
        runtime->snapshot.hdd_override
    );
    gdox_mutex_unlock(&runtime->mutex);
    imported = detect_kind
        ? gdox_runtime_bundle_import_firmware_auto(
            path,
            executable_override,
            hdd_override,
            &kind,
            &bundle,
            &error
        )
        : gdox_runtime_bundle_import_firmware(
            requested_kind,
            path,
            executable_override,
            hdd_override,
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
            kind == GDOX_FIRMWARE_MCPX
                ? "MCPX boot ROM imported"
                : "Xbox BIOS imported"
        );
        gdox_mutex_unlock(&runtime->mutex);
    }
    return true;
}

bool gdox_runtime_import_firmware(
    gdox_runtime *runtime,
    const char *path
)
{
    return import_firmware(runtime, path, true, GDOX_FIRMWARE_MCPX);
}

bool gdox_runtime_import_mcpx(
    gdox_runtime *runtime,
    const char *path
)
{
    return import_firmware(runtime, path, false, GDOX_FIRMWARE_MCPX);
}

bool gdox_runtime_import_bios(
    gdox_runtime *runtime,
    const char *path
)
{
    return import_firmware(runtime, path, false, GDOX_FIRMWARE_FLASH);
}

void gdox_runtime_destroy(gdox_runtime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    atomic_store_explicit(&runtime->stopping, true, memory_order_release);
    atomic_store_explicit(
        &runtime->preservation_cancelled,
        true,
        memory_order_release
    );
    if (runtime->thread_started) {
        (void)gdox_thread_join(&runtime->thread);
    }
    gdox_mutex_destroy(&runtime->mutex);
    free(runtime);
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
    gdox_error ignored;

    if (runtime != NULL && gdox_mutex_lock(&runtime->mutex)) {
        runtime->snapshot.auto_start = enabled;
        gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
        gdox_mutex_unlock(&runtime->mutex);
        (void)gdox_preferences_save(&preferences, &ignored);
    }
}

void gdox_runtime_request(gdox_runtime *runtime, gdox_runtime_command command)
{
    uint32_t bit = 0U;
    if (runtime == NULL) {
        return;
    }
    switch (command) {
        case GDOX_RUNTIME_START:
            bit = GDOX_RUNTIME_COMMAND_START;
            break;
        case GDOX_RUNTIME_RESTART:
            bit = GDOX_RUNTIME_COMMAND_RESTART;
            break;
        case GDOX_RUNTIME_CLOSE:
            bit = GDOX_RUNTIME_COMMAND_CLOSE;
            break;
        case GDOX_RUNTIME_EJECT:
            bit = GDOX_RUNTIME_COMMAND_EJECT;
            break;
        case GDOX_RUNTIME_CANCEL_PRESERVATION:
            atomic_store_explicit(
                &runtime->preservation_cancelled,
                true,
                memory_order_release
            );
            return;
        case GDOX_RUNTIME_USE_PHYSICAL_DISC:
            bit = GDOX_RUNTIME_COMMAND_USE_PHYSICAL;
            break;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        runtime->commands |= bit;
        gdox_mutex_unlock(&runtime->mutex);
    }
}

bool gdox_runtime_open_disc_image(
    gdox_runtime *runtime,
    const char *path
)
{
    size_t path_bytes;
    bool accepted = false;

    if (runtime == NULL || path == NULL) {
        return false;
    }
    path_bytes = strlen(path);
    if (path_bytes == 0U
        || path_bytes >= sizeof(runtime->pending_image_path)) {
        return false;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        if (runtime->snapshot.phase != GDOX_RUNTIME_PRESERVING) {
            memcpy(runtime->pending_image_path, path, path_bytes + 1U);
            runtime->commands |= GDOX_RUNTIME_COMMAND_OPEN_IMAGE;
            accepted = true;
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
    size_t path_bytes;
    bool accepted = false;

    if (runtime == NULL || output_path == NULL
        || (format != GDOX_PRESERVATION_XISO_COMPACT
            && format != GDOX_PRESERVATION_REDUMP)) {
        return false;
    }
    path_bytes = strlen(output_path);
    if (path_bytes == 0U
        || path_bytes >= sizeof(runtime->pending_preservation_path)) {
        return false;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        if (runtime->snapshot.can_preserve
            && runtime->snapshot.phase != GDOX_RUNTIME_PRESERVING) {
            runtime->pending_preservation_format = format;
            runtime->pending_preservation_verify = verify;
            memcpy(
                runtime->pending_preservation_path,
                output_path,
                path_bytes + 1U
            );
            atomic_store_explicit(
                &runtime->preservation_cancelled,
                false,
                memory_order_release
            );
            runtime->commands |= GDOX_RUNTIME_COMMAND_PRESERVE;
            runtime->snapshot.can_preserve = false;
            runtime->snapshot.can_cancel_preservation = true;
            accepted = true;
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
    gdox_error ignored;

    if (runtime == NULL || internal_resolution_scale < 1U
        || internal_resolution_scale > 10U
        || (aspect != GDOX_EMULATOR_ASPECT_AUTOMATIC
            && aspect != GDOX_EMULATOR_ASPECT_WIDESCREEN
            && aspect != GDOX_EMULATOR_ASPECT_FOUR_THREE
            && aspect != GDOX_EMULATOR_ASPECT_NATIVE)
        || (fit != GDOX_EMULATOR_FIT_CENTER
            && fit != GDOX_EMULATOR_FIT_SCALE
            && fit != GDOX_EMULATOR_FIT_STRETCH)
        || window_width < 640U || window_width > 7680U
        || window_height < 480U || window_height > 4320U) {
        return;
    }
    if (gdox_mutex_lock(&runtime->mutex)) {
        runtime->snapshot.internal_resolution_scale =
            internal_resolution_scale;
        runtime->snapshot.display_aspect = aspect;
        runtime->snapshot.display_fit = fit;
        runtime->snapshot.fullscreen = fullscreen;
        runtime->snapshot.window_width = window_width;
        runtime->snapshot.window_height = window_height;
        runtime->commands |= GDOX_RUNTIME_COMMAND_APPLY_DISPLAY;
        gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
        gdox_mutex_unlock(&runtime->mutex);
        (void)gdox_preferences_save(&preferences, &ignored);
    }
}

bool gdox_runtime_set_xemu_override(
    gdox_runtime *runtime,
    const char *path
)
{
    gdox_runtime_bundle_status bundle;
    gdox_preferences preferences;
    gdox_error error;
    const char *selected = path != NULL ? path : "";
    char hdd_override[GDOX_EMULATOR_PATH_CAPACITY];

    gdox_error_clear(&error);
    if (runtime == NULL) {
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    gdox_runtime_copy_text(
        hdd_override,
        sizeof(hdd_override),
        runtime->snapshot.hdd_override
    );
    gdox_mutex_unlock(&runtime->mutex);
    if (strlen(selected) >= GDOX_EMULATOR_PATH_CAPACITY
        || strchr(selected, '\n') != NULL
        || strchr(selected, '\r') != NULL
        || !gdox_runtime_bundle_prepare(
            selected,
            hdd_override,
            &bundle,
            &error
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
            &error,
            GDOX_ERROR_NOT_FOUND,
            "selected xemu could not be prepared"
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
        runtime->snapshot.xemu_override,
        sizeof(runtime->snapshot.xemu_override),
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
        selected[0] == '\0'
            ? "Using the xemu included with GDOX"
            : "Using your selected xemu"
    );
    runtime->commands |= GDOX_RUNTIME_COMMAND_APPLY_DISPLAY;
    gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
    gdox_mutex_unlock(&runtime->mutex);
    return gdox_preferences_save(&preferences, &error);
}

bool gdox_runtime_set_hdd_override(
    gdox_runtime *runtime,
    const char *path
)
{
    gdox_runtime_bundle_status bundle;
    gdox_preferences preferences;
    gdox_error error;
    const char *selected = path != NULL ? path : "";
    char executable_override[GDOX_EMULATOR_PATH_CAPACITY];

    gdox_error_clear(&error);
    if (runtime == NULL) {
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    gdox_runtime_copy_text(
        executable_override,
        sizeof(executable_override),
        runtime->snapshot.xemu_override
    );
    gdox_mutex_unlock(&runtime->mutex);
    if (strlen(selected) >= GDOX_EMULATOR_PATH_CAPACITY
        || strchr(selected, '\n') != NULL
        || strchr(selected, '\r') != NULL
        || !gdox_runtime_bundle_prepare(
            executable_override,
            selected,
            &bundle,
            &error
        )) {
        if (error.code == GDOX_ERROR_NONE) {
            gdox_error_set(
                &error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "selected Xbox hard disk path is invalid"
            );
        }
        if (gdox_mutex_lock(&runtime->mutex)) {
            (void)snprintf(
                runtime->snapshot.notice,
                sizeof(runtime->snapshot.notice),
                "Hard disk selection: %.135s",
                error.message
            );
            gdox_mutex_unlock(&runtime->mutex);
        }
        return false;
    }
    if (!bundle.configuration_ready || !bundle.hdd_ready) {
        if (gdox_mutex_lock(&runtime->mutex)) {
            gdox_runtime_copy_text(
                runtime->snapshot.notice,
                sizeof(runtime->snapshot.notice),
                "Selected Xbox hard disk could not be prepared"
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
        runtime->snapshot.hdd_override,
        sizeof(runtime->snapshot.hdd_override),
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
        selected[0] == '\0'
            ? "Using the Xbox hard disk included with GDOX"
            : "Using your selected Xbox hard disk"
    );
    runtime->commands |= GDOX_RUNTIME_COMMAND_APPLY_DISPLAY;
    gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
    gdox_mutex_unlock(&runtime->mutex);
    return gdox_preferences_save(&preferences, &error);
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
        || strchr(path, '\n') != NULL
        || strchr(path, '\r') != NULL
        || !gdox_storage_ensure_directory(path, &error)
        || !gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    gdox_runtime_copy_text(
        runtime->snapshot.preservation_directory,
        sizeof(runtime->snapshot.preservation_directory),
        path
    );
    gdox_runtime_preferences_from_snapshot(&runtime->snapshot, &preferences);
    gdox_mutex_unlock(&runtime->mutex);
    return gdox_preferences_save(&preferences, &error);
}
