#ifndef GDOX_XENIA_BRIDGE_TOOLS_LINUX_H
#define GDOX_XENIA_BRIDGE_TOOLS_LINUX_H

#include "gdox/xenia.h"

typedef struct gdox_xenia_bridge_tools {
    char mount[GDOX_EMULATOR_PATH_CAPACITY];
    char unmount[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_xenia_bridge_tools;

bool gdox_xenia_bridge_tools_resolve(
    gdox_xenia_bridge_tools *tools,
    gdox_error *error
);

#endif
