#include "app/xemu_process_stop.h"

#include <stdint.h>

struct gdox_emulator_process {
    int marker;
};

static struct gdox_emulator_process fake_process = {1};
static bool stop_result;
static int stop_exit_code;
static unsigned int stop_calls;
static unsigned int destroy_calls;
static uint32_t observed_grace_ms;

bool gdox_emulator_stop(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    ++stop_calls;
    observed_grace_ms = grace_ms;
    if (process != &fake_process) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected fake process");
        return false;
    }
    if (!stop_result) {
        gdox_error_set(error, GDOX_ERROR_IO, "simulated stop failure");
        return false;
    }
    *exit_code = stop_exit_code;
    return true;
}

void gdox_emulator_process_destroy(gdox_emulator_process *process)
{
    if (process == &fake_process) {
        ++destroy_calls;
    }
}

static void reset_audit(void)
{
    stop_result = true;
    stop_exit_code = 0;
    stop_calls = 0U;
    destroy_calls = 0U;
    observed_grace_ms = 0U;
}

int main(void)
{
    gdox_emulator_process *process = NULL;
    gdox_error error;

    reset_audit();
    if (!gdox_xemu_process_stop_orderly(&process, &error)
        || stop_calls != 0U || destroy_calls != 0U) {
        return 1;
    }

    process = &fake_process;
    reset_audit();
    stop_result = false;
    if (gdox_xemu_process_stop_orderly(&process, &error)
        || process != &fake_process
        || stop_calls != 1U || destroy_calls != 0U
        || observed_grace_ms != UINT32_C(15000)
        || error.code != GDOX_ERROR_IO) {
        return 1;
    }

    process = &fake_process;
    reset_audit();
    if (!gdox_xemu_process_stop_orderly(&process, &error)
        || process != NULL
        || stop_calls != 1U || destroy_calls != 1U
        || observed_grace_ms != UINT32_C(15000)) {
        return 1;
    }

    process = &fake_process;
    reset_audit();
    stop_exit_code = 9;
    if (gdox_xemu_process_stop_orderly(&process, &error)
        || process != NULL
        || stop_calls != 1U || destroy_calls != 1U
        || observed_grace_ms != UINT32_C(15000)
        || error.code != GDOX_ERROR_IO) {
        return 1;
    }
    return 0;
}
