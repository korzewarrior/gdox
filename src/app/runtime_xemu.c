#include "app/runtime_xemu.h"
#include "app/xemu_performance.h"
#include "app/xemu_save_storage.h"

static bool read_launch_state(
    gdox_runtime *runtime,
    gdox_runtime_bundle_status *bundle,
    gdox_app_settings *settings,
    gdox_error *error
)
{
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(
            error, GDOX_ERROR_INTERNAL, "could not read xemu launch state"
        );
        return false;
    }
    *bundle = runtime->bundle;
    if (settings != NULL) {
        *settings = runtime->snapshot.settings;
    }
    gdox_mutex_unlock(&runtime->mutex);
    if (!gdox_runtime_bundle_complete(bundle)) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "xemu setup needs a valid MCPX boot ROM, BIOS, and hard-disk image"
        );
        return false;
    }
    return true;
}

bool gdox_runtime_xemu_prepare_launch(
    gdox_runtime *runtime,
    gdox_error *error
)
{
    gdox_runtime_bundle_status bundle;

    gdox_error_clear(error);
    if (runtime == NULL) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "runtime is required"
        );
        return false;
    }
    if (!read_launch_state(runtime, &bundle, NULL, error)) {
        return false;
    }
    if (runtime->xemu_save_migration.retained_due_to_rejected_migration) {
        return true;
    }
    return gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        bundle.executable,
        bundle.hdd,
        &runtime->xemu_save_migration,
        error
    );
}

bool gdox_runtime_xemu_start(gdox_runtime *runtime, gdox_error *error)
{
    gdox_runtime_bundle_status bundle;
    gdox_app_settings settings;
    gdox_emulator_options options;
    char save_vault[GDOX_STORAGE_PATH_CAPACITY];

    gdox_error_clear(error);
    if (runtime == NULL || runtime->xemu != NULL || !runtime->media.open
        || runtime->media.info.backend != GDOX_MEDIA_BACKEND_XEMU
        || runtime->media.exported == NULL
        || !read_launch_state(runtime, &bundle, &settings, error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "a ready Original Xbox media session is required"
            );
        }
        return false;
    }
    if (!gdox_xemu_save_vault_prepare(save_vault, error)) {
        return false;
    }
    options = (gdox_emulator_options){
        .executable = bundle.executable,
        .configuration = bundle.configuration,
        .save_vault = save_vault,
        .internal_resolution_scale = gdox_xemu_effective_resolution_scale(
            runtime->host_profile, settings.internal_resolution_scale
        ),
        .aspect = settings.display_aspect,
        .fit = settings.display_fit,
        .fullscreen = settings.fullscreen,
        .console_output = false,
        .window_width = settings.window_width,
        .window_height = settings.window_height,
    };
    return gdox_emulator_launch(
        &options,
        gdox_nbd_uri(runtime->media.exported),
        &runtime->xemu,
        error
    );
}

bool gdox_runtime_xemu_stop(gdox_runtime *runtime, gdox_error *error)
{
    if (runtime == NULL) {
        gdox_error_clear(error);
        return true;
    }
    return gdox_xemu_process_stop_orderly(&runtime->xemu, error);
}

bool gdox_runtime_xemu_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    if (runtime == NULL || runtime->xemu == NULL) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "xemu process is not running"
        );
        return false;
    }
    if (!gdox_emulator_poll(runtime->xemu, running, exit_code, error)) {
        return false;
    }
    if (!*running) {
        gdox_emulator_process_destroy(runtime->xemu);
        runtime->xemu = NULL;
    }
    return true;
}
