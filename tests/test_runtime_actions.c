#include "app/runtime_actions.h"

#include <stdio.h>
#include <string.h>

typedef struct action_mocks {
    bool media_owned;
    bool playback_running;
    bool playback_stop_succeeds;
    bool playback_start_succeeds;
    bool session_close_succeeds;
    bool eject_succeeds;
    gdox_error_code eject_error;
    unsigned int playback_stop_calls;
    unsigned int playback_start_calls;
    unsigned int session_close_calls;
    unsigned int eject_calls;
    unsigned int attention_calls;
} action_mocks;

static action_mocks mocks;
static int failures;

static void check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "runtime actions test failed: %s\n", message);
        ++failures;
    }
}

static void reset_mocks(void)
{
    memset(&mocks, 0, sizeof(mocks));
    mocks.playback_stop_succeeds = true;
    mocks.playback_start_succeeds = true;
    mocks.session_close_succeeds = true;
    mocks.eject_succeeds = true;
    mocks.eject_error = GDOX_ERROR_IO;
}

static void initialize_eject_case(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor
)
{
    memset(runtime, 0, sizeof(*runtime));
    memset(snapshot, 0, sizeof(*snapshot));
    runtime->optical_drive = GDOX_OPTICAL_DRIVE_GP63;
    runtime->media.open = true;
    snapshot->media_source = GDOX_MEDIA_PHYSICAL_DISC;
    gdox_optical_monitor_initialize(monitor);
    mocks.media_owned = true;
}

static bool execute_eject(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor,
    uint32_t *observation_delay,
    gdox_error *error
)
{
    const gdox_runtime_request_entry request = {
        .kind = GDOX_RUNTIME_REQUEST_EJECT,
    };
    bool force_launch = false;

    return gdox_runtime_actions_execute(
        runtime,
        snapshot,
        &request,
        monitor,
        observation_delay,
        &force_launch,
        error
    );
}

static void execute_restart(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor,
    gdox_error *error
)
{
    const gdox_runtime_request_entry request = {
        .kind = GDOX_RUNTIME_REQUEST_RESTART,
    };
    uint32_t observation_delay = 0U;
    bool force_launch = false;

    (void)gdox_runtime_actions_execute(
        runtime,
        snapshot,
        &request,
        monitor,
        &observation_delay,
        &force_launch,
        error
    );
}

static bool observe_ready(gdox_optical_monitor *monitor)
{
    const gdox_optical_presence ready = {
        .drive_present = true,
        .media_status_known = true,
        .media_present = true,
    };

    return gdox_optical_monitor_observe(monitor, &ready);
}

static void check_stable_discovery(gdox_optical_monitor *monitor)
{
    uint32_t observation;

    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        check(!observe_ready(monitor), "discovery must remain debounced");
    }
    check(observe_ready(monitor), "stable media must permit discovery");
}

static void test_success_rearms_after_tray_eject(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_optical_monitor monitor;
    gdox_error error;
    uint32_t observation_delay = 0U;

    reset_mocks();
    initialize_eject_case(&runtime, &snapshot, &monitor);
    check(
        execute_eject(
            &runtime,
            &snapshot,
            &monitor,
            &observation_delay,
            &error
        ),
        "owned successful eject must complete the action cycle"
    );
    check(mocks.session_close_calls == 1U, "successful eject closes media");
    check(mocks.eject_calls == 1U, "successful eject opens the tray");
    check(gdox_optical_monitor_is_armed(&monitor), "tray eject rearms rediscovery");
    check_stable_discovery(&monitor);
}

static void test_unsupported_eject_rearms_discovery(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_optical_monitor monitor;
    gdox_error error;
    uint32_t observation_delay = 0U;

    reset_mocks();
    initialize_eject_case(&runtime, &snapshot, &monitor);
    mocks.eject_succeeds = false;
    mocks.eject_error = GDOX_ERROR_UNSUPPORTED;
    check(
        execute_eject(
            &runtime,
            &snapshot,
            &monitor,
            &observation_delay,
            &error
        ),
        "owned unsupported eject must complete the action cycle"
    );
    check(mocks.session_close_calls == 1U, "unsupported eject releases media ownership");
    check(mocks.eject_calls == 1U, "unsupported eject reaches the drive adapter");
    check(mocks.attention_calls == 1U, "unsupported eject reports attention");
    check(error.code == GDOX_ERROR_UNSUPPORTED, "unsupported eject preserves its error");
    check(gdox_optical_monitor_is_armed(&monitor), "unsupported eject rearms discovery");
    check(
        observation_delay == GDOX_RUNTIME_OBSERVATION_INTERVAL_TICKS,
        "released media retains the observation cooldown"
    );
    check_stable_discovery(&monitor);
}

