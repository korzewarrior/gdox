#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "app/app.h"
#include "app/runtime_internal.h"

#include "gdox/disc.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct close_audit {
    unsigned int prepare_calls;
    unsigned int close_calls;
    unsigned int failures_remaining;
} close_audit;

static uint64_t fake_length(const void *context)
{
    (void)context;
    return 0U;
}

static bool fake_read(
    void *context,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    (void)context;
    (void)offset;
    (void)output;
    (void)output_bytes;
    (void)read_bytes;
    gdox_error_set(error, GDOX_ERROR_IO, "fake disc is not readable");
    return false;
}

static bool fake_present(const void *context)
{
    (void)context;
    return true;
}

static bool fake_close(void *context, gdox_error *error)
{
    close_audit *audit = context;

    gdox_error_clear(error);
    ++audit->close_calls;
    return true;
}

static bool fake_prepare_close(void *context, gdox_error *error)
{
    close_audit *audit = context;

    gdox_error_clear(error);
    ++audit->prepare_calls;
    if (audit->failures_remaining > 0U) {
        --audit->failures_remaining;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated close preparation failure");
        return false;
    }
    return true;
}

static const gdox_random_disc_ops fake_disc_ops = {
    fake_length,
    fake_read,
    fake_present,
    fake_close,
    NULL,
    NULL,
    fake_prepare_close,
    NULL,
};

static uint64_t fake_sector_count(const void *context)
{
    (void)context;
    return 0U;
}

static bool fake_sector_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    (void)context;
    (void)lba;
    (void)blocks;
    (void)output;
    (void)output_bytes;
    gdox_error_set(error, GDOX_ERROR_IO, "fake source is not readable");
    return false;
}

static const gdox_sector_source_ops fake_source_ops = {
    fake_sector_count,
    fake_sector_read,
    fake_present,
    fake_close,
    NULL,
    NULL,
    NULL,
    fake_prepare_close,
    NULL,
};

static gdox_runtime *make_runtime(void)
{
    gdox_runtime *runtime = calloc(1U, sizeof(*runtime));

    if (runtime == NULL || !gdox_mutex_init(&runtime->mutex)) {
        free(runtime);
        return NULL;
    }
    atomic_init(&runtime->stopping, false);
    atomic_init(&runtime->preservation_cancelled, false);
    return runtime;
}

int main(void)
{
    gdox_runtime *runtime = make_runtime();
    gdox_app app = {0};
    close_audit audit = {0};
    close_audit source_audit = {0};
    gdox_error error;

    if (runtime == NULL) {
        return 1;
    }
    audit.failures_remaining = 1U;
    runtime->media.open = true;
    runtime->media.validated_disc.context = &audit;
    runtime->media.validated_disc.ops = &fake_disc_ops;
    app.runtime = runtime;

    if (gdox_app_shutdown(&app, &error)
        || app.runtime != runtime
        || error.code != GDOX_ERROR_IO
        || audit.prepare_calls != 1U
        || audit.close_calls != 0U) {
        return 1;
    }
    if (!gdox_app_shutdown(&app, &error)
        || app.runtime != NULL
        || audit.prepare_calls < 2U
        || audit.close_calls != 1U) {
        return 1;
    }

    runtime = make_runtime();
    if (runtime == NULL) {
        return 1;
    }
    source_audit.failures_remaining = 1U;
    runtime->media.retained_source.context = &source_audit;
    runtime->media.retained_source.ops = &fake_source_ops;
    app.runtime = runtime;
    if (gdox_app_shutdown(&app, &error)
        || app.runtime != runtime
        || error.code != GDOX_ERROR_IO
        || source_audit.prepare_calls != 1U
        || source_audit.close_calls != 0U) {
        return 1;
    }
    if (!gdox_app_shutdown(&app, &error)
        || app.runtime != NULL
        || source_audit.prepare_calls < 2U
        || source_audit.close_calls != 1U) {
        return 1;
    }

    runtime = make_runtime();
    if (runtime == NULL) {
        return 1;
    }
    runtime->terminal_shutdown_failed = true;
    gdox_error_set(
        &runtime->terminal_shutdown_error,
        GDOX_ERROR_IO,
        "xemu did not complete an orderly save checkpoint"
    );
    app.runtime = runtime;
    if (gdox_app_shutdown(&app, &error)
        || app.runtime != NULL
        || error.code != GDOX_ERROR_IO
        || strcmp(
            error.message,
            "xemu did not complete an orderly save checkpoint"
        ) != 0
        || app.snapshot.phase != GDOX_APP_ATTENTION
        || strcmp(
            app.snapshot.status,
            "GDOX closed with a save error"
        ) != 0
        || strcmp(app.snapshot.notice, error.message) != 0) {
        return 1;
    }
    if (!gdox_app_shutdown(&app, &error)) {
        return 1;
    }
    return 0;
}
