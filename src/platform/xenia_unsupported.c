#include "gdox/xenia.h"

#include <string.h>

struct gdox_xenia_process {
    unsigned int unused;
};

static bool unsupported(gdox_error *error)
{
    gdox_error_clear(error);
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "Xbox 360 playback is unavailable on this platform"
    );
    return false;
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
    if (output != NULL) {
        memset(output, 0, sizeof(*output));
    }
    return unsupported(error);
}

bool gdox_xenia_target_supported(gdox_xenia_target_kind kind)
{
    (void)kind;
    return false;
}

bool gdox_xenia_runtime_target_supported(
    const gdox_xenia_runtime *runtime,
    gdox_xenia_target_kind kind
)
{
    (void)runtime;
    (void)kind;
    return false;
}

bool gdox_xenia_target_preflight(
    gdox_xenia_target_kind kind,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (kind != GDOX_XENIA_TARGET_IMAGE
        && kind != GDOX_XENIA_TARGET_PRIVATE_NBD) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia target kind is invalid"
        );
        return false;
    }
    return unsupported(error);
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
    if (process != NULL) {
        *process = NULL;
    }
    return unsupported(error);
}

bool gdox_xenia_poll(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    (void)process;
    if (running != NULL) {
        *running = false;
    }
    if (exit_code != NULL) {
        *exit_code = -1;
    }
    return unsupported(error);
}

bool gdox_xenia_stop(
    gdox_xenia_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    (void)process;
    (void)grace_ms;
    if (exit_code != NULL) {
        *exit_code = -1;
    }
    return unsupported(error);
}

void gdox_xenia_process_destroy(gdox_xenia_process *process)
{
    (void)process;
}
