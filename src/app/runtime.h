#ifndef GDOX_APP_RUNTIME_H
#define GDOX_APP_RUNTIME_H

#include "app/model.h"

#include <stdbool.h>
#include <stddef.h>

#define GDOX_RUNTIME_DISCOVERING GDOX_APP_DISCOVERING
#define GDOX_RUNTIME_EMPTY GDOX_APP_EMPTY
#define GDOX_RUNTIME_PREPARING GDOX_APP_PREPARING
#define GDOX_RUNTIME_READY GDOX_APP_DISC_READY
#define GDOX_RUNTIME_PLAYING GDOX_APP_PLAYING
#define GDOX_RUNTIME_PRESERVING GDOX_APP_PRESERVING
#define GDOX_RUNTIME_PRESERVED GDOX_APP_PRESERVED
#define GDOX_RUNTIME_ATTENTION GDOX_APP_ATTENTION

typedef enum gdox_runtime_command {
    GDOX_RUNTIME_START = 0,
    GDOX_RUNTIME_RESTART,
    GDOX_RUNTIME_CLOSE,
    GDOX_RUNTIME_EJECT,
    GDOX_RUNTIME_CANCEL_PRESERVATION,
    GDOX_RUNTIME_USE_PHYSICAL_DISC
} gdox_runtime_command;

typedef gdox_app_snapshot gdox_runtime_snapshot;

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
