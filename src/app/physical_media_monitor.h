#ifndef GDOX_APP_PHYSICAL_MEDIA_MONITOR_H
#define GDOX_APP_PHYSICAL_MEDIA_MONITOR_H

#include "gdox/source.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    GDOX_PHYSICAL_MEDIA_ABSENT_CONFIRMATIONS = 2U,
    GDOX_PHYSICAL_MEDIA_DISCONNECT_CONFIRMATIONS = 3U,
    GDOX_PHYSICAL_MEDIA_FAULT_CONFIRMATIONS = 3U,
};

typedef enum gdox_physical_media_event {
    GDOX_PHYSICAL_MEDIA_EVENT_NONE = 0,
    GDOX_PHYSICAL_MEDIA_EVENT_EJECT_REQUEST,
    GDOX_PHYSICAL_MEDIA_EVENT_CHANGED,
    GDOX_PHYSICAL_MEDIA_EVENT_DISCONNECTED,
    GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT,
} gdox_physical_media_event;

typedef struct gdox_physical_media_monitor {
    gdox_physical_media_event event;
    uint64_t generation;
    uint32_t absent_observations;
    uint32_t disconnected_observations;
    uint32_t fault_observations;
    bool generation_known;
    bool session_fault;
} gdox_physical_media_monitor;

bool gdox_physical_media_eject_request_matches(
    const gdox_media_observation *observation,
    uint64_t expected_generation
);
void gdox_physical_media_monitor_initialize(
    gdox_physical_media_monitor *monitor
);
gdox_physical_media_event gdox_physical_media_monitor_observe(
    gdox_physical_media_monitor *monitor,
    const gdox_media_observation *observation
);
gdox_physical_media_event gdox_physical_media_monitor_connection(
    gdox_physical_media_monitor *monitor,
    bool status_known,
    bool connected
);
void gdox_physical_media_monitor_session_fault(
    gdox_physical_media_monitor *monitor
);
gdox_physical_media_event gdox_physical_media_monitor_event(
    const gdox_physical_media_monitor *monitor
);

#endif
