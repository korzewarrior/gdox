#include "app/runtime_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int failures;
static unsigned int preparation_calls;
static unsigned int import_calls;
static unsigned int save_calls;
static bool preparation_succeeds;
static bool import_succeeds;
static bool save_succeeds = true;
static bool edit_during_save;
static gdox_preferences saved;
static gdox_runtime *active_runtime;

static void check(bool condition, const char *message)
{
    if (!condition) {
        ++failures;
        (void)fprintf(stderr, "%s\n", message);
    }
}

bool gdox_optical_drive_can_eject(gdox_optical_drive drive)
{
    (void)drive;
    return false;
}

bool gdox_preferences_save(const gdox_preferences *preferences, gdox_error *error)
{
    ++save_calls;
    check(gdox_mutex_lock(&active_runtime->mutex), "preference storage does not hold snapshot lock");
    if (edit_during_save) {
        active_runtime->snapshot.settings.auto_start = !preferences->auto_start;
        gdox_runtime_setup_mark_preferences_dirty(active_runtime);
        edit_during_save = false;
    }
    gdox_mutex_unlock(&active_runtime->mutex);
    if (!save_succeeds) {
        gdox_error_set(error, GDOX_ERROR_IO, "injected preference failure");
        return false;
    }
    saved = *preferences;
    return true;
}

bool gdox_runtime_bundle_prepare(const char *selection,
    gdox_runtime_bundle_status *bundle, gdox_error *error)
{
    ++preparation_calls;
    check(strcmp(saved.xemu_override, selection) == 0,
        "requested choice is durable before slow preparation starts");
    /* Slow preparation must leave snapshots available to the render thread. */
    check(gdox_mutex_lock(&active_runtime->mutex), "preparation can read unlocked runtime");
    gdox_mutex_unlock(&active_runtime->mutex);
    memset(bundle, 0, sizeof(*bundle));
    bundle->mcpx_ready = true;
    bundle->flash_ready = true;
    gdox_runtime_copy_text(bundle->mcpx, sizeof(bundle->mcpx), "managed/mcpx.bin");
    gdox_runtime_copy_text(bundle->flash, sizeof(bundle->flash), "managed/bios.bin");
    check(strcmp(selection, GDOX_XEMU_INCLUDED_SELECTION) == 0,
        "worker prepares the requested included selection");
    gdox_runtime_copy_text(bundle->executable, sizeof(bundle->executable),
        "new-package/runtime/xemu/xemu.exe");
    if (!preparation_succeeds) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "included xemu was not found at expected path");
        bundle->setup_error = *error;
        return false;
    }
    bundle->xemu_available = true;
    bundle->configuration_ready = true;
    bundle->hdd_ready = true;
    bundle->full_hdd_isolation = true;
    bundle->persistent_save_export = true;
    bundle->bundled = true;
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_bundle_import_firmware(gdox_firmware_kind kind,
    const char *path, const char *selection, gdox_runtime_bundle_status *bundle,
    gdox_error *error)
{
    ++import_calls;
    check(strcmp(selection, GDOX_XEMU_INCLUDED_SELECTION) == 0,
        "firmware worker uses the current persisted selection");
    check(path[0] != '\0', "firmware request owns its path");
    if (!import_succeeds) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "injected invalid BIOS");
        return false;
    }
    if (kind == GDOX_FIRMWARE_MCPX) bundle->mcpx_ready = true;
    else bundle->flash_ready = true;
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_bundle_import_firmware_auto(const char *path,
    const char *selection, gdox_firmware_kind *kind,
    gdox_runtime_bundle_status *bundle, gdox_error *error)
{
    *kind = GDOX_FIRMWARE_FLASH;
    return gdox_runtime_bundle_import_firmware(*kind, path, selection, bundle, error);
}

static void execute_next(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot)
{
    gdox_runtime_request_entry request;
    check(gdox_runtime_setup_take_request(runtime, &request), "worker takes setup request");
    check(gdox_runtime_setup_execute(runtime, snapshot, &request), "worker handles setup request");
}

