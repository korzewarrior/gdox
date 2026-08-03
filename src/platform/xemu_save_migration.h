#ifndef GDOX_PLATFORM_XEMU_SAVE_MIGRATION_H
#define GDOX_PLATFORM_XEMU_SAVE_MIGRATION_H

#include "core/xemu_save_migration.h"

uint32_t gdox_xemu_save_migration_timeout_ms(uint64_t source_bytes);

bool gdox_emulator_migrate_legacy_hdd(
    const char *executable,
    const char *source_hdd,
    const char *clean_hdd,
    const char *save_vault,
    uint64_t expected_source_bytes,
    gdox_xemu_save_migration_proof *proof,
    gdox_error *error
);
bool gdox_emulator_validate_save_vault(
    const char *executable,
    const char *save_vault,
    const char *clean_hdd,
    gdox_xemu_save_vault_proof *proof,
    gdox_error *error
);

#endif
