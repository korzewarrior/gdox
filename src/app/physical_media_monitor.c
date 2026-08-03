#include "app/physical_media_monitor.h"

bool gdox_physical_media_eject_request_matches(
    const gdox_media_observation *observation,
    uint64_t expected_generation
)
{
    return observation != NULL
        && observation->event == GDOX_MEDIA_EVENT_EJECT_REQUEST
        && observation->generation == expected_generation
        && observation->readiness == GDOX_MEDIA_READINESS_PRESENT;
}

static gdox_physical_media_event latch_event(
    gdox_physical_media_monitor *monitor,
    gdox_physical_media_event event
)
{
    if (monitor->event == GDOX_PHYSICAL_MEDIA_EVENT_NONE) {
        monitor->event = event;
    }
    return monitor->event;
}

void gdox_physical_media_monitor_initialize(
    gdox_physical_media_monitor *monitor
)
{
    if (monitor == NULL) {
        return;
    }
    *monitor = (gdox_physical_media_monitor){0};
}

gdox_physical_media_event gdox_physical_media_monitor_observe(
    gdox_physical_media_monitor *monitor,
    const gdox_media_observation *observation
)
{
    if (monitor == NULL || observation == NULL
        || monitor->event != GDOX_PHYSICAL_MEDIA_EVENT_NONE) {
        return monitor != NULL
            ? monitor->event
            : GDOX_PHYSICAL_MEDIA_EVENT_NONE;
    }
    if (observation->event == GDOX_MEDIA_EVENT_EJECT_REQUEST) {
        return latch_event(
            monitor, GDOX_PHYSICAL_MEDIA_EVENT_EJECT_REQUEST
        );
    }
    if (monitor->generation_known
        && (observation->generation != monitor->generation
            || observation->event == GDOX_MEDIA_EVENT_NEW_MEDIA
            || observation->event == GDOX_MEDIA_EVENT_REMOVAL
            || observation->event == GDOX_MEDIA_EVENT_CHANGED)) {
        return latch_event(monitor, GDOX_PHYSICAL_MEDIA_EVENT_CHANGED);
    }
    if (!monitor->generation_known) {
        monitor->generation = observation->generation;
        monitor->generation_known = true;
    }
    if (monitor->session_fault) {
        if (observation->readiness == GDOX_MEDIA_READINESS_PRESENT) {
            return latch_event(
                monitor, GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT
            );
        }
        if (monitor->fault_observations
            < GDOX_PHYSICAL_MEDIA_FAULT_CONFIRMATIONS) {
            ++monitor->fault_observations;
        }
        if (monitor->fault_observations
            >= GDOX_PHYSICAL_MEDIA_FAULT_CONFIRMATIONS) {
            return latch_event(
                monitor, GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT
            );
        }
    } else {
        monitor->fault_observations = 0U;
    }

    switch (observation->readiness) {
        case GDOX_MEDIA_READINESS_PRESENT:
            monitor->absent_observations = 0U;
            break;
        case GDOX_MEDIA_READINESS_ABSENT:
            if (monitor->absent_observations
                < GDOX_PHYSICAL_MEDIA_ABSENT_CONFIRMATIONS) {
                ++monitor->absent_observations;
            }
            if (monitor->absent_observations
                >= GDOX_PHYSICAL_MEDIA_ABSENT_CONFIRMATIONS) {
                return latch_event(
                    monitor, GDOX_PHYSICAL_MEDIA_EVENT_CHANGED
                );
            }
            break;
        case GDOX_MEDIA_READINESS_UNKNOWN:
            break;
    }
    return monitor->event;
}

gdox_physical_media_event gdox_physical_media_monitor_connection(
    gdox_physical_media_monitor *monitor,
    bool status_known,
    bool connected
)
{
    if (monitor == NULL
        || monitor->event != GDOX_PHYSICAL_MEDIA_EVENT_NONE) {
        return monitor != NULL
            ? monitor->event
            : GDOX_PHYSICAL_MEDIA_EVENT_NONE;
    }
    if (!status_known) {
        return monitor->event;
    }
    if (connected) {
        monitor->disconnected_observations = 0U;
        return monitor->event;
    }
    if (monitor->disconnected_observations
        < GDOX_PHYSICAL_MEDIA_DISCONNECT_CONFIRMATIONS) {
        ++monitor->disconnected_observations;
    }
    if (monitor->disconnected_observations
        >= GDOX_PHYSICAL_MEDIA_DISCONNECT_CONFIRMATIONS) {
        return latch_event(
            monitor, GDOX_PHYSICAL_MEDIA_EVENT_DISCONNECTED
        );
    }
    return monitor->event;
}

void gdox_physical_media_monitor_session_fault(
    gdox_physical_media_monitor *monitor
)
{
    if (monitor != NULL
        && monitor->event == GDOX_PHYSICAL_MEDIA_EVENT_NONE) {
        monitor->session_fault = true;
    }
}

gdox_physical_media_event gdox_physical_media_monitor_event(
    const gdox_physical_media_monitor *monitor
)
{
    return monitor != NULL
        ? monitor->event
        : GDOX_PHYSICAL_MEDIA_EVENT_NONE;
}
