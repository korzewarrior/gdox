#ifndef GDOX_APP_OPTICAL_MONITOR_H
#define GDOX_APP_OPTICAL_MONITOR_H

#include "gdox/optical.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    GDOX_MEDIA_STABLE_OBSERVATIONS = 4U,
    GDOX_MEDIA_REARM_OBSERVATIONS = 10U,
    GDOX_MEDIA_TRANSIENT_RETRY_LIMIT = 2U,
};

typedef enum gdox_optical_monitor_failure {
    GDOX_OPTICAL_MONITOR_FAILURE_TRANSIENT = 0,
    GDOX_OPTICAL_MONITOR_FAILURE_TERMINAL,
} gdox_optical_monitor_failure;

typedef struct gdox_optical_monitor {
    uint32_t stable_observations;
    uint32_t rearm_observations;
    uint32_t transient_failures;
    bool attempt_permitted;
    bool command_required;
} gdox_optical_monitor;

void gdox_optical_monitor_initialize(gdox_optical_monitor *monitor);
void gdox_optical_monitor_block(gdox_optical_monitor *monitor);
void gdox_optical_monitor_eject_completed(
    gdox_optical_monitor *monitor,
    gdox_optical_eject_completion completion
);
void gdox_optical_monitor_fail(
    gdox_optical_monitor *monitor,
    gdox_optical_monitor_failure failure
);
void gdox_optical_monitor_session_ended(gdox_optical_monitor *monitor);
void gdox_optical_monitor_retry(gdox_optical_monitor *monitor);
void gdox_optical_monitor_observation_failed(gdox_optical_monitor *monitor);
bool gdox_optical_monitor_observe(
    gdox_optical_monitor *monitor,
    const gdox_optical_presence *presence
);
bool gdox_optical_monitor_is_armed(const gdox_optical_monitor *monitor);
bool gdox_optical_monitor_has_pending_failure(
    const gdox_optical_monitor *monitor
);

#endif
