/* Exercise the actual worker cycle and public app callbacks with no hardware.
 * Including the worker also lets the test stop at one deterministic cycle. */
#include "app/runtime.c"
#include "app/app.h"

#include <stdio.h>

static atomic_bool inventory_entered;
static atomic_bool inventory_release;
static atomic_bool close_entered;
static atomic_bool close_release;
static atomic_bool cycle_finished;
static atomic_uint inventory_calls;
static atomic_uint open_calls;
static atomic_uint close_attempts;
static bool fail_first_close;
static bool block_inventory;
static gdox_optical_device fake_device;
static unsigned int failures;
static const char *config_home;

#define CHECK(condition) do { if (!(condition)) { \
    (void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    ++failures; \
} } while (false)

static bool set_config_home(const char *path)
{
#if defined(_WIN32)
    return SetEnvironmentVariableA("GDOX_CONFIG_HOME", path) != 0;
#else
    return setenv("GDOX_CONFIG_HOME", path, 1) == 0;
#endif
}

static void seed_saved_preferences(void)
{
    gdox_preferences preferences;
    gdox_error error;
    gdox_preferences_defaults(&preferences);
    CHECK(gdox_preferences_save(&preferences, &error));
}

static void check_saved_auto_start(bool expected)
{
    gdox_preferences preferences;
    gdox_error error;
    CHECK(gdox_preferences_load(&preferences, &error));
    CHECK(preferences.auto_start == expected);
}

static bool wait_until(const atomic_bool *flag)
{
    const uint64_t deadline = gdox_monotonic_ms() + 2000U;
    while (!atomic_load_explicit(flag, memory_order_acquire)) {
        if (gdox_monotonic_ms() >= deadline) return false;
        gdox_sleep_ms(1U);
    }
    return true;
}

bool gdox_optical_list_devices_filtered(gdox_optical_device *devices,
    size_t capacity, size_t *count, const gdox_optical_media_query *query,
    gdox_error *error)
{
    (void)query;
    atomic_fetch_add_explicit(&inventory_calls, 1U, memory_order_relaxed);
    atomic_store_explicit(&inventory_entered, true, memory_order_release);
    while (block_inventory
        && !atomic_load_explicit(&inventory_release, memory_order_acquire)) {
        gdox_sleep_ms(1U);
    }
    CHECK(capacity >= 1U);
    devices[0] = fake_device;
    *count = 1U;
    gdox_error_clear(error);
    return true;
}

bool gdox_optical_list_devices(gdox_optical_device *devices, size_t capacity,
    size_t *count, bool query_media, gdox_error *error)
{
    const gdox_optical_media_query query = {.enabled = query_media};
    return gdox_optical_list_devices_filtered(devices, capacity, count, &query, error);
}

bool gdox_optical_device_connected(const gdox_optical_device *device,
    bool *connected, gdox_error *error)
{
    (void)device;
    *connected = true;
    gdox_error_clear(error);
    return true;
}

bool gdox_optical_open_device_media(const gdox_optical_device *device,
    uint8_t speed, uint32_t timeout, gdox_sector_source *source,
    gdox_optical_media_info *info, gdox_error *error)
{
    (void)device; (void)speed; (void)timeout; (void)source; (void)info;
    atomic_fetch_add_explicit(&open_calls, 1U, memory_order_relaxed);
    gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "injected absent disc");
    return false;
}

bool gdox_optical_eject_device(const gdox_optical_device *device, gdox_error *error)
{
    (void)device;
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "test never ejects hardware");
    return false;
}

bool gdox_optical_complete_device_eject_request(const gdox_optical_device *device,
    gdox_optical_eject_completion *completion, gdox_error *error)
{
    (void)device; (void)completion;
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "test never ejects hardware");
    return false;
}

static void initialize_fixture(void)
{
    atomic_init(&inventory_entered, false);
    atomic_init(&inventory_release, false);
    atomic_init(&close_entered, false);
    atomic_init(&close_release, false);
    atomic_init(&cycle_finished, false);
    atomic_init(&inventory_calls, 0U);
    atomic_init(&open_calls, 0U);
    atomic_init(&close_attempts, 0U);
    fail_first_close = false;
    block_inventory = false;
    fake_device = (gdox_optical_device){
        .drive = GDOX_OPTICAL_DRIVE_GP57,
        .accessible = true, .media_status_known = true,
    };
    (void)snprintf(fake_device.id, sizeof(fake_device.id), "test:drive");
    (void)snprintf(fake_device.name, sizeof(fake_device.name), "Fake optical drive");
}

