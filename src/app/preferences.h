#ifndef GDOX_PREFERENCES_H
#define GDOX_PREFERENCES_H

#include "gdox/emulator.h"
#include "gdox/error.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct gdox_preferences {
    bool auto_start;
    uint8_t internal_resolution_scale;
    gdox_emulator_aspect display_aspect;
    gdox_emulator_fit display_fit;
    bool fullscreen;
    uint16_t window_width;
    uint16_t window_height;
    char xemu_override[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd_override[GDOX_EMULATOR_PATH_CAPACITY];
    char preservation_directory[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_preferences;

void gdox_preferences_defaults(gdox_preferences *preferences);
bool gdox_preferences_load(
    gdox_preferences *preferences,
    gdox_error *error
);
bool gdox_preferences_save(
    const gdox_preferences *preferences,
    gdox_error *error
);

#endif
