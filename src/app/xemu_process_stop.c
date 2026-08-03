#include "app/xemu_process_stop.h"

bool gdox_xemu_process_stop_orderly(
    gdox_emulator_process **process,
    gdox_error *error
)
{
    int exit_code;

    gdox_error_clear(error);
    if (process == NULL || *process == NULL) {
        return true;
    }
    if (!gdox_emulator_stop(
            *process,
            GDOX_XEMU_ORDERLY_STOP_GRACE_MS,
            &exit_code,
            error
        )) {
        return false;
    }
    gdox_emulator_process_destroy(*process);
    *process = NULL;
    if (exit_code != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "xemu did not complete an orderly save checkpoint"
        );
        return false;
    }
    return true;
}
