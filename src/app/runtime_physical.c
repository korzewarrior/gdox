#include "app/runtime_physical.h"

#include "app/runtime_session.h"

static void begin_watch(
    gdox_runtime *runtime,
    gdox_runtime_physical_state *state
)
{
    if (state->watched_export == runtime->media.exported) {
        return;
    }
    state->watched_export = runtime->media.exported;
    gdox_physical_media_monitor_initialize(&state->monitor);
    state->media_delay = 0U;
    state->device_delay = 0U;
}

static void observe_connection(
    gdox_runtime *runtime,
    gdox_runtime_physical_state *state,
    gdox_error *error
)
{
    bool connected = false;
    bool status_known;

    if (state->device_delay > 0U) {
        --state->device_delay;
        return;
    }
    gdox_error_clear(error);
    status_known = gdox_optical_connected(
        runtime->optical_drive, &connected, error
    );
    (void)gdox_physical_media_monitor_connection(
        &state->monitor, status_known, connected
    );
    state->device_delay = GDOX_RUNTIME_LIVE_DEVICE_INTERVAL_TICKS;
}

static void observe_media(
    gdox_runtime *runtime,
    gdox_runtime_physical_state *state,
    bool session_failed,
    gdox_media_observation *observation
)
{
    *observation = (gdox_media_observation){0};

    if (session_failed) {
        gdox_physical_media_monitor_session_fault(&state->monitor);
    }
    if (!session_failed && state->media_delay > 0U) {
        --state->media_delay;
        return;
    }
    if (!gdox_nbd_observe_media(runtime->media.exported, observation)) {
        observation->generation = state->monitor.generation_known
            ? state->monitor.generation
            : 0U;
    }
    (void)gdox_physical_media_monitor_observe(
        &state->monitor, observation
    );
    state->media_delay = GDOX_RUNTIME_LIVE_MEDIA_INTERVAL_TICKS;
}

static gdox_runtime_physical_end_reason end_reason(
    gdox_physical_media_event event
)
{
    switch (event) {
        case GDOX_PHYSICAL_MEDIA_EVENT_EJECT_REQUEST:
            return GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED;
        case GDOX_PHYSICAL_MEDIA_EVENT_CHANGED:
            return GDOX_RUNTIME_PHYSICAL_MEDIA_CHANGED;
        case GDOX_PHYSICAL_MEDIA_EVENT_DISCONNECTED:
            return GDOX_RUNTIME_PHYSICAL_DRIVE_DISCONNECTED;
        case GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT:
            return GDOX_RUNTIME_PHYSICAL_SESSION_FAILED;
        case GDOX_PHYSICAL_MEDIA_EVENT_NONE:
            break;
    }
    return GDOX_RUNTIME_PHYSICAL_SESSION_FAILED;
}

static bool eject_request_is_current(
    gdox_runtime *runtime,
    const gdox_runtime_physical_state *state
)
{
    gdox_media_observation observation = {0};

    return runtime != NULL && state != NULL
        && runtime->media.exported != NULL
        && gdox_nbd_observe_media(
            runtime->media.exported, &observation
        )
        && gdox_physical_media_eject_request_matches(
            &observation, state->eject_generation
        );
}

static void complete_eject_request(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *optical_monitor,
    gdox_error *error
)
{
    gdox_optical_eject_completion completion;
    bool completed;

    completed = gdox_optical_complete_eject_request(
        state->eject_drive, &completion, error
    );
    if (completed
        && completion != GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED
        && completion
            != GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "drive eject request returned no completion outcome"
        );
        completed = false;
    }
    if (completed) {
        gdox_optical_monitor_eject_completed(
            optical_monitor, completion
        );
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            completion
                    == GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED
                ? "Drive eject request completed"
                : "Drive released; press the drive eject button again"
        );
        snapshot->can_eject = false;
        gdox_runtime_publish(runtime, snapshot);
    } else {
        gdox_optical_monitor_session_ended(optical_monitor);
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not complete drive eject request",
            error,
            false,
            false
        );
    }
    gdox_runtime_physical_reset(state);
}

