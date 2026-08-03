#include "core/xemu_capabilities.h"

#include <string.h>

static bool exact_line(
    const char *output,
    size_t bytes,
    const char *expected
)
{
    const size_t expected_bytes = strlen(expected);

    if (bytes == expected_bytes
        && memcmp(output, expected, expected_bytes) == 0) {
        return true;
    }
    if (bytes == expected_bytes + 1U
        && output[expected_bytes] == '\n'
        && memcmp(output, expected, expected_bytes) == 0) {
        return true;
    }
    return bytes == expected_bytes + 2U
        && output[expected_bytes] == '\r'
        && output[expected_bytes + 1U] == '\n'
        && memcmp(output, expected, expected_bytes) == 0;
}

bool gdox_xemu_capabilities_parse(
    const char *output,
    size_t bytes,
    gdox_xemu_storage_capabilities *capabilities,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (output == NULL || capabilities == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu capability output and destination are required"
        );
        return false;
    }
    memset(capabilities, 0, sizeof(*capabilities));
    if (exact_line(
            output, bytes, GDOX_XEMU_CAPABILITIES_FALSE_RESPONSE
        )) {
        return true;
    }
    if (exact_line(
            output, bytes, GDOX_XEMU_CAPABILITIES_TRUE_RESPONSE
        )) {
        capabilities->persistent_save_export = true;
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "xemu did not report the required GDOX storage capability contract"
    );
    return false;
}
