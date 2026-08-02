#ifndef GDOX_CORE_EMULATOR_CONFIGURATION_H
#define GDOX_CORE_EMULATOR_CONFIGURATION_H

#include "gdox/emulator.h"

#include <stdbool.h>

bool gdox_emulator_configuration_update(
    const gdox_emulator_options *options,
    const char *original,
    char **updated,
    gdox_error *error
);
bool gdox_emulator_configuration_get_file(
    const char *configuration,
    const char *key,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
);
bool gdox_emulator_configuration_set_file(
    const char *configuration,
    const char *key,
    const char *path,
    char **updated,
    gdox_error *error
);

#endif
