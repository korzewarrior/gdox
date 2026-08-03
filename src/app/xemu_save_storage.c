#include "app/xemu_save_storage.h"

#include "platform/xemu_save_migration.h"

#include <stdint.h>
#include <string.h>

#define GDOX_XEMU_SAVE_VAULT_RELATIVE "xemu/saves/v1"
#define GDOX_XEMU_LEGACY_HDD_RELATIVE "xemu/xbox_hdd.qcow2"
#define GDOX_XEMU_LEGACY_HDD_MAX_BYTES (UINT64_C(64) * 1024U * 1024U * 1024U)

bool gdox_xemu_save_vault_prepare(
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    char path[GDOX_STORAGE_PATH_CAPACITY];

    gdox_error_clear(error);
    if (output == NULL
        || !gdox_user_data_path(
            GDOX_XEMU_SAVE_VAULT_RELATIVE, path, error
        )
        || !gdox_storage_ensure_private_directory(path, error)) {
        if (output == NULL && !gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "xemu save vault destination is required"
            );
        }
        return false;
    }
    return gdox_storage_resolve_existing_path(path, output, error);
}

bool gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
    const char *executable,
    const char *clean_hdd,
    gdox_xemu_legacy_migration_outcome *outcome,
    gdox_error *error
)
{
    char legacy_path[GDOX_STORAGE_PATH_CAPACITY];
    char legacy_resolved[GDOX_STORAGE_PATH_CAPACITY];
    char clean_resolved[GDOX_STORAGE_PATH_CAPACITY];
    char save_vault[GDOX_STORAGE_PATH_CAPACITY];
    gdox_xemu_save_migration_proof migrated;
    gdox_xemu_save_vault_proof validated;
    uint64_t source_bytes = 0U;
    uint64_t pending_bytes_after = 0U;
    bool legacy_found = false;
    bool legacy_present_after = false;
    bool pending_removal = false;
    bool pending_removal_after = false;
    bool clean_found = false;

    gdox_error_clear(error);
    if (outcome != NULL) {
        memset(outcome, 0, sizeof(*outcome));
    }
    if (executable == NULL || executable[0] == '\0'
        || clean_hdd == NULL || clean_hdd[0] == '\0') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu executable and verified clean HDD are required"
        );
        return false;
    }
    if (!gdox_user_data_path(
            GDOX_XEMU_LEGACY_HDD_RELATIVE, legacy_path, error
        )
        || !gdox_storage_ordinary_file(
            legacy_path, &legacy_found, error
        )) {
        return false;
    }
    if (!legacy_found) {
        if (!gdox_storage_xemu_pending_hdd(
                legacy_path, &pending_removal, &source_bytes, error
            )) {
            return false;
        }
        if (!pending_removal) {
            return true;
        }
    }
    if (outcome != NULL) {
        outcome->legacy_found = true;
    }
    if (!gdox_storage_ordinary_file(clean_hdd, &clean_found, error)
        || !clean_found
        || (!pending_removal
            && !gdox_storage_resolve_existing_path(
                legacy_path, legacy_resolved, error
            ))
        || !gdox_storage_resolve_existing_path(
            clean_hdd, clean_resolved, error
        )
        || !gdox_xemu_save_vault_prepare(save_vault, error)
        || (!pending_removal
            && !gdox_storage_file_size(legacy_resolved, &source_bytes))) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "managed legacy Xbox HDD or verified clean HDD is invalid"
            );
        }
        return false;
    }
    if (pending_removal) {
        memcpy(legacy_resolved, legacy_path, strlen(legacy_path) + 1U);
    }
    if (source_bytes == 0U
        || source_bytes > GDOX_XEMU_LEGACY_HDD_MAX_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "managed legacy Xbox HDD has an invalid or excessive container size"
        );
        return false;
    }
    if (!gdox_emulator_migrate_legacy_hdd(
            executable,
            legacy_resolved,
            clean_resolved,
            save_vault,
            source_bytes,
            &migrated,
            error
        )
        || !gdox_emulator_validate_save_vault(
            executable,
            save_vault,
            clean_resolved,
            &validated,
            error
        )) {
        return false;
    }
    if (!gdox_xemu_save_vault_proof_equal(
            &migrated.vault, &validated
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "migrated Xbox saves did not match independent vault validation"
        );
        return false;
    }
    if (!gdox_storage_ordinary_file(
            legacy_path, &legacy_present_after, error
        )
        || !gdox_storage_xemu_pending_hdd(
            legacy_path,
            &pending_removal_after,
            &pending_bytes_after,
            error
        )) {
        return false;
    }
    (void)pending_bytes_after;
    if (migrated.source_removed && pending_removal_after) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "xemu reported legacy Xbox HDD removal but quarantine remains"
        );
        return false;
    }
    if (migrated.source_removed == legacy_present_after) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            migrated.source_removed
                ? "xemu reported legacy Xbox HDD removal but the managed source remains"
                : "xemu preserved legacy Xbox HDD saves but the managed source is missing"
        );
        return false;
    }
    if (outcome != NULL) {
        outcome->source_removed = migrated.source_removed;
        outcome->receipt_reused = migrated.receipt_reused;
        outcome->unclassified_tdata_entries =
            migrated.vault.unclassified_tdata_entries;
        outcome->unclassified_tdata_bytes =
            migrated.vault.unclassified_tdata_bytes;
        outcome->retained_due_to_unclassified =
            !migrated.source_removed
            && migrated.vault.unclassified_tdata_entries != 0U;
        outcome->retained_due_to_save_conflict =
            !migrated.source_removed
            && !migrated.source_removal_safe
            && migrated.vault.unclassified_tdata_entries == 0U;
    }
    return true;
}

bool gdox_xemu_migrate_legacy_managed_hdd(
    const char *executable,
    const char *clean_hdd,
    gdox_error *error
)
{
    return gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        executable, clean_hdd, NULL, error
    );
}