static void test_close_failure_does_not_leave_monitor_blocked(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_optical_monitor monitor;
    gdox_error error;
    uint32_t observation_delay = 0U;

    reset_mocks();
    initialize_eject_case(&runtime, &snapshot, &monitor);
    mocks.session_close_succeeds = false;
    check(
        execute_eject(
            &runtime,
            &snapshot,
            &monitor,
            &observation_delay,
            &error
        ),
        "owned close failure must complete the action cycle"
    );
    check(mocks.session_close_calls == 1U, "failed eject attempts cleanup");
    check(mocks.eject_calls == 0U, "failed cleanup prevents tray command");
    check(mocks.media_owned, "failed cleanup retains media ownership");
    check(gdox_optical_monitor_is_armed(&monitor), "failed cleanup does not permanently block discovery");
    mocks.media_owned = false;
    check_stable_discovery(&monitor);
}

static void test_restart_short_circuits_and_reports_failures(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_optical_monitor monitor;
    gdox_error error;

    reset_mocks();
    initialize_eject_case(&runtime, &snapshot, &monitor);
    mocks.playback_running = true;
    execute_restart(&runtime, &snapshot, &monitor, &error);
    check(mocks.playback_stop_calls == 1U, "restart stops playback once");
    check(mocks.playback_start_calls == 1U, "restart starts playback once");
    check(mocks.attention_calls == 0U, "successful restart needs no attention");

    reset_mocks();
    initialize_eject_case(&runtime, &snapshot, &monitor);
    mocks.playback_running = true;
    mocks.playback_stop_succeeds = false;
    execute_restart(&runtime, &snapshot, &monitor, &error);
    check(mocks.playback_stop_calls == 1U, "failed restart attempts one stop");
    check(mocks.playback_start_calls == 0U, "failed stop prevents restart launch");
    check(mocks.attention_calls == 1U, "failed stop reports attention");

    reset_mocks();
    initialize_eject_case(&runtime, &snapshot, &monitor);
    mocks.playback_running = true;
    mocks.playback_start_succeeds = false;
    execute_restart(&runtime, &snapshot, &monitor, &error);
    check(mocks.playback_stop_calls == 1U, "failed launch follows one stop");
    check(mocks.playback_start_calls == 1U, "failed launch is attempted once");
    check(mocks.attention_calls == 1U, "failed launch reports attention");
}

bool gdox_runtime_media_is_owned(const gdox_runtime_media_session *session)
{
    (void)session;
    return mocks.media_owned;
}

bool gdox_runtime_playback_running(const gdox_runtime *runtime)
{
    (void)runtime;
    return mocks.playback_running;
}

bool gdox_runtime_playback_stop(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    (void)runtime;
    (void)snapshot;
    ++mocks.playback_stop_calls;
    if (!mocks.playback_stop_succeeds) {
        gdox_error_set(error, GDOX_ERROR_IO, "simulated playback stop failure");
        return false;
    }
    mocks.playback_running = false;
    return true;
}

bool gdox_runtime_playback_start(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    (void)runtime;
    (void)snapshot;
    ++mocks.playback_start_calls;
    if (!mocks.playback_start_succeeds) {
        gdox_error_set(error, GDOX_ERROR_IO, "simulated playback start failure");
        return false;
    }
    mocks.playback_running = true;
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_session_close(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    (void)snapshot;
    ++mocks.session_close_calls;
    if (!mocks.session_close_succeeds) {
        runtime->media.open = false;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated media close failure");
        return false;
    }
    mocks.media_owned = false;
    runtime->media.open = false;
    return true;
}

bool gdox_optical_eject(gdox_optical_drive drive, gdox_error *error)
{
    (void)drive;
    ++mocks.eject_calls;
    if (!mocks.eject_succeeds) {
        gdox_error_set(error, mocks.eject_error, "simulated eject failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

void gdox_runtime_attention(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *operation,
    const gdox_error *error,
    bool has_session,
    bool emulator_running
)
{
    (void)runtime;
    (void)snapshot;
    (void)operation;
    (void)error;
    (void)has_session;
    (void)emulator_running;
    ++mocks.attention_calls;
}

void gdox_runtime_copy_text(char *output, size_t capacity, const char *input)
{
    if (output != NULL && capacity != 0U) {
        (void)snprintf(output, capacity, "%s", input != NULL ? input : "");
    }
}

void gdox_runtime_session_select_physical(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor
)
{
    (void)runtime;
    (void)snapshot;
    (void)monitor;
}

bool gdox_runtime_session_prepare_image(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *path,
    bool launch,
    gdox_error *error
)
{
    (void)runtime;
    (void)snapshot;
    (void)path;
    (void)launch;
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_run_preservation(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *request,
    gdox_error *error
)
{
    (void)runtime;
    (void)snapshot;
    (void)request;
    gdox_error_clear(error);
    return true;
}

int main(void)
{
    test_success_rearms_after_tray_eject();
    test_unsupported_eject_rearms_discovery();
    test_close_failure_does_not_leave_monitor_blocked();
    test_restart_short_circuits_and_reports_failures();
    return failures == 0 ? 0 : 1;
}
