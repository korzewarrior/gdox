#ifndef GDOX_CORE_PORTS_PRESERVATION_IO_H
#define GDOX_CORE_PORTS_PRESERVATION_IO_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_preservation_file gdox_preservation_file;

bool gdox_preservation_file_create(
    const char *path,
    gdox_preservation_file **output,
    gdox_error *error
);
bool gdox_preservation_file_open_read(
    const char *path,
    gdox_preservation_file **output,
    uint64_t *length,
    gdox_error *error
);
bool gdox_preservation_file_write(
    gdox_preservation_file *file,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
);
bool gdox_preservation_file_read(
    gdox_preservation_file *file,
    uint8_t *bytes,
    size_t capacity,
    size_t *read_bytes,
    gdox_error *error
);
bool gdox_preservation_file_sync_close(
    gdox_preservation_file *file,
    gdox_error *error
);
bool gdox_preservation_file_close(
    gdox_preservation_file *file,
    gdox_error *error
);
bool gdox_preservation_path_exists(const char *path);
bool gdox_preservation_path_remove(const char *path);
bool gdox_preservation_path_commit(
    const char *temporary_path,
    const char *final_path,
    gdox_error *error
);
bool gdox_preservation_available_space(
    const char *path,
    uint64_t *bytes,
    gdox_error *error
);

#endif
