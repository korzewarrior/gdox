#include "app/runtime_xenia.h"
#include "app/xenia_process_stop.h"

#include <stdio.h>
#include <string.h>

struct gdox_xenia_process {
    int marker;
};

static struct gdox_xenia_process fixture_process;
static bool stop_success;
static int stop_exit_code;
static uint32_t observed_grace_ms;
static unsigned int destroy_calls;
static bool storage_close_success;
static unsigned int storage_close_calls;
static int failures;

static void check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(
            stderr, "Xenia process stop test failed: %s\n", message
        );
        ++failures;
    }
}

bool gdox_xenia_stop(
    gdox_xenia_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    (void)process;
    observed_grace_ms = grace_ms;
    *exit_code = stop_exit_code;
    if (!stop_success) {
        gdox_error_set(error, GDOX_ERROR_IO, "simulated Xenia stop failure");
    }
    return stop_success;
}

void gdox_xenia_process_destroy(gdox_xenia_process *process)
{
    if (process != NULL) {
        ++destroy_calls;
    }
}

bool gdox_xenia_storage_open(
    const gdox_xenia_launch_policy *policy,
    gdox_xenia_storage *storage,
    gdox_error *error
)
{
    (void)policy;
    (void)storage;
    gdox_error_clear(error);
    return true;
}

bool gdox_xenia_storage_close(
    gdox_xenia_storage *storage,
    gdox_error *error
)
{
    ++storage_close_calls;
    gdox_error_clear(error);
    if (!storage_close_success) {
        gdox_error_set(
            error, GDOX_ERROR_IO, "simulated Xenia storage close failure"
        );
        return false;
    }
    storage->session.active = false;
    return true;
}

bool gdox_xenia_resolve_runtime(
    const gdox_xenia_runtime *runtime,
    const char *override,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    (void)runtime;
    (void)override;
    (void)output;
    gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "unused runtime mock");
    return false;
}

bool gdox_xenia_launch(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_process **process,
    gdox_error *error
)
{
    (void)options;
    (void)target;
    (void)process;
    gdox_error_set(error, GDOX_ERROR_IO, "unused launch mock");
    return false;
}

bool gdox_xenia_poll(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    (void)process;
    (void)running;
    (void)exit_code;
    gdox_error_set(error, GDOX_ERROR_IO, "unused poll mock");
    return false;
}

bool gdox_runtime_media_prepare_xenia_target(
    gdox_runtime_media_session *session,
    gdox_xenia_target *target,
    gdox_error *error
)
{
    (void)session;
    (void)target;
    gdox_error_set(error, GDOX_ERROR_IO, "unused media mock");
    return false;
}

void gdox_runtime_copy_text(
    char *output,
    size_t capacity,
    const char *text
)
{
    size_t bytes;
    size_t copied;

    if (output == NULL || capacity == 0U) {
        return;
    }
    bytes = text != NULL ? strlen(text) : 0U;
    copied = bytes < capacity - 1U ? bytes : capacity - 1U;
    memmove(output, text != NULL ? text : "", copied);
    output[copied] = '\0';
}

static void reset_fixture(void)
{
    stop_success = true;
    stop_exit_code = 0;
    observed_grace_ms = 0U;
    destroy_calls = 0U;
    storage_close_success = true;
    storage_close_calls = 0U;
}

