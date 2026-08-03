#include "app/runtime_physical.h"
#include "app/runtime_session.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            (void)fprintf(                                                     \
                stderr, "%s:%d: check failed: %s\n",                         \
                __FILE__, __LINE__, #expression                               \
            );                                                                 \
            return false;                                                      \
        }                                                                      \
    } while (false)

typedef struct fake_runtime_physical {
    gdox_media_observation observations[6];
    size_t observation_count;
    size_t observation_index;
    gdox_runtime_physical_end_reason end_reason;
    unsigned int end_count;
    unsigned int eject_count;
    unsigned int publish_count;
    unsigned int attention_count;
    char actions[8];
    size_t action_count;
    bool cleanup_pending;
    bool eject_succeeds;
    bool runtime_failed;
    bool authorization_blocked;
    bool leave_media_open;
    gdox_optical_eject_completion eject_completion;
} fake_runtime_physical;

static fake_runtime_physical fake;
static int export_token;

static gdox_nbd_export *test_export(void)
{
    return (gdox_nbd_export *)&export_token;
}

static void reset_fake(void)
{
    fake = (fake_runtime_physical){0};
    fake.eject_succeeds = true;
    fake.eject_completion = GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED;
}

static void append_action(char action)
{
    if (fake.action_count < sizeof(fake.actions)) {
        fake.actions[fake.action_count++] = action;
    }
}

bool gdox_nbd_observe_media(
    const gdox_nbd_export *exported,
    gdox_media_observation *output
)
{
    (void)exported;
    if (output == NULL || fake.observation_index >= fake.observation_count) {
        return false;
    }
    *output = fake.observations[fake.observation_index++];
    return true;
}

bool gdox_nbd_runtime_error(
    const gdox_nbd_export *exported,
    gdox_error *error
)
{
    (void)exported;
    if (fake.runtime_failed) {
        gdox_error_set(error, GDOX_ERROR_IO, "injected NBD failure");
        return true;
    }
    gdox_error_clear(error);
    return false;
}

bool gdox_optical_connected(
    gdox_optical_drive drive,
    bool *connected,
    gdox_error *error
)
{
    (void)drive;
    *connected = true;
    gdox_error_clear(error);
    return true;
}

bool gdox_optical_complete_eject_request(
    gdox_optical_drive drive,
    gdox_optical_eject_completion *completion,
    gdox_error *error
)
{
    (void)drive;
    append_action('E');
    ++fake.eject_count;
    if (!fake.eject_succeeds) {
        gdox_error_set(error, GDOX_ERROR_IO, "injected eject failure");
        return false;
    }
    *completion = fake.eject_completion;
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_session_end_physical(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_end_reason reason,
    uint64_t eject_generation,
    bool *eject_authorized,
    const gdox_error *observation_error,
    gdox_optical_monitor *optical_monitor
)
{
    gdox_media_observation observation = {0};

    (void)snapshot;
    (void)observation_error;
    (void)optical_monitor;
    ++fake.end_count;
    fake.end_reason = reason;
    if (eject_authorized != NULL) {
        *eject_authorized = reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED
            && !fake.authorization_blocked
            && gdox_nbd_observe_media(
                runtime->media.exported, &observation
            )
            && gdox_physical_media_eject_request_matches(
                &observation, eject_generation
            );
    }
    append_action('C');
    if (!fake.leave_media_open) {
        runtime->media.open = false;
    }
    return fake.cleanup_pending;
}

void gdox_runtime_copy_text(
    char *output,
    size_t capacity,
    const char *text
)
{
    size_t bytes;

    if (output == NULL || capacity == 0U) {
        return;
    }
    bytes = strlen(text);
    if (bytes >= capacity) {
        bytes = capacity - 1U;
    }
    memcpy(output, text, bytes);
    output[bytes] = '\0';
}

void gdox_runtime_publish(
    gdox_runtime *runtime,
    const gdox_runtime_snapshot *snapshot
)
{
    (void)runtime;
    (void)snapshot;
    ++fake.publish_count;
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
    ++fake.attention_count;
}

static void initialize_runtime(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *monitor
)
{
    *runtime = (gdox_runtime){0};
    *snapshot = (gdox_runtime_snapshot){0};
    runtime->media.open = true;
    runtime->media.exported = test_export();
    runtime->optical_drive = GDOX_OPTICAL_DRIVE_GP65;
    snapshot->can_eject = true;
    gdox_runtime_physical_initialize(state);
    gdox_optical_monitor_initialize(monitor);
}

static bool poll(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_state *state,
    gdox_optical_monitor *monitor
)
{
    uint32_t observation_delay = 0U;
    gdox_error error;

    gdox_error_clear(&error);
    return gdox_runtime_physical_poll(
        runtime,
        snapshot,
        state,
        monitor,
        &observation_delay,
        &error
    );
}

static bool test_current_request_ejects_after_close(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;
    const gdox_optical_presence replacement = {
        .drive_present = true,
        .media_status_known = true,
        .media_present = true,
    };

    reset_fake();
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(4),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = fake.observations[0];
    fake.observation_count = 2U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.end_count == 1U);
    CHECK(fake.end_reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED);
    CHECK(fake.eject_count == 1U);
    CHECK(fake.action_count == 2U);
    CHECK(fake.actions[0] == 'C' && fake.actions[1] == 'E');
    CHECK(monitor.attempt_permitted);
    CHECK(!snapshot.can_eject);
    for (uint32_t observation = 1U;
        observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
        ++observation) {
        CHECK(!gdox_optical_monitor_observe(&monitor, &replacement));
    }
    CHECK(gdox_optical_monitor_observe(&monitor, &replacement));
    return true;
}

