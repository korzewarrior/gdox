#include "app/optical_monitor.h"

void gdox_optical_monitor_initialize(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = 0U;
    monitor->rearm_observations = 0U;
    monitor->transient_failures = 0U;
    monitor->attempt_permitted = true;
    monitor->command_required = false;
}

void gdox_optical_monitor_block(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = 0U;
    monitor->rearm_observations = 0U;
    monitor->transient_failures = 0U;
    monitor->attempt_permitted = false;
    monitor->command_required = false;
}

void gdox_optical_monitor_eject_completed(
    gdox_optical_monitor *monitor,
    gdox_optical_eject_completion completion
)
{
    if (completion == GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED) {
        gdox_optical_monitor_session_ended(monitor);
    } else {
        gdox_optical_monitor_block(monitor);
    }
}

void gdox_optical_monitor_fail(
    gdox_optical_monitor *monitor,
    gdox_optical_monitor_failure failure
)
{
    monitor->stable_observations = 0U;
    monitor->rearm_observations = 0U;
    monitor->attempt_permitted = false;
    if (failure == GDOX_OPTICAL_MONITOR_FAILURE_TRANSIENT
        && monitor->transient_failures
            < GDOX_MEDIA_TRANSIENT_RETRY_LIMIT) {
        ++monitor->transient_failures;
        monitor->attempt_permitted = true;
        monitor->command_required = false;
        return;
    }
    monitor->command_required = true;
}

void gdox_optical_monitor_session_ended(gdox_optical_monitor *monitor)
{
    gdox_optical_monitor_initialize(monitor);
}

void gdox_optical_monitor_retry(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = GDOX_MEDIA_STABLE_OBSERVATIONS;
    monitor->rearm_observations = 0U;
    monitor->transient_failures = 0U;
    monitor->attempt_permitted = true;
    monitor->command_required = false;
}

void gdox_optical_monitor_observation_failed(gdox_optical_monitor *monitor)
{
    monitor->stable_observations = 0U;
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
        if (monitor->rearm_observations
            >= GDOX_MEDIA_REARM_OBSERVATIONS) {
            monitor->transient_failures = 0U;
            monitor->attempt_permitted = true;
            monitor->command_required = false;
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

bool gdox_optical_monitor_has_pending_failure(
    const gdox_optical_monitor *monitor
)
{
    return monitor->command_required || monitor->transient_failures != 0U;
}
