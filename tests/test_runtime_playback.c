#include "app/runtime_playback.h"
#include "app/runtime_xemu.h"
#include "app/runtime_xenia.h"

#include <stdio.h>
#include <string.h>

struct gdox_emulator_process {
    int marker;
};

struct gdox_xenia_process {
    int marker;
};

typedef struct mock_playback {
    bool prepare_success;
    bool preparation_was_visible;
    bool start_success;
    bool start_sets_process;
    bool stop_success;
    bool poll_success;
    bool poll_running;
    int poll_exit_code;
    unsigned int prepares;
    unsigned int starts;
    unsigned int stops;
    unsigned int polls;
    unsigned int destroys;
    gdox_xemu_legacy_migration_outcome migration_outcome;
} mock_playback;

static struct gdox_emulator_process xemu_process;
static struct gdox_xenia_process xenia_process;
static mock_playback xemu_mock;
static mock_playback xenia_mock;
static int failures;

static void check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "runtime playback test failed: %s\n", message);
        ++failures;
    }
}

static void reset_mocks(void)
{
    memset(&xemu_mock, 0, sizeof(xemu_mock));
    memset(&xenia_mock, 0, sizeof(xenia_mock));
    xemu_mock.prepare_success = true;
    xemu_mock.start_success = true;
    xemu_mock.start_sets_process = true;
    xemu_mock.stop_success = true;
    xemu_mock.poll_success = true;
    xemu_mock.poll_running = true;
    xenia_mock.start_success = true;
    xenia_mock.start_sets_process = true;
    xenia_mock.stop_success = true;
    xenia_mock.poll_success = true;
    xenia_mock.poll_running = true;
}

static void initialize_runtime(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_media_backend backend
)
{
    memset(runtime, 0, sizeof(*runtime));
    memset(snapshot, 0, sizeof(*snapshot));
    check(gdox_mutex_init(&runtime->mutex), "initialize runtime mutex");
    runtime->media.open = true;
    runtime->media.info.backend = backend;
    runtime->bundle.xemu_available = true;
    runtime->bundle.configuration_ready = true;
    runtime->bundle.mcpx_ready = true;
    runtime->bundle.flash_ready = true;
    runtime->bundle.hdd_ready = true;
    snapshot->phase = GDOX_RUNTIME_READY;
    snapshot->media_source = GDOX_MEDIA_DISC_IMAGE;
    snapshot->media_backend = backend;
    snapshot->xemu_ready = true;
    snapshot->xenia_ready = true;
}

static void destroy_runtime(gdox_runtime *runtime)
{
    gdox_mutex_destroy(&runtime->mutex);
}

bool gdox_optical_drive_can_eject(gdox_optical_drive drive)
{
    (void)drive;
    return false;
}

bool gdox_runtime_xemu_start(gdox_runtime *runtime, gdox_error *error)
{
    ++xemu_mock.starts;
    if (xemu_mock.start_sets_process) {
        runtime->xemu = &xemu_process;
    }
    if (!xemu_mock.start_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "xemu start failed");
    }
    return xemu_mock.start_success;
}

bool gdox_runtime_xemu_prepare_launch(
    gdox_runtime *runtime,
    gdox_error *error
)
{
    ++xemu_mock.prepares;
    runtime->xemu_save_migration = xemu_mock.migration_outcome;
    xemu_mock.preparation_was_visible =
        runtime->snapshot.phase == GDOX_RUNTIME_PREPARING
        && strcmp(
            runtime->snapshot.status,
            "Preparing Original Xbox playback"
        ) == 0
        && strcmp(
            runtime->snapshot.notice,
            "Checking persistent Xbox saved games"
        ) == 0
        && !runtime->snapshot.can_start
        && !runtime->snapshot.can_restart
        && !runtime->snapshot.can_close
        && !runtime->snapshot.can_eject;
    if (!xemu_mock.prepare_success) {
        gdox_error_set(
            error, GDOX_ERROR_IO, "legacy Xbox save migration failed"
        );
    }
    return xemu_mock.prepare_success;
}

static void test_xemu_retained_source_notice(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    xemu_mock.migration_outcome = (gdox_xemu_legacy_migration_outcome){
        .legacy_found = true,
        .retained_due_to_unclassified = true,
        .unclassified_tdata_entries = 2U,
        .unclassified_tdata_bytes = 4096U,
    };
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    check(
        gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "start xemu with retained legacy save source"
    );
    check(
        strcmp(
            snapshot.notice,
            "Saved games ready. Legacy Xbox hard disk kept because 2 title-data files (4096 bytes) could not be safely classified."
        ) == 0,
        "report retained legacy source without blocking playback"
    );
    check(
        snapshot.phase == GDOX_RUNTIME_PLAYING,
        "keep retained-source outcome nonblocking"
    );
    check(
        gdox_runtime_playback_stop(&runtime, &snapshot, &error),
        "stop xemu after retained-source notice"
    );
    destroy_runtime(&runtime);
}

