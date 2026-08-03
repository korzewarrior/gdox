#ifndef GDOX_CORE_XEMU_CAPABILITIES_H
#define GDOX_CORE_XEMU_CAPABILITIES_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>

#define GDOX_XEMU_CAPABILITIES_ARGUMENT "--gdox-capabilities"
#define GDOX_XEMU_CAPABILITIES_FALSE_RESPONSE \
    "{\"schema\":1,\"runtime\":\"xemu\",\"storage\":{\"full_hdd_ram_cow\":true,\"backing_writes\":false,\"persistent_save_export\":false,\"max_dirty_bytes\":4294967296}}"
#define GDOX_XEMU_CAPABILITIES_TRUE_RESPONSE \
    "{\"schema\":3,\"runtime\":\"xemu\",\"storage\":{\"full_hdd_ram_cow\":true,\"backing_writes\":false,\"persistent_save_export\":true,\"persistent_save_scope\":\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\",\"persistent_save_format\":\"logical-files-v2\",\"persistent_hdd_config_format\":\"hdd-config-v1\",\"persistent_hdd_config_bytes\":524288,\"tdata_policy\":\"positive-reviewed-paths-v1\",\"tdata_policy_revision\":1,\"unknown_tdata\":\"preserve-legacy-source\",\"persistent_save_atomic\":true,\"persistent_save_import_before_boot\":true,\"persistent_save_checkpoint\":\"guest-flush-and-orderly-shutdown\",\"legacy_hdd_migration\":true,\"migration_receipt\":true,\"migration_interruption_safe\":true,\"max_dirty_bytes\":4294967296}}"

typedef struct gdox_xemu_storage_capabilities {
    bool persistent_save_export;
} gdox_xemu_storage_capabilities;

bool gdox_xemu_capabilities_parse(
    const char *output,
    size_t bytes,
    gdox_xemu_storage_capabilities *capabilities,
    gdox_error *error
);

#endif