static void finish_watch(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    gdox_physical_media_event event,
    const gdox_error *event_error
)
{
    const gdox_runtime_physical_end_reason reason = end_reason(event);
    bool eject_authorized = false;
    const bool cleanup_pending = gdox_runtime_session_end_physical(
        runtime,
        snapshot,
        reason,
        state->eject_generation,
        &eject_authorized,
        event_error,
        optical_monitor
    );

    *observation_delay = GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS;
    if (cleanup_pending) {
        if (reason == GDOX_RUNTIME_PHYSICAL_SESSION_FAILED) {
            state->cleanup = GDOX_RUNTIME_PHYSICAL_CLEANUP_REQUIRE_RETRY;
        } else if (reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED) {
            state->cleanup = GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT;
        } else {
            state->cleanup = GDOX_RUNTIME_PHYSICAL_CLEANUP_REARM;
        }
        return;
    }
    if (reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED
        && eject_authorized) {
        gdox_error eject_error;

        gdox_error_clear(&eject_error);
        complete_eject_request(
            runtime, snapshot, state, optical_monitor, &eject_error
        );
        return;
    }
    gdox_runtime_physical_reset(state);
}

void gdox_runtime_physical_initialize(gdox_runtime_physical_state *state)
{
    if (state == NULL) {
        return;
    }
    *state = (gdox_runtime_physical_state){0};
    gdox_physical_media_monitor_initialize(&state->monitor);
}

void gdox_runtime_physical_reset(gdox_runtime_physical_state *state)
{
    gdox_runtime_physical_initialize(state);
}

bool gdox_runtime_physical_poll(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *optical_monitor,
    uint32_t *observation_delay,
    gdox_error *error
)
{
    gdox_error session_error;
    bool session_failed;
    gdox_media_observation observation;
    gdox_physical_media_event event;

    if (runtime == NULL || snapshot == NULL || state == NULL
        || optical_monitor == NULL || observation_delay == NULL
        || runtime->media.exported == NULL) {
        return false;
    }
    if (state->cleanup != GDOX_RUNTIME_PHYSICAL_CLEANUP_NONE) {
        if (runtime->media.open) {
            const gdox_physical_media_event retry_event =
                state->cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT
                    ? GDOX_PHYSICAL_MEDIA_EVENT_EJECT_REQUEST
                    : state->cleanup
                        == GDOX_RUNTIME_PHYSICAL_CLEANUP_REQUIRE_RETRY
                        ? GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT
                        : GDOX_PHYSICAL_MEDIA_EVENT_CHANGED;

            finish_watch(
                runtime,
                snapshot,
                state,
                optical_monitor,
                observation_delay,
                retry_event,
                NULL
            );
            return true;
        }
        return false;
    }
    begin_watch(runtime, state);
    session_failed = gdox_nbd_runtime_error(
        runtime->media.exported, &session_error
    );
    observe_media(runtime, state, session_failed, &observation);
    observe_connection(runtime, state, error);
    event = gdox_physical_media_monitor_event(&state->monitor);
    if (event == GDOX_PHYSICAL_MEDIA_EVENT_NONE) {
        return false;
    }
    if (event == GDOX_PHYSICAL_MEDIA_EVENT_EJECT_REQUEST
        && observation.event == GDOX_MEDIA_EVENT_EJECT_REQUEST
        && state->eject_drive == GDOX_OPTICAL_DRIVE_NONE) {
        state->eject_generation = observation.generation;
        state->eject_drive = runtime->optical_drive;
    }
    finish_watch(
        runtime,
        snapshot,
        state,
        optical_monitor,
        observation_delay,
        event,
        session_failed ? &session_error : NULL
    );
    return true;
}

void gdox_runtime_physical_cleanup_completed(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *optical_monitor,
    gdox_error *error
)
{
    if (runtime == NULL || snapshot == NULL || state == NULL
        || optical_monitor == NULL || error == NULL) {
        return;
    }
    if (state->cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_REARM) {
        gdox_optical_monitor_session_ended(optical_monitor);
    } else if (
        state->cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_REQUIRE_RETRY
    ) {
        gdox_optical_monitor_fail(
            optical_monitor, GDOX_OPTICAL_MONITOR_FAILURE_TERMINAL
        );
    } else if (state->cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT) {
        complete_eject_request(
            runtime, snapshot, state, optical_monitor, error
        );
        return;
    }
    gdox_runtime_physical_reset(state);
}

void gdox_runtime_physical_validate_cleanup(
    gdox_runtime *runtime,
    gdox_runtime_physical_state *state
)
{
    if (runtime != NULL && state != NULL
        && state->cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT
        && !eject_request_is_current(runtime, state)) {
        state->cleanup = GDOX_RUNTIME_PHYSICAL_CLEANUP_REARM;
    }
}