static gdox_runtime *make_runtime(void)
{
    gdox_runtime *runtime = calloc(1U, sizeof(*runtime));
    if (runtime == NULL || !gdox_mutex_init(&runtime->mutex)) abort();
    atomic_init(&runtime->stopping, false);
    atomic_init(&runtime->worker_finished, false);
    atomic_init(&runtime->preservation_cancelled, false);
    gdox_preferences_defaults(&runtime->snapshot.settings);
    runtime->snapshot.phase = GDOX_RUNTIME_EMPTY;
    runtime->snapshot.can_select_drive = true;
    runtime->snapshot.optical_devices[0] = (gdox_app_optical_device){
        .device = fake_device, .connected = true,
    };
    runtime->snapshot.optical_device_count = 1U;
    return runtime;
}

typedef struct cycle_context {
    gdox_runtime *runtime;
    gdox_runtime_loop loop;
} cycle_context;

static void run_one_cycle(void *opaque)
{
    cycle_context *context = opaque;
    run_runtime_cycle(context->runtime, &context->loop);
    atomic_store_explicit(&cycle_finished, true, memory_order_release);
}

static void test_selection_precedes_inventory(void)
{
    initialize_fixture();
    gdox_runtime *runtime = make_runtime();
    gdox_app app = {.runtime = runtime};
    cycle_context *context = calloc(1U, sizeof(*context));
    gdox_thread worker;
    gdox_error error;
    if (context == NULL) abort();
    context->runtime = runtime;
    gdox_runtime_copy_snapshot(runtime, &context->loop.snapshot);
    block_inventory = true;
    CHECK(gdox_app_select_drive(&app, fake_device.id));
    CHECK(gdox_thread_start(&worker, run_one_cycle, context));
    const uint64_t deadline = gdox_monotonic_ms() + 2000U;
    while (!atomic_load_explicit(&cycle_finished, memory_order_acquire)
        && !atomic_load_explicit(&inventory_entered, memory_order_acquire)
        && gdox_monotonic_ms() < deadline) gdox_sleep_ms(1U);
    CHECK(atomic_load_explicit(&cycle_finished, memory_order_acquire));
    CHECK(atomic_load_explicit(&inventory_calls, memory_order_relaxed) == 0U);
    atomic_store_explicit(&inventory_release, true, memory_order_release);
    CHECK(gdox_thread_join(&worker));
    gdox_app_tick(&app);
    CHECK(strcmp(app.snapshot.settings.optical_device_id, fake_device.id) == 0);
    CHECK(gdox_app_shutdown(&app, &error));
    gdox_preferences saved;
    CHECK(gdox_preferences_load(&saved, &error));
    CHECK(strcmp(saved.optical_device_id, fake_device.id) == 0);
    free(context);
}

static void test_stop_during_inventory_starts_no_more_work(void)
{
    initialize_fixture();
    gdox_runtime *runtime = make_runtime();
    gdox_app app = {.runtime = runtime};
    cycle_context *context = calloc(1U, sizeof(*context));
    gdox_thread worker;
    gdox_error error;
    if (context == NULL) abort();
    context->runtime = runtime;
    gdox_runtime_copy_snapshot(runtime, &context->loop.snapshot);
    gdox_optical_monitor_retry(&context->loop.optical_monitor);
    fake_device.media_present = true;
    seed_saved_preferences();
    gdox_app_set_auto_start(&app, false);
    block_inventory = true;
    CHECK(gdox_thread_start(&worker, run_one_cycle, context));
    CHECK(wait_until(&inventory_entered));
    check_saved_auto_start(false);
    const uint64_t started = gdox_monotonic_ms();
    gdox_app_select_page(&app, GDOX_APP_PAGE_SOURCES);
    gdox_app_tick(&app);
    CHECK(gdox_app_select_drive(&app, fake_device.id));
    CHECK(app.snapshot.page == GDOX_APP_PAGE_SOURCES);
    CHECK(gdox_monotonic_ms() - started < 100U);
    /* This is the same stop flag published by public shutdown, before the
     * blocked command returns. No queued selection/probe may start afterward. */
    atomic_store_explicit(&runtime->stopping, true, memory_order_release);
    atomic_store_explicit(&inventory_release, true, memory_order_release);
    CHECK(gdox_thread_join(&worker));
    CHECK(runtime->snapshot.settings.optical_device_id[0] == '\0');
    CHECK(atomic_load_explicit(&open_calls, memory_order_relaxed) == 0U);
    CHECK(gdox_app_shutdown(&app, &error));
    free(context);
}

static uint64_t fake_sector_count(const void *context)
{
    (void)context;
    return 1U;
}

static bool fake_sector_read(void *context, uint64_t lba, uint32_t blocks,
    uint8_t *output, size_t bytes, gdox_error *error)
{
    (void)context; (void)lba; (void)blocks; (void)output; (void)bytes;
    gdox_error_set(error, GDOX_ERROR_IO, "test does not read disc data");
    return false;
}

static bool fake_present(const void *context)
{
    (void)context;
    return true;
}

static bool fake_close(void *context, gdox_error *error)
{
    (void)context;
    gdox_error_clear(error);
    return true;
}

