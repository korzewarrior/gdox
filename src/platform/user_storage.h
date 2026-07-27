#ifndef GDOX_USER_STORAGE_H
#define GDOX_USER_STORAGE_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_STORAGE_PATH_CAPACITY 4096U

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
bool gdox_storage_ensure_directory(
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

#endif