static void test_requested_choice(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot)
{
    gdox_runtime_copy_text(runtime->snapshot.settings.xemu_override,
        sizeof(runtime->snapshot.settings.xemu_override), "Desktop/xemu.exe");
    gdox_runtime_copy_text(runtime->bundle.executable, sizeof(runtime->bundle.executable),
        "Desktop/xemu.exe");
    gdox_error_set(&runtime->bundle.setup_error, GDOX_ERROR_UNSUPPORTED,
        "old custom capability failure");
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_SET_XEMU,
        GDOX_XEMU_INCLUDED_SELECTION), "unavailable requested selection can be queued");
    check(preparation_calls == 0U, "UI submission never executes slow preparation");
    check(save_calls == 0U, "UI submission performs no preference storage I/O");
    check(runtime->setup_request_pending && !runtime->snapshot.xemu_ready,
        "pending setup cannot launch the old executable");
    gdox_runtime_publish(runtime, snapshot);
    check(strstr(runtime->snapshot.xemu_setup, "Preparing") != NULL,
        "stale worker publication keeps pending setup visible");
    check(!gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_SET_XEMU, "other.exe"),
        "another executable change cannot race pending preparation");
    execute_next(runtime, snapshot);
    check(!runtime->setup_request_pending && preparation_calls == 1U,
        "worker finishes failed selection once");
    check(strcmp(saved.xemu_override, GDOX_XEMU_INCLUDED_SELECTION) == 0,
        "failed preparation still retains the durable requested choice");
    check(strcmp(runtime->snapshot.settings.xemu_override, GDOX_XEMU_INCLUDED_SELECTION) == 0,
        "failure never reverts desired selection to Desktop xemu");
    check(strstr(runtime->snapshot.xemu_executable, "new-package") != NULL
        && strstr(runtime->snapshot.xemu_setup, "included xemu was not found") != NULL,
        "failed setup publishes one coherent desired executable and actual error");
    check(runtime->snapshot.mcpx_ready && runtime->snapshot.flash_ready,
        "failed runtime selection preserves imported firmware state");
    gdox_runtime_copy_text(snapshot->xemu_setup, sizeof(snapshot->xemu_setup), "stale error");
    gdox_runtime_publish(runtime, snapshot);
    check(strstr(runtime->snapshot.xemu_setup, "included xemu was not found") != NULL,
        "later media publications retain the current setup failure");
    preparation_succeeds = true;
    runtime->media.open = true;
    snapshot->media_source = GDOX_MEDIA_PHYSICAL_DISC;
    snapshot->media_platform = GDOX_MEDIA_PLATFORM_XBOX;
    snapshot->media_backend = GDOX_MEDIA_BACKEND_XEMU;
    snapshot->can_preserve = true;
    runtime->snapshot.can_preserve = true;
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_SET_XEMU,
        GDOX_XEMU_INCLUDED_SELECTION), "repaired runtime can be retried");
    execute_next(runtime, snapshot);
    check(runtime->snapshot.xemu_ready && runtime->snapshot.bundled_xemu,
        "retry recovers without reimporting firmware");
    check(runtime->snapshot.can_preserve && runtime->snapshot.can_start,
        "successful setup restores launch controls for the ready disc");
    snapshot->media_source = GDOX_MEDIA_DISC_IMAGE;
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_SET_XEMU,
        GDOX_XEMU_INCLUDED_SELECTION), "retry setup while a disc image is ready");
    execute_next(runtime, snapshot);
    check(runtime->snapshot.can_start && !runtime->snapshot.can_preserve,
        "disc image launch recovers even though image preservation is unavailable");
}

static void test_firmware_and_priority(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot)
{
    gdox_runtime_request_entry request = {.kind = GDOX_RUNTIME_REQUEST_START};
    gdox_runtime_request_entry output;
    char path[] = "selected-bios.bin";
    runtime->requests.head = GDOX_RUNTIME_REQUEST_CAPACITY - 1U;
    check(gdox_runtime_request_enqueue(&runtime->requests, &request), "queue older drive start");
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_BIOS, path),
        "queue firmware behind a stalled drive action");
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_MCPX, "mcpx.bin"),
        "two dropped firmware files are accepted in order");
    path[0] = 'X';
    check(import_calls == 0U, "UI does not validate or read firmware synchronously");
    check(gdox_runtime_setup_take_request(runtime, &output)
        && output.kind == GDOX_RUNTIME_REQUEST_IMPORT_BIOS
        && strcmp(output.path, "selected-bios.bin") == 0,
        "Sources priority retains copied path across wrapped queue");
    check(gdox_runtime_setup_execute(runtime, snapshot, &output), "worker reports invalid firmware");
    check(runtime->setup_request_pending, "second firmware request keeps setup pending");
    check(runtime->bundle.mcpx_ready && runtime->bundle.flash_ready,
        "invalid replacement leaves existing managed firmware intact");
    import_succeeds = true;
    execute_next(runtime, snapshot);
    check(!runtime->setup_request_pending && runtime->snapshot.xemu_ready,
        "last firmware result restores coherent ready status");
    check(gdox_runtime_request_dequeue(&runtime->requests, &output)
        && output.kind == GDOX_RUNTIME_REQUEST_START && runtime->requests.count == 0U,
        "priority Sources request neither consumes nor reorders old drive action");
    import_succeeds = false;
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_BIOS, "bad.bin"),
        "queue invalid replacement independently");
    execute_next(runtime, snapshot);
    check(strstr(runtime->snapshot.notice, "injected invalid BIOS") != NULL
        && runtime->snapshot.xemu_ready, "invalid firmware error is visible without disabling valid setup");
}

