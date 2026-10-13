#ifndef GDOX_DISC_BRIDGE_LINUX_H
#define GDOX_DISC_BRIDGE_LINUX_H

#include "gdox/emulator.h"

typedef struct gdox_disc_bridge gdox_disc_bridge;

bool gdox_disc_bridge_preflight(gdox_error *error);
bool gdox_disc_bridge_start(
    const char *storage_root,
    bool console_output,
    const char *disc_uri,
    uint64_t length,
    gdox_disc_bridge **output,
    gdox_error *error
);
const char *gdox_disc_bridge_path(const gdox_disc_bridge *bridge);
bool gdox_disc_bridge_alive(
    gdox_disc_bridge *bridge,
    bool *alive,
    gdox_error *error
);
bool gdox_disc_bridge_close(
    gdox_disc_bridge *bridge,
    gdox_error *error
);
bool gdox_disc_bridge_try_close(
    gdox_disc_bridge *bridge,
    bool *closed,
    gdox_error *error
);
void gdox_disc_bridge_destroy(gdox_disc_bridge *bridge);

#endif
