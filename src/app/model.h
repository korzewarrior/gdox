#ifndef GDOX_APP_MODEL_H
#define GDOX_APP_MODEL_H

#include "gdox/emulator.h"
#include "gdox/media.h"
#include "gdox/nbd.h"
#include "gdox/preserve.h"
#include "gdox/xenia_policy.h"

#include <stdbool.h>
#include <stdint.h>

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
    GDOX_APP_PREPARING,
    GDOX_APP_DISC_READY,
    GDOX_APP_PLAYING,
    GDOX_APP_PRESERVING,
    GDOX_APP_PRESERVED,
    GDOX_APP_ATTENTION
} gdox_app_phase;

typedef enum gdox_host_profile {
    GDOX_HOST_PROFILE_DESKTOP = 0,
    GDOX_HOST_PROFILE_HANDHELD
} gdox_host_profile;

/*
 * User-owned settings are one value throughout persistence, runtime, and UI.
 * Keeping them composed prevents worker publications from overwriting a
 * concurrent settings change and prevents new fields from being omitted from
 * a hand-maintained copy list.
 */
typedef struct gdox_app_settings {
    bool auto_start;
    uint8_t internal_resolution_scale;
    gdox_emulator_aspect display_aspect;
    gdox_emulator_fit display_fit;
    bool fullscreen;
    uint16_t window_width;
    uint16_t window_height;
    char xemu_override[GDOX_EMULATOR_PATH_CAPACITY];
    char preservation_directory[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_app_settings;

/*
 * The application and its worker exchange this one canonical value object.
 * Presentation owns `page`, the settings service owns `settings`, and the
 * worker publishes the remaining session state without flattening either
 * independently owned value into a fragile field-by-field mapping.
 */
typedef struct gdox_app_snapshot {
    gdox_app_page page;
    gdox_app_phase phase;
    gdox_app_settings settings;
    bool xemu_ready;
    bool xenia_ready;
    bool can_start;
    bool can_restart;
    bool can_close;
    bool can_eject;
    bool can_preserve;
    bool can_cancel_preservation;
    bool preservation_complete;
    bool bundled_xemu;
    bool bundled_xenia;
    bool mcpx_ready;
    bool flash_ready;
    bool hdd_ready;
    gdox_media_source media_source;
    gdox_media_platform media_platform;
    gdox_media_backend media_backend;
    gdox_media_image_layout image_layout;
    gdox_x360_image_layout x360_image_layout;
    gdox_x360_execution_info x360_execution;
    const gdox_xenia_launch_policy *xenia_policy;
    gdox_preservation_phase preservation_phase;
    uint64_t preservation_completed_bytes;
    uint64_t preservation_total_bytes;
    double preservation_bytes_per_second;
    uint64_t preservation_unreadable_sectors;
    uint64_t physical_read_commands;
    uint64_t physical_read_sectors;
    uint64_t physical_read_bytes;
    uint64_t physical_last_lba;
    gdox_nbd_read_stats nbd_read_stats;
    uint64_t image_source_sectors;
    uint64_t image_game_partition_lba;
    char drive[GDOX_APP_TEXT_CAPACITY];
    char disc[GDOX_APP_TEXT_CAPACITY];
    char status[GDOX_APP_TEXT_CAPACITY];
    char notice[GDOX_APP_TEXT_CAPACITY];
    char xemu_setup[GDOX_APP_TEXT_CAPACITY];
    char xenia_setup[GDOX_APP_TEXT_CAPACITY];
    char preservation_output[GDOX_EMULATOR_PATH_CAPACITY];
    char disc_image_path[GDOX_EMULATOR_PATH_CAPACITY];
    char xemu_executable[GDOX_EMULATOR_PATH_CAPACITY];
    char xenia_executable[GDOX_EMULATOR_PATH_CAPACITY];
    char xemu_configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char mcpx_path[GDOX_EMULATOR_PATH_CAPACITY];
    char flash_path[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd_path[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_app_snapshot;

#endif
