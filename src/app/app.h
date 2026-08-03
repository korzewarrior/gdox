#ifndef GDOX_APP_H
#define GDOX_APP_H

#include "app/model.h"

#include "gdox/error.h"
#include "gdox/session.h"
#include "gdox/preserve.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_app {
    gdox_app_snapshot snapshot;
    struct gdox_runtime *runtime;
    gdox_host_profile host_profile;
} gdox_app;

void gdox_app_initialize(gdox_app *app, gdox_host_profile host_profile);
void gdox_app_tick(gdox_app *app);
bool gdox_app_shutdown(gdox_app *app, gdox_error *error);
const gdox_app_snapshot *gdox_app_snapshot_get(const gdox_app *app);
void gdox_app_select_page(gdox_app *app, gdox_app_page page);
void gdox_app_set_auto_start(gdox_app *app, bool enabled);
void gdox_app_command(gdox_app *app, gdox_session_event event);
bool gdox_app_begin_preservation(
    gdox_app *app,
    gdox_preservation_format format,
    const char *output_path,
    bool verify
);
void gdox_app_cancel_preservation(gdox_app *app);
void gdox_app_set_display(
    gdox_app *app,
    uint8_t internal_resolution_scale,
    gdox_emulator_aspect aspect,
    gdox_emulator_fit fit,
    bool fullscreen,
    uint16_t window_width,
    uint16_t window_height
);
bool gdox_app_set_xemu_override(gdox_app *app, const char *path);
bool gdox_app_set_preservation_directory(gdox_app *app, const char *path);
bool gdox_app_open_disc_image(gdox_app *app, const char *path);
void gdox_app_use_physical_disc(gdox_app *app);
bool gdox_app_import_firmware(gdox_app *app, const char *path);
bool gdox_app_import_mcpx(gdox_app *app, const char *path);
bool gdox_app_import_bios(gdox_app *app, const char *path);

#ifdef __cplusplus
}
#endif

#endif