static void test_xemu_save_conflict_notice(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    xemu_mock.migration_outcome = (gdox_xemu_legacy_migration_outcome){
        .legacy_found = true,
        .retained_due_to_save_conflict = true,
    };
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    check(
        gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "start xemu with conflicting legacy saves"
    );
    check(
        strcmp(
            snapshot.notice,
            "Saved games ready. Legacy Xbox hard disk kept because some legacy saves conflict with existing saved games."
        ) == 0,
        "report a retained legacy source with conflicting saves"
    );
    check(
        snapshot.phase == GDOX_RUNTIME_PLAYING,
        "keep save-conflict outcome nonblocking"
    );
    check(
        gdox_runtime_playback_stop(&runtime, &snapshot, &error),
        "stop xemu after save-conflict notice"
    );
    destroy_runtime(&runtime);
}

bool gdox_runtime_xemu_stop(gdox_runtime *runtime, gdox_error *error)
{
    ++xemu_mock.stops;
    if (!xemu_mock.stop_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "xemu stop failed");
        return false;
    }
    gdox_emulator_process_destroy(runtime->xemu);
    runtime->xemu = NULL;
    return true;
}

bool gdox_runtime_xemu_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    ++xemu_mock.polls;
    if (!xemu_mock.poll_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "xemu poll failed");
        return false;
    }
    *running = xemu_mock.poll_running;
    *exit_code = xemu_mock.poll_exit_code;
    if (!*running) {
        gdox_emulator_process_destroy(runtime->xemu);
        runtime->xemu = NULL;
    }
    return true;
}

void gdox_emulator_process_destroy(gdox_emulator_process *process)
{
    if (process != NULL) {
        ++xemu_mock.destroys;
    }
}

bool gdox_runtime_xenia_prepare(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    (void)runtime;
    (void)snapshot;
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_xenia_start(gdox_runtime *runtime, gdox_error *error)
{
    ++xenia_mock.starts;
    if (xenia_mock.start_sets_process) {
        runtime->xenia = &xenia_process;
    }
    if (!xenia_mock.start_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "Xenia start failed");
    }
    return xenia_mock.start_success;
}

bool gdox_runtime_xenia_stop(gdox_runtime *runtime, gdox_error *error)
{
    ++xenia_mock.stops;
    if (!xenia_mock.stop_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "Xenia stop failed");
        return false;
    }
    gdox_xenia_process_destroy(runtime->xenia);
    runtime->xenia = NULL;
    return true;
}

bool gdox_runtime_xenia_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    ++xenia_mock.polls;
    if (!xenia_mock.poll_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "Xenia poll failed");
        return false;
    }
    *running = xenia_mock.poll_running;
    *exit_code = xenia_mock.poll_exit_code;
    if (!*running) {
        gdox_xenia_process_destroy(runtime->xenia);
        runtime->xenia = NULL;
    }
    return true;
}

void gdox_xenia_process_destroy(gdox_xenia_process *process)
{
    if (process != NULL) {
        ++xenia_mock.destroys;
    }
}

static void test_overlapping_text_copy(void)
{
    char text[8] = "abcdef";

    gdox_runtime_copy_text(text, sizeof(text), text);
    check(strcmp(text, "abcdef") == 0, "copy text onto itself");
    gdox_runtime_copy_text(text + 1, sizeof(text) - 1U, text);
    check(strcmp(text, "aabcdef") == 0, "copy overlapping text");
}

