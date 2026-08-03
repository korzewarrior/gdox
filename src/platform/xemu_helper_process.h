#ifndef GDOX_PLATFORM_XEMU_HELPER_PROCESS_H
#define GDOX_PLATFORM_XEMU_HELPER_PROCESS_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_XEMU_HELPER_CAPTURE_BYTES 4096U
#define GDOX_XEMU_HELPER_MAXIMUM_TIMEOUT_MS 3600000U

typedef struct gdox_xemu_helper_result {
    char output[GDOX_XEMU_HELPER_CAPTURE_BYTES];
    char diagnostics[GDOX_XEMU_HELPER_CAPTURE_BYTES];
    size_t output_bytes;
    size_t diagnostic_bytes;
    int exit_code;
    bool timed_out;
    bool overflow;
} gdox_xemu_helper_result;

bool gdox_xemu_helper_run(
    const char *executable,
    const char *const *arguments,
    uint32_t timeout_ms,
    gdox_xemu_helper_result *result,
    gdox_error *error
);

#endif
