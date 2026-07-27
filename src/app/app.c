#include "app/app.h"
#include "app/runtime.h"

#include <stdio.h>
#include <string.h>

static void gdox_app_copy(char *output, size_t capacity, const char *value)
{
    if (output == NULL || capacity == 0U) {
        return;
    }
    (void)snprintf(output, capacity, "%s", value != NULL ? value : "");
}

void gdox_app_initialize(gdox_app *app)
{
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof(*app));
    app->runtime = gdox_runtime_create();
    app->snapshot.page = GDOX_APP_PAGE_PLAY;
    app->snapshot.phase = GDOX_APP_DISCOVERING;
    app->snapshot.auto_start = true;
    gdox_app_copy(app->snapshot.drive, sizeof(app->snapshot.drive), "Checking optical drive");
    gdox_app_copy(app->snapshot.disc, sizeof(app->snapshot.disc), "No Xbox disc");
    gdox_app_copy(app->snapshot.status, sizeof(app->snapshot.status), "Starting GDOX");
    gdox_app_copy(app->snapshot.notice, sizeof(app->snapshot.notice), "Native runtime is initializing");
    if (app->runtime == NULL) {
        app->snapshot.phase = GDOX_APP_ATTENTION;
        gdox_app_copy(app->snapshot.status, sizeof(app->snapshot.status), "GDOX needs attention");
        gdox_app_copy(app->snapshot.notice, sizeof(app->snapshot.notice), "Native runtime could not start");
    }
}

void gdox_app_tick(gdox_app *app)
{
    gdox_runtime_snapshot runtime;
    const gdox_app_page page = app != NULL ? app->snapshot.page : GDOX_APP_PAGE_PLAY;

    if (app == NULL || app->runtime == NULL) {
        return;
    }
    gdox_runtime_copy_snapshot(app->runtime, &runtime);
    switch (runtime.phase) {
        case GDOX_RUNTIME_DISCOVERING:
        case GDOX_RUNTIME_PREPARING:
            app->snapshot.phase = GDOX_APP_DISCOVERING;
            break;
        case GDOX_RUNTIME_EMPTY:
            app->snapshot.phase = GDOX_APP_EMPTY;
            break;
        case GDOX_RUNTIME_READY:
            app->snapshot.phase = GDOX_APP_DISC_READY;
            break;
        case GDOX_RUNTIME_PLAYING:
            app->snapshot.phase = GDOX_APP_PLAYING;
            break;
        case GDOX_RUNTIME_PRESERVING:
            app->snapshot.phase = GDOX_APP_PRESERVING;
            break;
        case GDOX_RUNTIME_PRESERVED:
            app->snapshot.phase = GDOX_APP_PRESERVED;
            break;
        case GDOX_RUNTIME_ATTENTION:
            app->snapshot.phase = GDOX_APP_ATTENTION;
            break;
    }
    app->snapshot.page = page;
    app->snapshot.auto_start = runtime.auto_start;
    app->snapshot.xemu_ready = runtime.xemu_ready;
    app->snapshot.can_start = runtime.can_start;
    app->snapshot.can_restart = runtime.can_restart;
    app->snapshot.can_close = runtime.can_close;
    app->snapshot.can_eject = runtime.can_eject;
    app->snapshot.can_preserve = runtime.can_preserve;
    app->snapshot.can_cancel_preservation = runtime.can_cancel_preservation;
    app->snapshot.preservation_complete = runtime.preservation_complete;
    app->snapshot.bundled_xemu = runtime.bundled_xemu;
    app->snapshot.mcpx_ready = runtime.mcpx_ready;
    app->snapshot.flash_ready = runtime.flash_ready;
    app->snapshot.hdd_ready = runtime.hdd_ready;
    app->snapshot.hdd_cache_reset = runtime.hdd_cache_reset;
    app->snapshot.media_source = runtime.media_source;
    app->snapshot.image_layout = runtime.image_layout;
    app->snapshot.internal_resolution_scale =
        runtime.internal_resolution_scale;
    app->snapshot.display_aspect = runtime.display_aspect;
    app->snapshot.display_fit = runtime.display_fit;
    app->snapshot.fullscreen = runtime.fullscreen;
    app->snapshot.window_width = runtime.window_width;
    app->snapshot.window_height = runtime.window_height;
    app->snapshot.preservation_phase = runtime.preservation_phase;
    app->snapshot.preservation_completed_bytes =
        runtime.preservation_completed_bytes;
    app->snapshot.preservation_total_bytes =
        runtime.preservation_total_bytes;
    app->snapshot.preservation_bytes_per_second =
        runtime.preservation_bytes_per_second;
    app->snapshot.preservation_unreadable_sectors =
        runtime.preservation_unreadable_sectors;
    app->snapshot.physical_read_commands = runtime.physical_read_commands;
    app->snapshot.physical_read_sectors = runtime.physical_read_sectors;
    app->snapshot.physical_read_bytes = runtime.physical_read_bytes;
    app->snapshot.physical_last_lba = runtime.physical_last_lba;
    app->snapshot.image_source_sectors = runtime.image_source_sectors;
    app->snapshot.image_game_partition_lba =
        runtime.image_game_partition_lba;
    gdox_app_copy(app->snapshot.drive, sizeof(app->snapshot.drive), runtime.drive);
    gdox_app_copy(app->snapshot.disc, sizeof(app->snapshot.disc), runtime.disc);
    gdox_app_copy(app->snapshot.status, sizeof(app->snapshot.status), runtime.status);
    gdox_app_copy(app->snapshot.notice, sizeof(app->snapshot.notice), runtime.notice);
    gdox_app_copy(
        app->snapshot.xemu_setup,
        sizeof(app->snapshot.xemu_setup),
        runtime.xemu_setup
    );
    gdox_app_copy(
        app->snapshot.preservation_output,
        sizeof(app->snapshot.preservation_output),
        runtime.preservation_output
    );
    gdox_app_copy(
        app->snapshot.xemu_override,
        sizeof(app->snapshot.xemu_override),
        runtime.xemu_override
    );
    gdox_app_copy(
        app->snapshot.hdd_override,
        sizeof(app->snapshot.hdd_override),
        runtime.hdd_override
    );
    gdox_app_copy(
        app->snapshot.preservation_directory,
        sizeof(app->snapshot.preservation_directory),
        runtime.preservation_directory
    );
    gdox_app_copy(
        app->snapshot.disc_image_path,
        sizeof(app->snapshot.disc_image_path),
        runtime.disc_image_path
    );
    gdox_app_copy(
        app->snapshot.xemu_executable,
        sizeof(app->snapshot.xemu_executable),
        runtime.xemu_executable
    );
    gdox_app_copy(
        app->snapshot.xemu_configuration,
        sizeof(app->snapshot.xemu_configuration),
        runtime.xemu_configuration
    );
    gdox_app_copy(
        app->snapshot.mcpx_path,
        sizeof(app->snapshot.mcpx_path),
        runtime.mcpx_path
    );
    gdox_app_copy(
        app->snapshot.flash_path,
        sizeof(app->snapshot.flash_path),
        runtime.flash_path
    );
    gdox_app_copy(
        app->snapshot.hdd_path,
        sizeof(app->snapshot.hdd_path),
        runtime.hdd_path
    );
}