static void test_media_open_result_presentation(void)
{
    static const gdox_xenia_launch_policy policy = {0};
    gdox_runtime_snapshot snapshot = {0};
    gdox_runtime_media_open_result result = {0};

    result.state = GDOX_RUNTIME_MEDIA_IDENTIFIED;
    result.info.source = GDOX_MEDIA_PHYSICAL_DISC;
    result.info.platform = GDOX_MEDIA_PLATFORM_XBOX_360;
    result.info.backend = GDOX_MEDIA_BACKEND_XENIA;
    result.info.source_sectors = 25063U;
    result.info.game_partition_lba = 24U;
    result.info.x360.layout = GDOX_X360_IMAGE_LAYOUT_FB20;
    result.info.x360.execution.valid = true;
    result.info.x360.execution.title_id = UINT32_C(0x555308ae);
    result.info.xenia_policy = &policy;
    (void)snprintf(
        result.info.title, sizeof(result.info.title), "Xbox 360 title 555308AE"
    );

    check(
        gdox_runtime_apply_media_open_result(&snapshot, &result),
        "apply identified media result"
    );
    check(
        snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360
            && snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA,
        "present identified Xbox 360 media with Xenia"
    );
    check(
        snapshot.x360_image_layout == GDOX_X360_IMAGE_LAYOUT_FB20
            && snapshot.x360_execution.title_id == UINT32_C(0x555308ae)
            && snapshot.xenia_policy == &policy,
        "retain identified Xbox 360 details"
    );
    check(
        strcmp(snapshot.disc, "Xbox 360 title 555308AE") == 0,
        "present identified Xbox 360 title"
    );
    (void)snprintf(
        snapshot.notice,
        sizeof(snapshot.notice),
        "Xenia bridge is temporarily unavailable"
    );
    gdox_runtime_mark_drive_unavailable(
        &snapshot, "Connect the supported USB optical drive"
    );
    check(
        snapshot.phase == GDOX_RUNTIME_ATTENTION
            && snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360
            && snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA,
        "retain Xenia identity across transient drive loss"
    );
    check(
        strcmp(snapshot.disc, "Xbox 360 title 555308AE") == 0
            && snapshot.xenia_policy == &policy
            && strcmp(
                snapshot.notice,
                "Xenia bridge is temporarily unavailable"
            ) == 0,
        "retain Xbox 360 presentation across transient drive loss"
    );

    result.info.backend = GDOX_MEDIA_BACKEND_XEMU;
    check(
        !gdox_runtime_apply_media_open_result(&snapshot, &result)
            && snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA,
        "reject inconsistent platform and backend identity"
    );

    snapshot.physical_read_commands = 7U;
    snapshot.physical_read_sectors = 9U;
    snapshot.nbd_read_stats.requests = 11U;
    snapshot.nbd_read_stats.served_without_drive_io_requests = 5U;
    (void)snprintf(
        snapshot.disc_image_path,
        sizeof(snapshot.disc_image_path),
        "stale-image.iso"
    );
    gdox_runtime_reset_media_snapshot(
        &snapshot, GDOX_MEDIA_PHYSICAL_DISC
    );
    check(
        snapshot.media_platform == GDOX_MEDIA_PLATFORM_NONE
            && snapshot.media_backend == GDOX_MEDIA_BACKEND_NONE
            && snapshot.xenia_policy == NULL,
        "clear identified backend when physical media resets"
    );
    check(
        snapshot.physical_read_commands == 0U
            && snapshot.physical_read_sectors == 0U
            && snapshot.nbd_read_stats.requests == 0U
            && snapshot.nbd_read_stats.served_without_drive_io_requests == 0U
            && snapshot.disc_image_path[0] == '\0',
        "clear media counters and stale image path"
    );
}

static void test_media_open_retry_policy(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XENIA);
    snapshot.media_platform = GDOX_MEDIA_PLATFORM_XBOX_360;
    gdox_error_set(
        &error,
        GDOX_ERROR_UNSUPPORTED,
        "Xbox 360 playback is unavailable on this platform"
    );
    gdox_runtime_attention(
        &runtime,
        &snapshot,
        "Could not prepare disc",
        &error,
        false,
        false
    );
    snapshot.can_start = gdox_runtime_media_open_can_retry(
        error.code, false, true
    );
    check(
        snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360
            && snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA,
        "retain Xbox 360 identity after unsupported preflight"
    );
    check(
        !snapshot.can_start && !snapshot.can_restart,
        "disable playback controls after unsupported preflight"
    );
    check(
        gdox_runtime_media_open_can_retry(GDOX_ERROR_NOT_FOUND, false, true)
            && gdox_runtime_media_open_can_retry(
                GDOX_ERROR_TRANSPORT, false, true
            )
            && gdox_runtime_media_open_can_retry(
                GDOX_ERROR_IO, false, true
            ),
        "allow recoverable media-open retries"
    );
    check(
        !gdox_runtime_media_open_can_retry(
            GDOX_ERROR_INVALID_VOLUME, false, true
        )
            && !gdox_runtime_media_open_can_retry(
                GDOX_ERROR_IO, true, true
            )
            && !gdox_runtime_media_open_can_retry(
                GDOX_ERROR_IO, false, false
            ),
        "reject nonrecoverable or unsafe media-open retries"
    );
    destroy_runtime(&runtime);
}

