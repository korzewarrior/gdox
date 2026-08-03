#ifndef GDOX_RUNTIME_BUNDLE_H
#define GDOX_RUNTIME_BUNDLE_H

#include "gdox/emulator.h"
#include "gdox/error.h"

#include <stdbool.h>

typedef enum gdox_firmware_kind {
    GDOX_FIRMWARE_MCPX = 0,
    GDOX_FIRMWARE_FLASH,
} gdox_firmware_kind;

typedef struct gdox_runtime_bundle_status {
    bool xemu_available;
    bool bundled;
    bool configuration_ready;
    bool mcpx_ready;
    bool flash_ready;
    bool hdd_ready;
    bool custom_executable;
    bool full_hdd_isolation;
    bool persistent_save_export;
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char mcpx[GDOX_EMULATOR_PATH_CAPACITY];
    char flash[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd[GDOX_EMULATOR_PATH_CAPACITY];
    char eeprom[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_runtime_bundle_status;

bool gdox_runtime_bundle_prepare(
    const char *executable_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
);
bool gdox_runtime_bundle_import_firmware(
    gdox_firmware_kind kind,
    const char *source,
    const char *executable_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
);
bool gdox_runtime_bundle_import_firmware_auto(
    const char *source,
    const char *executable_override,
    gdox_firmware_kind *kind,
    gdox_runtime_bundle_status *status,
    gdox_error *error
);

#endif
