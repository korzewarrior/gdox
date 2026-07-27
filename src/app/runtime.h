#ifndef GDOX_APP_RUNTIME_H
#define GDOX_APP_RUNTIME_H

#include "gdox/preserve.h"
#include "gdox/emulator.h"
#include "gdox/media.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum gdox_runtime_phase {
    GDOX_RUNTIME_DISCOVERING = 0,
    GDOX_RUNTIME_EMPTY,
    GDOX_RUNTIME_PREPARING,
    GDOX_RUNTIME_READY,
    GDOX_RUNTIME_PLAYING,
    GDOX_RUNTIME_PRESERVING,
    GDOX_RUNTIME_PRESERVED,
    GDOX_RUNTIME_ATTENTION
} gdox_runtime_phase;

typedef enum gdox_runtime_command {
    GDOX_RUNTIME_START = 0,
    GDOX_RUNTIME_RESTART,
    GDOX_RUNTIME_CLOSE,
    GDOX_RUNTIME_EJECT,
    GDOX_RUNTIME_CANCEL_PRESERVATION,
    GDOX_RUNTIME_USE_PHYSICAL_DISC
} gdox_runtime_command;

typedef struct gdox_runtime_snapshot {
    gdox_runtime_phase phase;
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
    char drive[160];
    char disc[160];
    char status[160];
    char notice[160];
    char xemu_setup[160];
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
} gdox_runtime_snapshot;

typedef struct gdox_runtime gdox_runtime;

gdox_runtime *gdox_runtime_create(void);
void gdox_runtime_destroy(gdox_runtime *runtime);
void gdox_runtime_copy_snapshot(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
);
void gdox_runtime_set_auto_start(gdox_runtime *runtime, bool enabled);
void gdox_runtime_request(gdox_runtime *runtime, gdox_runtime_command command);
bool gdox_runtime_begin_preservation(
    gdox_runtime *runtime,
    gdox_preservation_format format,
    const char *output_path,
    bool verify
);
void gdox_runtime_set_display(
    gdox_runtime *runtime,
    uint8_t internal_resolution_scale,
    gdox_emulator_aspect aspect,
    gdox_emulator_fit fit,
    bool fullscreen,
    uint16_t window_width,
    uint16_t window_height
);
bool gdox_runtime_set_xemu_override(
    gdox_runtime *runtime,
    const char *path
);
bool gdox_runtime_set_hdd_override(
    gdox_runtime *runtime,
    const char *path
);
bool gdox_runtime_set_preservation_directory(
    gdox_runtime *runtime,
    const char *path
);
bool gdox_runtime_open_disc_image(
    gdox_runtime *runtime,
    const char *path
);
bool gdox_runtime_import_firmware(
    gdox_runtime *runtime,
    const char *path
);
bool gdox_runtime_import_mcpx(
    gdox_runtime *runtime,
    const char *path
);
bool gdox_runtime_import_bios(
    gdox_runtime *runtime,
    const char *path
);

#endif
