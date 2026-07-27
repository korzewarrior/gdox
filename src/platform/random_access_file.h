#ifndef GDOX_PLATFORM_RANDOM_ACCESS_FILE_H
#define GDOX_PLATFORM_RANDOM_ACCESS_FILE_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_random_access_file gdox_random_access_file;

bool gdox_random_access_file_open_update(
    const char *path,
    gdox_random_access_file **output,
    uint64_t *length,
    gdox_error *error
);
bool gdox_random_access_file_read(
    gdox_random_access_file *file,
    uint64_t offset,
    uint8_t *output,
    size_t bytes,
    gdox_error *error
);
bool gdox_random_access_file_write(
    gdox_random_access_file *file,
    uint64_t offset,
    const uint8_t *input,
    size_t bytes,
    gdox_error *error
);
bool gdox_random_access_file_sync_close(
    gdox_random_access_file *file,
    gdox_error *error
);
bool gdox_random_access_file_close(
    gdox_random_access_file *file,
    gdox_error *error
);

#endif
