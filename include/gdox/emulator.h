#ifndef GDOX_EMULATOR_H
#define GDOX_EMULATOR_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_EMULATOR_PATH_CAPACITY 4096U

typedef enum gdox_emulator_aspect {
    GDOX_EMULATOR_ASPECT_AUTOMATIC = 0,
    GDOX_EMULATOR_ASPECT_WIDESCREEN,
    GDOX_EMULATOR_ASPECT_FOUR_THREE,
    GDOX_EMULATOR_ASPECT_NATIVE
} gdox_emulator_aspect;

typedef enum gdox_emulator_fit {
    GDOX_EMULATOR_FIT_CENTER = 0,
    GDOX_EMULATOR_FIT_SCALE,
    GDOX_EMULATOR_FIT_STRETCH
} gdox_emulator_fit;

typedef struct gdox_emulator_paths {
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char configuration[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_emulator_paths;

typedef struct gdox_emulator_options {
    const char *executable;
    const char *configuration;
    uint8_t internal_resolution_scale;
    gdox_emulator_aspect aspect;
    gdox_emulator_fit fit;
    bool fullscreen;
    bool console_output;
    uint16_t window_width;
    uint16_t window_height;
} gdox_emulator_options;

typedef struct gdox_emulator_process gdox_emulator_process;

bool gdox_emulator_discover_executable(
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
);
bool gdox_emulator_validate_executable(
    const char *path,
    gdox_error *error
);
bool gdox_emulator_discover(gdox_emulator_paths *paths, gdox_error *error);
bool gdox_emulator_prepare(
    const gdox_emulator_options *options,
    gdox_error *error
);
bool gdox_emulator_launch(
    const gdox_emulator_options *options,
    const char *dvd_uri,
    gdox_emulator_process **process,
    gdox_error *error
);
bool gdox_emulator_poll(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
);
bool gdox_emulator_stop(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
);
void gdox_emulator_process_destroy(gdox_emulator_process *process);

#ifdef __cplusplus
}
#endif

#endif
