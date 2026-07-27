#include "app/optical_monitor.h"

void gdox_optical_monitor_initialize(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = 0U;
    monitor->rearm_observations = 0U;
    monitor->attempt_permitted = true;
    monitor->command_required = false;
}

void gdox_optical_monitor_block(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = 0U;
    monitor->rearm_observations = 0U;
    monitor->attempt_permitted = false;
    monitor->command_required = false;
}

void gdox_optical_monitor_fail(gdox_optical_monitor *monitor)
{
    gdox_optical_monitor_block(monitor);
    monitor->command_required = true;
}

void gdox_optical_monitor_retry(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = GDOX_MEDIA_STABLE_OBSERVATIONS;
    monitor->rearm_observations = 0U;
    monitor->attempt_permitted = true;
    monitor->command_required = false;
}

bool gdox_optical_monitor_observe(
    gdox_optical_monitor *monitor,
    const gdox_optical_presence *presence
)
{
    const bool media_absent = presence->media_status_known
        && !presence->media_present;

    if (!presence->drive_present || media_absent) {
        monitor->stable_observations = 0U;
        if (monitor->rearm_observations < GDOX_MEDIA_REARM_OBSERVATIONS) {
            ++monitor->rearm_observations;
        }
        if (!monitor->command_required
            && monitor->rearm_observations
                >= GDOX_MEDIA_REARM_OBSERVATIONS) {
            monitor->attempt_permitted = true;
        }
        return false;
    }
    monitor->rearm_observations = 0U;
    if (monitor->stable_observations < GDOX_MEDIA_STABLE_OBSERVATIONS) {
        ++monitor->stable_observations;
    }
    if (!monitor->attempt_permitted
        || monitor->stable_observations < GDOX_MEDIA_STABLE_OBSERVATIONS) {
        return false;
    }
    monitor->attempt_permitted = false;
    return true;
}

bool gdox_optical_monitor_is_armed(const gdox_optical_monitor *monitor)
{
    return monitor->attempt_permitted;
}
