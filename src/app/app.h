#ifndef GDOX_APP_H
#define GDOX_APP_H

#include "gdox/session.h"
#include "gdox/preserve.h"
#include "gdox/emulator.h"
#include "gdox/media.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_APP_TEXT_CAPACITY 160U

typedef enum gdox_app_page {
    GDOX_APP_PAGE_PLAY = 0,
    GDOX_APP_PAGE_PRESERVE,
    GDOX_APP_PAGE_DETAILS,
    GDOX_APP_PAGE_SETTINGS,
    GDOX_APP_PAGE_SOURCES
} gdox_app_page;

typedef enum gdox_app_phase {
    GDOX_APP_DISCOVERING = 0,
    GDOX_APP_EMPTY,
    GDOX_APP_DISC_READY,
    GDOX_APP_PLAYING,
    GDOX_APP_PRESERVING,
    GDOX_APP_PRESERVED,
    GDOX_APP_ATTENTION
} gdox_app_phase;

typedef struct gdox_app_snapshot {
    gdox_app_page page;
    gdox_app_phase phase;
    bool auto_start;
    bool xemu_ready;
    bool can_start;
    bool can_restart;
    bool can_close;
    bool can_eject;
    bool can_preserve;
    bool can_cancel_preservation;
    bool preservation_complete;
    bool bundled_xemu;
    bool mcpx_ready;
    bool flash_ready;
    bool hdd_ready;
    bool hdd_cache_reset;
    gdox_media_source media_source;
    gdox_media_image_layout image_layout;
    uint8_t internal_resolution_scale;
    gdox_emulator_aspect display_aspect;
    gdox_emulator_fit display_fit;
    bool fullscreen;
    uint16_t window_width;
    uint16_t window_height;
    gdox_preservation_phase preservation_phase;
    uint64_t preservation_completed_bytes;
    uint64_t preservation_total_bytes;
    double preservation_bytes_per_second;
    uint64_t preservation_unreadable_sectors;
    uint64_t physical_read_commands;
    uint64_t physical_read_sectors;
    uint64_t physical_read_bytes;
    uint64_t physical_last_lba;
    uint64_t image_source_sectors;
    uint64_t image_game_partition_lba;
    char drive[GDOX_APP_TEXT_CAPACITY];
    char disc[GDOX_APP_TEXT_CAPACITY];
    char status[GDOX_APP_TEXT_CAPACITY];
    char notice[GDOX_APP_TEXT_CAPACITY];
    char xemu_setup[GDOX_APP_TEXT_CAPACITY];
    char preservation_output[GDOX_EMULATOR_PATH_CAPACITY];
    char xemu_override[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd_override[GDOX_EMULATOR_PATH_CAPACITY];
    char preservation_directory[GDOX_EMULATOR_PATH_CAPACITY];
    char disc_image_path[GDOX_EMULATOR_PATH_CAPACITY];
    char xemu_executable[GDOX_EMULATOR_PATH_CAPACITY];
    char xemu_configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char mcpx_path[GDOX_EMULATOR_PATH_CAPACITY];
    char flash_path[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd_path[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_app_snapshot;

typedef struct gdox_app {
    gdox_app_snapshot snapshot;
    struct gdox_runtime *runtime;
} gdox_app;

void gdox_app_initialize(gdox_app *app);
void gdox_app_tick(gdox_app *app);
void gdox_app_shutdown(gdox_app *app);
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
bool gdox_app_set_hdd_override(gdox_app *app, const char *path);
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
