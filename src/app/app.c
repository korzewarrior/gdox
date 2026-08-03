#include "app/app.h"
#include "app/runtime.h"

#include <stdio.h>
#include <string.h>

static void gdox_app_copy(char *output, size_t capacity, const char *value)
{
    const char *source = value != NULL ? value : "";
    size_t bytes;

    if (output == NULL || capacity == 0U) {
        return;
    }
    bytes = strlen(source);
    if (bytes >= capacity) {
        bytes = capacity - 1U;
    }
    memcpy(output, source, bytes);
    output[bytes] = '\0';
}

void gdox_app_initialize(gdox_app *app, gdox_host_profile host_profile)
{
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof(*app));
    app->host_profile = host_profile;
    app->runtime = gdox_runtime_create(host_profile);
    app->snapshot.page = GDOX_APP_PAGE_PLAY;
    app->snapshot.phase = GDOX_APP_DISCOVERING;
    app->snapshot.settings.auto_start = true;
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
    const gdox_app_page page =
        app != NULL ? app->snapshot.page : GDOX_APP_PAGE_PLAY;

    if (app == NULL || app->runtime == NULL) {
        return;
    }
    gdox_runtime_copy_snapshot(app->runtime, &app->snapshot);
    app->snapshot.page = page;
}
bool gdox_app_shutdown(gdox_app *app, gdox_error *error)
{
    gdox_runtime_destroy_result result;

    gdox_error_clear(error);
    if (app == NULL) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "application is required"
        );
        return false;
    }
    if (app->runtime == NULL) {
        return true;
    }
    result = gdox_runtime_destroy(app->runtime, error);
    if (result == GDOX_RUNTIME_DESTROYED) {
        app->runtime = NULL;
        return true;
    }
    if (result == GDOX_RUNTIME_DESTROYED_WITH_ERROR) {
        app->runtime = NULL;
        app->snapshot.phase = GDOX_APP_ATTENTION;
        gdox_app_copy(
            app->snapshot.status,
            sizeof(app->snapshot.status),
            "GDOX closed with a save error"
        );
        gdox_app_copy(
            app->snapshot.notice,
            sizeof(app->snapshot.notice),
            error->message
        );
    }
    return false;
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
        app->snapshot.settings.auto_start = enabled;
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
