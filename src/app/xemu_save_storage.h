#ifndef GDOX_APP_XEMU_SAVE_STORAGE_H
#define GDOX_APP_XEMU_SAVE_STORAGE_H

#include "gdox/error.h"
#include "platform/user_storage.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct gdox_xemu_legacy_migration_outcome {
    bool legacy_found;
    bool source_removed;
    bool retained_due_to_unclassified;
    bool retained_due_to_save_conflict;
    bool receipt_reused;
    uint32_t unclassified_tdata_entries;
    uint64_t unclassified_tdata_bytes;
} gdox_xemu_legacy_migration_outcome;

bool gdox_xemu_save_vault_prepare(
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
);

/* Migrates only the historical fixed GDOX-managed HDD path. */
bool gdox_xemu_migrate_legacy_managed_hdd(
    const char *executable,
    const char *clean_hdd,
    gdox_error *error
);
bool gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
    const char *executable,
    const char *clean_hdd,
    gdox_xemu_legacy_migration_outcome *outcome,
    gdox_error *error
);

#endif
