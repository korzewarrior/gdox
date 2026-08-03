#include "gdox/emulator.h"

#include "core/xemu_capabilities.h"
#include "platform/xemu_helper_process.h"

enum {
    GDOX_XEMU_CAPABILITY_TIMEOUT_MS = 2000U,
};

bool gdox_emulator_query_storage_capabilities(
    const char *path,
    bool *persistent_save_export,
    gdox_error *error
)
{
    static const char *const arguments[] = {
        GDOX_XEMU_CAPABILITIES_ARGUMENT,
        NULL,
    };
    gdox_xemu_helper_result result;
    gdox_xemu_storage_capabilities capabilities;

    gdox_error_clear(error);
    if (persistent_save_export != NULL) {
        *persistent_save_export = false;
    }
    if (persistent_save_export == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu capability destination is required"
        );
        return false;
    }
    if (!gdox_xemu_helper_run(
            path,
            arguments,
            GDOX_XEMU_CAPABILITY_TIMEOUT_MS,
            &result,
            error
        )) {
        return false;
    }
    if (result.timed_out) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "xemu capability query exceeded its two-second limit"
        );
        return false;
    }
    if (result.exit_code != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "xemu capability query did not exit successfully"
        );
        return false;
    }
    if (result.overflow || result.diagnostic_bytes != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            result.overflow
                ? "xemu capability output exceeded its limit"
                : "xemu capability query wrote unexpected diagnostics"
        );
        return false;
    }
    if (!gdox_xemu_capabilities_parse(
            result.output,
            result.output_bytes,
            &capabilities,
            error
        )) {
        return false;
    }
    *persistent_save_export = capabilities.persistent_save_export;
    return true;
}
