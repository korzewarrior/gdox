#ifndef GDOX_DISC_BRIDGE_TOOLS_LINUX_H
#define GDOX_DISC_BRIDGE_TOOLS_LINUX_H

#include "gdox/emulator.h"

typedef struct gdox_disc_bridge_tools {
    char mount[GDOX_EMULATOR_PATH_CAPACITY];
    char unmount[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_disc_bridge_tools;

bool gdox_disc_bridge_tools_resolve(
    gdox_disc_bridge_tools *tools,
    gdox_error *error
);

#endif