static bool test_ordinary_change_never_ejects(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;

    reset_fake();
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(7),
    };
    fake.observations[1] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(8),
        .event = GDOX_MEDIA_EVENT_NEW_MEDIA,
    };
    fake.observation_count = 2U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(!poll(&runtime, &snapshot, &state, &monitor));
    state.media_delay = 0U;
    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.end_count == 1U);
    CHECK(fake.end_reason == GDOX_RUNTIME_PHYSICAL_MEDIA_CHANGED);
    CHECK(fake.eject_count == 0U);
    return true;
}

static bool test_manual_eject_request_reports_release(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;

    reset_fake();
    fake.eject_completion =
        GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT;
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(6),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = fake.observations[0];
    fake.observation_count = 2U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);
    runtime.optical_drive = GDOX_OPTICAL_DRIVE_ASUS_NR09;

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.eject_count == 1U);
    CHECK(strcmp(
        snapshot.notice,
        "Drive released; press the drive eject button again"
    ) == 0);
    CHECK(!monitor.attempt_permitted);
    CHECK(!snapshot.can_eject);
    return true;
}

static bool test_missing_eject_outcome_reports_failure(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;

    reset_fake();
    fake.eject_completion = GDOX_OPTICAL_EJECT_COMPLETION_NONE;
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(7),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = fake.observations[0];
    fake.observation_count = 2U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.eject_count == 1U);
    CHECK(fake.attention_count == 1U);
    CHECK(fake.publish_count == 0U);
    return true;
}

static bool test_request_absent_before_close_is_cancelled(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;

    reset_fake();
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(10),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = fake.observations[0];
    fake.observations[1].readiness = GDOX_MEDIA_READINESS_ABSENT;
    fake.observation_count = 2U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.end_count == 1U);
    CHECK(fake.eject_count == 0U);
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_NONE);
    return true;
}

static bool test_cleanup_retry_revalidates_request(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;
    gdox_error error;

    reset_fake();
    fake.cleanup_pending = true;
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(20),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = fake.observations[0];
    fake.observations[2] = fake.observations[0];
    fake.observation_count = 3U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT);
    CHECK(fake.eject_count == 0U);
    CHECK(state.eject_generation == UINT64_C(20));
    CHECK(!poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.end_count == 1U);
    CHECK(state.eject_generation == UINT64_C(20));
    gdox_runtime_physical_validate_cleanup(&runtime, &state);
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT);
    gdox_error_clear(&error);
    gdox_runtime_physical_cleanup_completed(
        &runtime, &snapshot, &state, &monitor, &error
    );
    CHECK(fake.eject_count == 1U);
    CHECK(fake.action_count == 2U);
    CHECK(fake.actions[0] == 'C' && fake.actions[1] == 'E');

    reset_fake();
    fake.cleanup_pending = true;
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(30),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = fake.observations[0];
    fake.observations[2] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(31),
    };
    fake.observation_count = 3U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT);
    gdox_runtime_physical_validate_cleanup(&runtime, &state);
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_REARM);
    gdox_error_clear(&error);
    gdox_runtime_physical_cleanup_completed(
        &runtime, &snapshot, &state, &monitor, &error
    );
    CHECK(fake.eject_count == 0U);
    return true;
}

static bool test_unobserved_fault_is_not_a_media_change(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;

    reset_fake();
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(41),
    };
    fake.observation_count = 1U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);
    CHECK(!poll(&runtime, &snapshot, &state, &monitor));

    fake.runtime_failed = true;
    CHECK(!poll(&runtime, &snapshot, &state, &monitor));
    CHECK(!poll(&runtime, &snapshot, &state, &monitor));
    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.end_reason == GDOX_RUNTIME_PHYSICAL_SESSION_FAILED);
    CHECK(fake.eject_count == 0U);
    return true;
}

static bool test_stop_retry_revalidates_before_eject(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_runtime_physical_state state;
    gdox_optical_monitor monitor;

    reset_fake();
    fake.cleanup_pending = true;
    fake.authorization_blocked = true;
    fake.leave_media_open = true;
    fake.observations[0] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(50),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };
    fake.observations[1] = (gdox_media_observation){
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(51),
    };
    fake.observation_count = 2U;
    initialize_runtime(&runtime, &snapshot, &state, &monitor);

    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_EJECT);
    CHECK(state.eject_generation == UINT64_C(50));
    CHECK(runtime.media.open);
    CHECK(fake.eject_count == 0U);

    fake.cleanup_pending = false;
    fake.authorization_blocked = false;
    fake.leave_media_open = false;
    CHECK(poll(&runtime, &snapshot, &state, &monitor));
    CHECK(fake.end_count == 2U);
    CHECK(state.cleanup == GDOX_RUNTIME_PHYSICAL_CLEANUP_NONE);
    CHECK(fake.eject_count == 0U);
    return true;
}

int main(void)
{
    return test_current_request_ejects_after_close()
        && test_ordinary_change_never_ejects()
        && test_manual_eject_request_reports_release()
        && test_missing_eject_outcome_reports_failure()
        && test_request_absent_before_close_is_cancelled()
        && test_cleanup_retry_revalidates_request()
        && test_unobserved_fault_is_not_a_media_change()
        && test_stop_retry_revalidates_before_eject()
        ? 0
        : 1;
}
