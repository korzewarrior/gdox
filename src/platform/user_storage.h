#ifndef GDOX_USER_STORAGE_H
#define GDOX_USER_STORAGE_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_STORAGE_PATH_CAPACITY 4096U

typedef enum gdox_storage_remove_result {
    GDOX_STORAGE_REMOVE_NOT_FOUND = 0,
    GDOX_STORAGE_REMOVE_MISMATCH,
    GDOX_STORAGE_REMOVE_REMOVED,
} gdox_storage_remove_result;

bool gdox_user_config_path(
    const char *relative,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
);
bool gdox_user_data_path(
    const char *relative,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
);
bool gdox_storage_file_size(
    const char *path,
    uint64_t *bytes
);
bool gdox_storage_ordinary_file(
    const char *path,
    bool *found,
    gdox_error *error
);
/* Finds one exact, private POSIX removal-quarantine candidate beside the
 * historical managed xemu HDD. Windows removal cannot create this state. */
bool gdox_storage_xemu_pending_hdd(
    const char *managed_path,
    bool *found,
    uint64_t *bytes,
    gdox_error *error
);
bool gdox_storage_resolve_existing_path(
    const char *path,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
);
bool gdox_storage_directory_exists(const char *path);
bool gdox_storage_ensure_directory(
    const char *path,
    gdox_error *error
);
/* Creates a private directory and rejects a final symlink/reparse point.
 * POSIX requires current-user ownership and no group or other permissions.
 * Windows requires current-user ownership and a protected caller-only DACL. */
bool gdox_storage_ensure_private_directory(
    const char *path,
    gdox_error *error
);
bool gdox_storage_read(
    const char *path,
    size_t maximum_bytes,
    uint8_t **data,
    size_t *bytes,
    bool *found,
    gdox_error *error
);
bool gdox_storage_write_private(
    const char *path,
    const uint8_t *data,
    size_t bytes,
    bool replace,
    gdox_error *error
);
bool gdox_storage_copy_private(
    const char *source,
    const char *destination,
    bool replace,
    gdox_error *error
);
bool gdox_storage_remove_exact_file(
    const char *path,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[32],
    gdox_storage_remove_result *result,
    gdox_error *error
);

#endif
