#include "app/xenia_process_stop.h"

bool gdox_xenia_process_stop_orderly(
    gdox_xenia_process **process,
    gdox_error *error
)
{
    int exit_code;

    gdox_error_clear(error);
    if (process == NULL || *process == NULL) {
        return true;
    }
    if (!gdox_xenia_stop(
            *process,
            GDOX_XENIA_ORDERLY_STOP_GRACE_MS,
            &exit_code,
            error
        )) {
        return false;
    }
    gdox_xenia_process_destroy(*process);
    *process = NULL;
    if (exit_code != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "Xenia did not complete an orderly shutdown"
        );
        return false;
    }
    return true;
}