static void test_xemu_owner_and_exclusion(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;
    bool running = false;
    int exit_code = -1;

    reset_mocks();
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    check(
        gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "start xemu owner"
    );
    check(xemu_mock.prepares == 1U, "prepare xemu saves before launch");
    check(
        xemu_mock.preparation_was_visible,
        "publish disabled preparation state before save migration"
    );
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_XEMU,
        "tag xemu owner"
    );
    check(gdox_runtime_playback_running(&runtime), "report xemu running");
    check(snapshot.phase == GDOX_RUNTIME_PLAYING, "publish playing phase");

    runtime.media.info.backend = GDOX_MEDIA_BACKEND_XENIA;
    snapshot.media_backend = GDOX_MEDIA_BACKEND_XENIA;
    check(
        !gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "reject second backend start"
    );
    check(xenia_mock.starts == 0U, "do not dispatch second backend");

    check(
        gdox_runtime_playback_poll(
            &runtime, &running, &exit_code, &error
        ),
        "poll tagged xemu owner"
    );
    check(running, "xemu remains running");
    check(xemu_mock.polls == 1U, "dispatch poll to xemu");
    check(xenia_mock.polls == 0U, "do not poll Xenia");

    check(
        gdox_runtime_playback_stop(&runtime, &snapshot, &error),
        "stop tagged xemu owner"
    );
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_NONE,
        "clear xemu owner"
    );
    check(!gdox_runtime_playback_running(&runtime), "report xemu stopped");
    destroy_runtime(&runtime);
}

static void test_xemu_migration_failure_retains_source_and_starts(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    xemu_mock.migration_outcome = (gdox_xemu_legacy_migration_outcome){
        .legacy_found = true,
        .retained_due_to_rejected_migration = true,
    };
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    check(
        gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "start xemu after preserving an unmigrated legacy HDD"
    );
    check(
        strstr(snapshot.notice, "migration was rejected") != NULL,
        "explain nonblocking legacy HDD preservation"
    );
    check(xemu_mock.starts == 1U, "launch xemu after migration fallback");
    destroy_runtime(&runtime);
}

static void test_xemu_preparation_failure_gates_launch(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    xemu_mock.prepare_success = false;
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    check(
        !gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "surface xemu save preparation failure"
    );
    check(xemu_mock.prepares == 1U, "attempt xemu save preparation once");
    check(xemu_mock.starts == 0U, "gate xemu launch on save preparation");
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_NONE
            && runtime.xemu == NULL,
        "leave playback ownership clear after preparation failure"
    );
    check(
        snapshot.phase == GDOX_RUNTIME_PREPARING
            && runtime.snapshot.phase == GDOX_RUNTIME_PREPARING,
        "retain published preparation state for caller attention"
    );
    check(
        error.code == GDOX_ERROR_IO
            && strcmp(
                error.message, "legacy Xbox save migration failed"
            ) == 0,
        "preserve save preparation failure reason"
    );
    destroy_runtime(&runtime);
}

static void test_xenia_poll_completion(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;
    bool running = true;
    int exit_code = -1;

    reset_mocks();
    xenia_mock.poll_running = false;
    xenia_mock.poll_exit_code = 7;
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XENIA);
    check(
        gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "start Xenia owner"
    );
    check(xemu_mock.prepares == 0U, "do not prepare xemu saves for Xenia");
    check(
        gdox_runtime_playback_poll(
            &runtime, &running, &exit_code, &error
        ),
        "poll completed Xenia owner"
    );
    check(!running && exit_code == 7, "return Xenia exit status");
    check(xenia_mock.polls == 1U, "dispatch poll to Xenia");
    check(xemu_mock.polls == 0U, "do not poll xemu");
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_NONE,
        "clear completed Xenia owner"
    );
    destroy_runtime(&runtime);
}

static void test_stop_failure_retains_owner_until_retry(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    xenia_mock.stop_success = false;
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XENIA);
    check(
        gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "start Xenia before stop failure"
    );
    check(
        !gdox_runtime_playback_stop(&runtime, &snapshot, &error),
        "surface Xenia stop failure"
    );
    check(gdox_runtime_playback_running(&runtime), "retain failed stop owner");
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_XENIA,
        "retain Xenia tag after stop failure"
    );
    check(
        !gdox_runtime_playback_shutdown(&runtime, &error),
        "report forced shutdown"
    );
    check(error.code == GDOX_ERROR_IO, "retain shutdown failure reason");
    check(gdox_runtime_playback_running(&runtime), "retain failed owner");
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_XENIA,
        "retain forced Xenia owner"
    );
    check(xenia_mock.destroys == 0U, "do not discard a live Xenia process");
    xenia_mock.stop_success = true;
    check(
        gdox_runtime_playback_shutdown(&runtime, &error),
        "retry Xenia shutdown"
    );
    check(!gdox_runtime_playback_running(&runtime), "dispose stopped owner");
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_NONE,
        "clear stopped Xenia owner"
    );
    check(xenia_mock.destroys == 1U, "destroy stopped Xenia process");
    destroy_runtime(&runtime);
}

