#ifndef GDOX_CORE_XEMU_SAVE_MIGRATION_H
#define GDOX_CORE_XEMU_SAVE_MIGRATION_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_XEMU_SAVE_CAPABILITY_SCHEMA 3U
#define GDOX_XEMU_SAVE_VAULT_VERSION 2U
#define GDOX_XEMU_SAVE_POLICY_REVISION 1U
#define GDOX_XEMU_SAVE_HDD_CONFIG_BYTES UINT64_C(0x80000)

typedef struct gdox_xemu_save_vault_proof {
    uint32_t vault_version;
    uint64_t generation;
    uint32_t entries;
    uint64_t logical_bytes;
    uint8_t tree_sha256[32];
    uint32_t policy_revision;
    uint8_t policy_sha256[32];
    uint32_t udata_entries;
    uint64_t udata_logical_bytes;
    uint32_t tdata_entries;
    uint64_t tdata_logical_bytes;
    uint64_t hdd_config_bytes;
    uint8_t hdd_config_sha256[32];
    uint32_t unclassified_tdata_entries;
    uint64_t unclassified_tdata_bytes;
    uint8_t unclassified_tdata_sha256[32];
    bool vault_reopened;
    bool anonymous_cow;
    bool clean_backing;
    bool reprojected;
    bool roundtrip_verified;
} gdox_xemu_save_vault_proof;

typedef struct gdox_xemu_save_migration_proof {
    uint64_t source_bytes;
    uint8_t source_identity_sha256[32];
    uint8_t source_sha256[32];
    bool source_hashed;
    bool source_projection_complete;
    uint64_t clean_bytes;
    uint8_t clean_sha256[32];
    bool source_writes;
    bool receipt_reused;
    bool source_removal_safe;
    bool source_removed;
    gdox_xemu_save_vault_proof vault;
} gdox_xemu_save_migration_proof;

typedef enum gdox_xemu_save_operation {
    GDOX_XEMU_SAVE_MIGRATE_HDD = 0,
    GDOX_XEMU_SAVE_VALIDATE_VAULT,
} gdox_xemu_save_operation;

bool gdox_xemu_save_migration_result_parse(
    const char *output,
    size_t bytes,
    uint64_t expected_source_bytes,
    gdox_xemu_save_migration_proof *proof,
    gdox_error *error
);
bool gdox_xemu_save_validation_result_parse(
    const char *output,
    size_t bytes,
    gdox_xemu_save_vault_proof *proof,
    gdox_error *error
);
bool gdox_xemu_save_vault_proof_equal(
    const gdox_xemu_save_vault_proof *left,
    const gdox_xemu_save_vault_proof *right
);

#endif