static void test_runtime_stop_contract(void)
{
    gdox_runtime runtime = {0};
    gdox_error error;

    reset_fixture();
    runtime.xenia = &fixture_process;
    runtime.xenia_storage.session.active = true;
    check(
        gdox_runtime_xenia_stop(&runtime, &error),
        "accept an orderly runtime stop"
    );
    check(runtime.xenia == NULL, "release an orderly runtime process");
    check(storage_close_calls == 1U, "close orderly transient storage");
    check(
        !runtime.xenia_storage.session.active,
        "release orderly transient storage ownership"
    );
    check(
        !runtime.terminal_shutdown_failed,
        "do not mark an orderly stop terminal"
    );

    reset_fixture();
    memset(&runtime, 0, sizeof(runtime));
    stop_exit_code = 1;
    runtime.xenia = &fixture_process;
    runtime.xenia_storage.session.active = true;
    check(
        !gdox_runtime_xenia_stop(&runtime, &error),
        "report a nonorderly runtime stop"
    );
    check(runtime.xenia == NULL, "release a nonzero runtime process");
    check(storage_close_calls == 1U, "close nonzero transient storage");
    check(
        !runtime.xenia_storage.session.active,
        "release nonzero transient storage ownership"
    );
    check(
        runtime.terminal_shutdown_failed
            && runtime.terminal_shutdown_error.code == GDOX_ERROR_IO,
        "retain the terminal nonorderly shutdown result"
    );

    reset_fixture();
    memset(&runtime, 0, sizeof(runtime));
    stop_success = false;
    runtime.xenia = &fixture_process;
    runtime.xenia_storage.session.active = true;
    check(
        !gdox_runtime_xenia_stop(&runtime, &error),
        "report an unreaped runtime process"
    );
    check(
        runtime.xenia == &fixture_process,
        "retain ownership of an unreaped runtime process"
    );
    check(storage_close_calls == 0U, "retain storage while Xenia is live");
    check(
        !runtime.terminal_shutdown_failed,
        "keep a live-process cleanup failure retryable"
    );

    reset_fixture();
    memset(&runtime, 0, sizeof(runtime));
    stop_exit_code = 1;
    storage_close_success = false;
    runtime.xenia = &fixture_process;
    runtime.xenia_storage.session.active = true;
    check(
        !gdox_runtime_xenia_stop(&runtime, &error),
        "report storage cleanup after a nonzero stop"
    );
    check(runtime.xenia == NULL, "release the stopped runtime process");
    check(
        runtime.xenia_storage.session.active,
        "retain failed transient storage cleanup for retry"
    );
    check(
        runtime.terminal_shutdown_failed,
        "retain the nonorderly result across storage cleanup retry"
    );
    storage_close_success = true;
    check(
        gdox_runtime_xenia_cleanup(&runtime, &error),
        "retry transient storage cleanup"
    );
    check(
        !runtime.xenia_storage.session.active,
        "release transient storage after cleanup retry"
    );
}

int main(void)
{
    gdox_xenia_process *process = NULL;
    gdox_error error;

    reset_fixture();
    check(
        gdox_xenia_process_stop_orderly(&process, &error),
        "accept an absent process"
    );
    check(destroy_calls == 0U, "do not destroy an absent process");

    reset_fixture();
    process = &fixture_process;
    check(
        gdox_xenia_process_stop_orderly(&process, &error),
        "accept an orderly zero exit"
    );
    check(
        observed_grace_ms == GDOX_XENIA_ORDERLY_STOP_GRACE_MS,
        "allow the full orderly-stop grace period"
    );
    check(process == NULL, "release an orderly stopped process");
    check(destroy_calls == 1U, "destroy an orderly stopped process once");
    check(!gdox_error_is_set(&error), "leave orderly stop error clear");

    reset_fixture();
    stop_exit_code = 1;
    process = &fixture_process;
    check(
        !gdox_xenia_process_stop_orderly(&process, &error),
        "report a nonzero stopped process"
    );
    check(process == NULL, "release a nonzero stopped process");
    check(destroy_calls == 1U, "destroy a nonzero stopped process once");
    check(
        error.code == GDOX_ERROR_IO
            && error.message[0] != '\0',
        "return the nonorderly shutdown reason"
    );

    reset_fixture();
    stop_success = false;
    process = &fixture_process;
    check(
        !gdox_xenia_process_stop_orderly(&process, &error),
        "report a process-control failure"
    );
    check(process == &fixture_process, "retain an unreaped process");
    check(destroy_calls == 0U, "do not destroy an unreaped process");
    check(
        error.code == GDOX_ERROR_IO,
        "preserve the process-control failure"
    );
    test_runtime_stop_contract();
    return failures == 0 ? 0 : 1;
}