void gdox_app_shutdown(gdox_app *app)
{
    if (app != NULL) {
        gdox_runtime_destroy(app->runtime);
        app->runtime = NULL;
    }
}

const gdox_app_snapshot *gdox_app_snapshot_get(const gdox_app *app)
{
    return app != NULL ? &app->snapshot : NULL;
}

void gdox_app_select_page(gdox_app *app, gdox_app_page page)
{
    if (app != NULL
        && page >= GDOX_APP_PAGE_PLAY
        && page <= GDOX_APP_PAGE_SOURCES) {
        app->snapshot.page = page;
    }
}

void gdox_app_set_auto_start(gdox_app *app, bool enabled)
{
    if (app != NULL) {
        app->snapshot.auto_start = enabled;
        gdox_runtime_set_auto_start(app->runtime, enabled);
    }
}

void gdox_app_command(gdox_app *app, gdox_session_event event)
{
    if (app == NULL || app->runtime == NULL) {
        return;
    }
    switch (event) {
        case GDOX_SESSION_LAUNCH_REQUESTED:
            gdox_runtime_request(app->runtime, GDOX_RUNTIME_START);
            break;
        case GDOX_SESSION_RESTART_REQUESTED:
            gdox_runtime_request(app->runtime, GDOX_RUNTIME_RESTART);
            break;
        case GDOX_SESSION_CLOSE_REQUESTED:
            gdox_runtime_request(app->runtime, GDOX_RUNTIME_CLOSE);
            break;
        case GDOX_SESSION_EJECT_REQUESTED:
            gdox_runtime_request(app->runtime, GDOX_RUNTIME_EJECT);
            break;
        case GDOX_SESSION_EMULATOR_EXITED:
        case GDOX_SESSION_CANCELLED:
        case GDOX_SESSION_MEDIA_REMOVED:
        case GDOX_SESSION_EXPORT_FAILED:
            break;
    }
}

bool gdox_app_begin_preservation(
    gdox_app *app,
    gdox_preservation_format format,
    const char *output_path,
    bool verify
)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_begin_preservation(
            app->runtime,
            format,
            output_path,
            verify
        );
}

void gdox_app_cancel_preservation(gdox_app *app)
{
    if (app != NULL && app->runtime != NULL) {
        gdox_runtime_request(
            app->runtime,
            GDOX_RUNTIME_CANCEL_PRESERVATION
        );
    }
}

bool gdox_app_open_disc_image(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_open_disc_image(app->runtime, path);
}

void gdox_app_use_physical_disc(gdox_app *app)
{
    if (app != NULL && app->runtime != NULL) {
        gdox_runtime_request(
            app->runtime,
            GDOX_RUNTIME_USE_PHYSICAL_DISC
        );
    }
}

void gdox_app_set_display(
    gdox_app *app,
    uint8_t internal_resolution_scale,
    gdox_emulator_aspect aspect,
    gdox_emulator_fit fit,
    bool fullscreen,
    uint16_t window_width,
    uint16_t window_height
)
{
    if (app != NULL && app->runtime != NULL) {
        gdox_runtime_set_display(
            app->runtime,
            internal_resolution_scale,
            aspect,
            fit,
            fullscreen,
            window_width,
            window_height
        );
    }
}

bool gdox_app_set_xemu_override(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_set_xemu_override(app->runtime, path);
}

bool gdox_app_set_hdd_override(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_set_hdd_override(app->runtime, path);
}

bool gdox_app_set_preservation_directory(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_set_preservation_directory(app->runtime, path);
}

bool gdox_app_import_firmware(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_import_firmware(app->runtime, path);
}

bool gdox_app_import_mcpx(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_import_mcpx(app->runtime, path);
}

bool gdox_app_import_bios(gdox_app *app, const char *path)
{
    return app != NULL && app->runtime != NULL
        && gdox_runtime_import_bios(app->runtime, path);
}