static void test_failed_start_retains_process_authority(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    xemu_mock.start_success = false;
    xemu_mock.stop_success = false;
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    check(
        !gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "surface failed xemu start"
    );
    check(
        runtime.playback_owner == GDOX_RUNTIME_PLAYBACK_XEMU,
        "retain process created by failed start"
    );
    check(
        gdox_runtime_playback_running(&runtime),
        "report process created by failed start"
    );
    check(
        !gdox_runtime_playback_start(&runtime, &snapshot, &error),
        "reject retry while failed start is owned"
    );
    check(xemu_mock.starts == 1U, "do not dispatch unsafe retry");
    check(
        !gdox_runtime_playback_shutdown(&runtime, &error),
        "retain failed start process when shutdown fails"
    );
    check(xemu_mock.destroys == 0U, "do not discard failed start process");
    check(gdox_runtime_playback_running(&runtime), "retain failed start owner");
    xemu_mock.stop_success = true;
    check(
        gdox_runtime_playback_shutdown(&runtime, &error),
        "retry shutdown of failed start process"
    );
    check(xemu_mock.destroys == 1U, "destroy stopped failed start process");
    check(!gdox_runtime_playback_running(&runtime), "clear stopped start owner");
    destroy_runtime(&runtime);
}

static void test_xenia_state_reset(void)
{
    static const gdox_xenia_launch_policy policy = {0};
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;

    reset_mocks();
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XENIA);
    runtime.xenia_runtime.definition = policy.runtime;
    runtime.xenia_runtime.payload[0] = 'x';
    snapshot.bundled_xenia = true;
    snapshot.xenia_policy = &policy;
    (void)snprintf(
        snapshot.xenia_executable,
        sizeof(snapshot.xenia_executable),
        "xenia_canary.exe"
    );
    (void)snprintf(
        snapshot.xenia_setup, sizeof(snapshot.xenia_setup), "Xenia is ready"
    );

    gdox_runtime_playback_reset_xenia(&runtime, &snapshot);
    check(!snapshot.xenia_ready, "clear Xenia readiness");
    check(!snapshot.bundled_xenia, "clear Xenia provenance");
    check(snapshot.xenia_policy == NULL, "clear Xenia policy");
    check(snapshot.xenia_executable[0] == '\0', "clear Xenia executable");
    check(snapshot.xenia_setup[0] == '\0', "clear Xenia setup status");
    check(
        runtime.xenia_runtime.definition == NULL
            && runtime.xenia_runtime.payload[0] == '\0',
        "clear Xenia runtime descriptor"
    );
    destroy_runtime(&runtime);
}

static void test_untagged_process_is_retained(void)
{
    gdox_runtime runtime;
    gdox_runtime_snapshot snapshot;
    gdox_error error;

    reset_mocks();
    initialize_runtime(&runtime, &snapshot, GDOX_MEDIA_BACKEND_XEMU);
    runtime.xemu = &xemu_process;
    check(
        gdox_runtime_playback_running(&runtime),
        "treat untagged process as owned"
    );
    check(
        !gdox_runtime_playback_shutdown(&runtime, &error),
        "refuse type-unsafe untagged shutdown"
    );
    check(runtime.xemu == &xemu_process, "retain untagged process authority");
    runtime.xemu = NULL;
    destroy_runtime(&runtime);
}

int main(void)
{
    check(
        GDOX_XEMU_ORDERLY_STOP_GRACE_MS == UINT32_C(15000),
        "allow the bounded xemu save checkpoint to finish"
    );
    test_overlapping_text_copy();
    test_media_open_result_presentation();
    test_media_open_retry_policy();
    test_xemu_owner_and_exclusion();
    test_xemu_migration_failure_retains_source_and_starts();
    test_xemu_retained_source_notice();
    test_xemu_save_conflict_notice();
    test_xemu_preparation_failure_gates_launch();
    test_xenia_poll_completion();
    test_stop_failure_retains_owner_until_retry();
    test_failed_start_retains_process_authority();
    test_xenia_state_reset();
    test_untagged_process_is_retained();
    return failures == 0 ? 0 : 1;
}
