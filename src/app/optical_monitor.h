#ifndef GDOX_APP_OPTICAL_MONITOR_H
#define GDOX_APP_OPTICAL_MONITOR_H

#include "gdox/optical.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    GDOX_MEDIA_STABLE_OBSERVATIONS = 4U,
    GDOX_MEDIA_REARM_OBSERVATIONS = 10U,
};

typedef struct gdox_optical_monitor {
    uint32_t stable_observations;
    uint32_t rearm_observations;
    bool attempt_permitted;
    bool command_required;
} gdox_optical_monitor;

void gdox_optical_monitor_initialize(gdox_optical_monitor *monitor);
void gdox_optical_monitor_block(gdox_optical_monitor *monitor);
void gdox_optical_monitor_fail(gdox_optical_monitor *monitor);
void gdox_optical_monitor_retry(gdox_optical_monitor *monitor);
bool gdox_optical_monitor_observe(
    gdox_optical_monitor *monitor,
    const gdox_optical_presence *presence
);
bool gdox_optical_monitor_is_armed(const gdox_optical_monitor *monitor);

#endif
