#ifndef GDOX_XENIA_BRIDGE_LINUX_H
#define GDOX_XENIA_BRIDGE_LINUX_H

#include "gdox/xenia.h"

typedef struct gdox_xenia_bridge gdox_xenia_bridge;

bool gdox_xenia_bridge_preflight(gdox_error *error);
bool gdox_xenia_bridge_start(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_bridge **output,
    gdox_error *error
);
const char *gdox_xenia_bridge_path(const gdox_xenia_bridge *bridge);
bool gdox_xenia_bridge_alive(
    gdox_xenia_bridge *bridge,
    bool *alive,
    gdox_error *error
);
bool gdox_xenia_bridge_close(
    gdox_xenia_bridge *bridge,
    gdox_error *error
);
bool gdox_xenia_bridge_try_close(
    gdox_xenia_bridge *bridge,
    bool *closed,
    gdox_error *error
);
void gdox_xenia_bridge_destroy(gdox_xenia_bridge *bridge);

#endif