static bool fake_prepare_close(void *context, gdox_error *error)
{
    (void)context;
    const unsigned int attempt = atomic_fetch_add_explicit(
        &close_attempts, 1U, memory_order_relaxed);
    if (fail_first_close && attempt == 0U) {
        gdox_error_set(error, GDOX_ERROR_IO, "injected restore error; reconnect the drive");
        return false;
    }
    atomic_store_explicit(&close_entered, true, memory_order_release);
    while (!atomic_load_explicit(&close_release, memory_order_acquire)) {
        gdox_sleep_ms(1U);
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops fake_source_ops = {
    fake_sector_count, fake_sector_read, fake_present, fake_close,
    NULL, NULL, NULL, fake_prepare_close, NULL,
};

static void test_shutdown_polls_existing_worker(bool pending, bool retry)
{
    initialize_fixture();
    gdox_runtime *runtime = make_runtime();
    gdox_app app = {.runtime = runtime};
    gdox_error error;
    seed_saved_preferences();
    gdox_app_set_auto_start(&app, false);
    fail_first_close = retry;
    runtime->pending_cleanup[0].device = fake_device;
    gdox_runtime_media_session *owner = pending
        ? &runtime->pending_cleanup[0].media : &runtime->media;
    owner->retained_source = (gdox_sector_source){
        .context = runtime, .ops = &fake_source_ops,
    };
    /* Start the actual worker already stopping: it must restore on that worker
     * while public app shutdown returns promptly and retains ownership. */
    atomic_store_explicit(&runtime->stopping, true, memory_order_release);
    CHECK(gdox_thread_start(&runtime->thread, runtime_thread, runtime));
    runtime->thread_started = true;
    CHECK(wait_until(&close_entered));
    check_saved_auto_start(false);
    const uint64_t started = gdox_monotonic_ms();
    CHECK(!gdox_app_shutdown(&app, &error));
    CHECK(gdox_monotonic_ms() - started < 100U);
    CHECK(app.runtime == runtime);
    CHECK(!atomic_load_explicit(&runtime->worker_finished, memory_order_acquire));
    gdox_app_tick(&app);
    CHECK(strcmp(app.snapshot.status, "Closing GDOX") == 0);
    CHECK(!app.snapshot.can_select_drive);
    CHECK(app.snapshot.pending_cleanup_count == (pending ? 1U : 0U));
    if (retry) {
        CHECK(strcmp(app.snapshot.notice,
            "injected restore error; reconnect the drive") == 0);
        CHECK(atomic_load_explicit(&close_attempts, memory_order_relaxed) == 2U);
    }
    CHECK(atomic_load_explicit(&inventory_calls, memory_order_relaxed) == 0U);
    atomic_store_explicit(&close_release, true, memory_order_release);
    CHECK(wait_until(&runtime->worker_finished));
    CHECK(gdox_app_shutdown(&app, &error));
    CHECK(app.runtime == NULL);
}

static void test_final_settings_failure_releases_ownership(void)
{
    char invalid_home[GDOX_STORAGE_PATH_CAPACITY];
    (void)snprintf(invalid_home, sizeof(invalid_home), "%s-blocked", config_home);
    FILE *blocked = fopen(invalid_home, "wb");
    CHECK(blocked != NULL);
    if (blocked == NULL) return;
    CHECK(fclose(blocked) == 0);
    CHECK(set_config_home(invalid_home));
    for (unsigned int scenario = 0U; scenario < 2U; ++scenario) {
        initialize_fixture();
        gdox_runtime *runtime = make_runtime();
        gdox_app app = {.runtime = runtime};
        gdox_error error;
        gdox_app_set_auto_start(&app, false);
        if (scenario == 0U) {
            runtime->terminal_shutdown_failed = true;
            gdox_error_set(&runtime->terminal_shutdown_error, GDOX_ERROR_IO,
                "injected checkpoint failure");
            atomic_store_explicit(&runtime->stopping, true, memory_order_release);
            CHECK(gdox_thread_start(&runtime->thread, runtime_thread, runtime));
            runtime->thread_started = true;
            CHECK(wait_until(&runtime->worker_finished));
            CHECK(runtime->shutdown_preferences_attempted && runtime->preferences_dirty);
        }
        CHECK(!gdox_app_shutdown(&app, &error));
        CHECK(app.runtime == NULL);
        CHECK(error.code != GDOX_ERROR_NONE);
        if (scenario == 0U) {
            CHECK(strcmp(error.message, "injected checkpoint failure") == 0);
        }
    }
    CHECK(set_config_home(config_home));
    CHECK(remove(invalid_home) == 0);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    config_home = argv[1];
    if (!set_config_home(config_home)) return 2;
    test_selection_precedes_inventory();
    test_stop_during_inventory_starts_no_more_work();
    test_shutdown_polls_existing_worker(false, false);
    test_shutdown_polls_existing_worker(true, false);
    test_shutdown_polls_existing_worker(true, true);
    test_final_settings_failure_releases_ownership();
    return failures == 0U ? 0 : 1;
}