static void test_rejection(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot)
{
    const unsigned int before = preparation_calls;
    save_succeeds = false;
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_SET_XEMU, "lost.exe"),
        "storage failure is handled by worker after nonblocking submission");
    execute_next(runtime, snapshot);
    check(!runtime->setup_request_pending && preparation_calls == before,
        "failed durable commit never starts slow preparation");
    check(strcmp(runtime->snapshot.settings.xemu_override, "lost.exe") == 0
        && strstr(runtime->snapshot.xemu_setup, "Could not save xemu selection") != NULL,
        "save failure leaves requested choice and actual error coherent");
    check(strcmp(saved.xemu_override, GDOX_XEMU_INCLUDED_SELECTION) == 0,
        "failed durable commit does not claim the disk selection changed");
    {
        const unsigned int attempts = save_calls;
        gdox_error error;
        check(gdox_runtime_setup_flush_preferences(runtime, false, &error)
            && save_calls == attempts && runtime->preferences_dirty,
            "failed settings save does not spin retries during idle polling");
        check(!gdox_runtime_setup_flush_preferences(runtime, true, &error)
            && save_calls == attempts + 1U,
            "shutdown attempts to preserve accepted unsaved settings once");
    }
    save_succeeds = true;
    runtime->snapshot.can_close = true;
    check(!gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_BIOS, "bios.bin"),
        "playing runtime rejects firmware mutation");
    runtime->snapshot.can_close = false;
    atomic_store(&runtime->stopping, true);
    check(!gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_SET_XEMU, "stopped.exe"),
        "closing runtime rejects new preparation");
    check(preparation_calls == before, "rejected requests never run preparation");
}

static void test_latest_preferences(gdox_runtime *runtime)
{
    gdox_error error;
    runtime->snapshot.settings.auto_start = false;
    gdox_runtime_setup_mark_preferences_dirty(runtime);
    edit_during_save = true;
    check(gdox_runtime_setup_flush_preferences(runtime, false, &error),
        "worker completes the first settings write");
    check(!saved.auto_start && runtime->snapshot.settings.auto_start
        && runtime->preferences_dirty && runtime->preferences_save_requested,
        "UI edit during storage I/O remains pending instead of being erased");
    check(gdox_runtime_setup_flush_preferences(runtime, false, &error)
        && saved.auto_start && !runtime->preferences_dirty,
        "next worker attempt persists the latest UI state");
}

static void test_playback_started_after_submission(gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot)
{
    const unsigned int before = import_calls;
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_BIOS, "bios.bin"),
        "firmware request can be accepted before playback starts");
    runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_XENIA;
    execute_next(runtime, snapshot);
    check(import_calls == before && strstr(runtime->snapshot.notice, "Close playback") != NULL,
        "worker rechecks playback before mutating firmware or starting helpers");
    check(!runtime->setup_request_pending, "execution-time rejection clears pending state");
    runtime->playback_owner = GDOX_RUNTIME_PLAYBACK_NONE;
}

static void test_multiple_auto_drops(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot)
{
    gdox_runtime_request_entry request;
    import_succeeds = true;
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_FIRMWARE, "first.bin"),
        "first auto-detected drop is accepted");
    check(gdox_runtime_setup_submit(runtime, GDOX_RUNTIME_REQUEST_IMPORT_FIRMWARE, "second.bin"),
        "second auto-detected drop is accepted");
    check(runtime->requests.count == 2U, "consecutive firmware drops are never coalesced");
    check(gdox_runtime_setup_take_request(runtime, &request)
        && strcmp(request.path, "first.bin") == 0, "first dropped path survives second drop");
    check(gdox_runtime_setup_execute(runtime, snapshot, &request)
        && runtime->setup_request_pending, "first drop leaves second import pending");
    check(gdox_runtime_setup_take_request(runtime, &request)
        && strcmp(request.path, "second.bin") == 0, "second dropped path retains order");
    check(gdox_runtime_setup_execute(runtime, snapshot, &request)
        && !runtime->setup_request_pending, "all dropped firmware files finish");
}

int main(void)
{
    gdox_runtime *runtime = calloc(1U, sizeof(*runtime));
    gdox_runtime_snapshot *snapshot = calloc(1U, sizeof(*snapshot));
    if (runtime == NULL || snapshot == NULL || !gdox_mutex_init(&runtime->mutex)) return 1;
    active_runtime = runtime;
    atomic_init(&runtime->stopping, false);
    test_requested_choice(runtime, snapshot);
    test_firmware_and_priority(runtime, snapshot);
    test_multiple_auto_drops(runtime, snapshot);
    test_latest_preferences(runtime);
    test_playback_started_after_submission(runtime, snapshot);
    test_rejection(runtime, snapshot);
    gdox_mutex_destroy(&runtime->mutex);
    free(snapshot);
    free(runtime);
    return failures == 0U ? 0 : 1;
}
