#include "platform/xemu_save_migration.h"

#include "core/xemu_save_migration.h"
#include "platform/xemu_helper_process.h"

#include <stdio.h>

enum {
    GDOX_XEMU_SAVE_OPERATION_MINIMUM_TIMEOUT_MS = 600000U,
    GDOX_XEMU_SAVE_OPERATION_MAXIMUM_TIMEOUT_MS = 3600000U,
    GDOX_XEMU_SAVE_OPERATION_BYTES_PER_SECOND = 16U * 1024U * 1024U,
};

uint32_t gdox_xemu_save_migration_timeout_ms(uint64_t source_bytes)
{
    const uint64_t extra_seconds =
        source_bytes / GDOX_XEMU_SAVE_OPERATION_BYTES_PER_SECOND
        + (source_bytes % GDOX_XEMU_SAVE_OPERATION_BYTES_PER_SECOND != 0U);
    const uint32_t available =
        GDOX_XEMU_SAVE_OPERATION_MAXIMUM_TIMEOUT_MS
        - GDOX_XEMU_SAVE_OPERATION_MINIMUM_TIMEOUT_MS;

    if (extra_seconds >= available / 1000U) {
        return GDOX_XEMU_SAVE_OPERATION_MAXIMUM_TIMEOUT_MS;
    }
    return GDOX_XEMU_SAVE_OPERATION_MINIMUM_TIMEOUT_MS
        + (uint32_t)extra_seconds * 1000U;
}

static bool run_save_operation(
    gdox_xemu_save_operation operation,
    const char *executable,
    const char *source_hdd,
    const char *clean_hdd,
    const char *save_vault,
    uint64_t expected_source_bytes,
    gdox_xemu_save_migration_proof *migration,
    gdox_xemu_save_vault_proof *validation,
    gdox_error *error
)
{
    char expected_bytes[32];
    const char *migration_arguments[] = {
        "--gdox-migrate-hdd",
        source_hdd,
        "--gdox-remove-migrated-source",
        "--gdox-clean-hdd",
        clean_hdd,
        "--gdox-save-vault",
        save_vault,
        "--gdox-expected-source-bytes",
        expected_bytes,
        NULL,
    };
    const char *validation_arguments[] = {
        "--gdox-validate-save-vault",
        save_vault,
        "--gdox-clean-hdd",
        clean_hdd,
        NULL,
    };
    gdox_xemu_helper_result result;
    const char *const *arguments = operation == GDOX_XEMU_SAVE_MIGRATE_HDD
        ? migration_arguments : validation_arguments;
    const uint32_t timeout_ms = operation == GDOX_XEMU_SAVE_MIGRATE_HDD
        ? gdox_xemu_save_migration_timeout_ms(expected_source_bytes)
        : GDOX_XEMU_SAVE_OPERATION_MINIMUM_TIMEOUT_MS;

    gdox_error_clear(error);
    if (executable == NULL || executable[0] == '\0'
        || clean_hdd == NULL || clean_hdd[0] == '\0'
        || save_vault == NULL || save_vault[0] == '\0'
        || (operation == GDOX_XEMU_SAVE_MIGRATE_HDD
            && (source_hdd == NULL || source_hdd[0] == '\0'
                || expected_source_bytes == 0U || migration == NULL))
        || (operation == GDOX_XEMU_SAVE_VALIDATE_VAULT
            && validation == NULL)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu save operation paths and proof destination are required"
        );
        return false;
    }
    if (operation == GDOX_XEMU_SAVE_MIGRATE_HDD) {
        const int written = snprintf(
            expected_bytes,
            sizeof(expected_bytes),
            "%llu",
            (unsigned long long)expected_source_bytes
        );

        if (written < 0 || (size_t)written >= sizeof(expected_bytes)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "xemu migration source size is invalid"
            );
            return false;
        }
    }
    if (!gdox_xemu_helper_run(
            executable, arguments, timeout_ms, &result, error
        )) {
        return false;
    }
    if (result.timed_out) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "xemu save operation exceeded its size-adjusted safety deadline"
        );
        return false;
    }
    if (result.exit_code != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            operation == GDOX_XEMU_SAVE_MIGRATE_HDD
                ? "xemu could not safely migrate the managed legacy Xbox hard disk"
                : "xemu could not validate the logical Xbox save vault"
        );
        return false;
    }
    if (result.overflow || result.diagnostic_bytes != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            result.overflow
                ? "xemu save operation output exceeded its limit"
                : "xemu save operation wrote unexpected diagnostics"
        );
        return false;
    }
    return operation == GDOX_XEMU_SAVE_MIGRATE_HDD
        ? gdox_xemu_save_migration_result_parse(
            result.output,
            result.output_bytes,
            expected_source_bytes,
            migration,
            error
        )
        : gdox_xemu_save_validation_result_parse(
            result.output,
            result.output_bytes,
            validation,
            error
        );
}

bool gdox_emulator_migrate_legacy_hdd(
    const char *executable,
    const char *source_hdd,
    const char *clean_hdd,
    const char *save_vault,
    uint64_t expected_source_bytes,
    gdox_xemu_save_migration_proof *proof,
    gdox_error *error
)
{
    return run_save_operation(
        GDOX_XEMU_SAVE_MIGRATE_HDD,
        executable,
        source_hdd,
        clean_hdd,
        save_vault,
        expected_source_bytes,
        proof,
        NULL,
        error
    );
}

bool gdox_emulator_validate_save_vault(
    const char *executable,
    const char *save_vault,
    const char *clean_hdd,
    gdox_xemu_save_vault_proof *proof,
    gdox_error *error
)
{
    return run_save_operation(
        GDOX_XEMU_SAVE_VALIDATE_VAULT,
        executable,
        NULL,
        clean_hdd,
        save_vault,
        0U,
        NULL,
        proof,
        error
    );
}
