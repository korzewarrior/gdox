#define _POSIX_C_SOURCE 200809L

#include "gdox/source.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct gdox_file_context {
    int descriptor;
    uint64_t sectors;
} gdox_file_context;

static uint64_t file_sector_count(const void *context)
{
    const gdox_file_context *file = context;
    return file->sectors;
}

static bool file_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_file_context *file = context;
    uint64_t offset = lba * GDOX_LOGICAL_SECTOR_BYTES;
    size_t completed = 0U;

    (void)blocks;
    while (completed < output_bytes) {
        const size_t remaining = output_bytes - completed;
        const size_t request = remaining > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : remaining;
        ssize_t received;

        if (offset > (uint64_t)INT64_MAX) {
            gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "file offset exceeds POSIX range");
            return false;
        }
        received = pread(
            file->descriptor,
            output + completed,
            request,
            (off_t)offset
        );
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            char message[GDOX_ERROR_MESSAGE_CAPACITY];
            (void)snprintf(message, sizeof(message), "file read failed: %s", strerror(errno));
            gdox_error_set(error, GDOX_ERROR_IO, message);
            return false;
        }
        if (received == 0) {
            gdox_error_set(error, GDOX_ERROR_IO, "file ended during a sector read");
            return false;
        }
        completed += (size_t)received;
        offset += (uint64_t)received;
    }
    return true;
}

static bool file_media_present(const void *context)
{
    (void)context;
    return true;
}

static bool file_close(void *context, gdox_error *error)
{
    gdox_file_context *file = context;
    const int result = close(file->descriptor);
    free(file);
    if (result != 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        (void)snprintf(message, sizeof(message), "file close failed: %s", strerror(errno));
        gdox_error_set(error, GDOX_ERROR_IO, message);
        return false;
    }
    return true;
}

static const gdox_sector_source_ops file_ops = {
    file_sector_count,
    file_read,
    file_media_present,
    file_close,
    NULL,
    NULL,
    NULL,
};

bool gdox_source_open_file(
    const char *path,
    gdox_sector_source *output,
    gdox_error *error
)
{
    struct stat status;
    gdox_file_context *context;
    int descriptor;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "file path and output are required");
        return false;
    }
    descriptor = open(path, O_RDONLY
#ifdef O_CLOEXEC
        | O_CLOEXEC
#endif
    );
    if (descriptor < 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        (void)snprintf(message, sizeof(message), "could not open image: %s", strerror(errno));
        gdox_error_set(error, GDOX_ERROR_IO, message);
        return false;
    }
    if (fstat(descriptor, &status) != 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        (void)snprintf(message, sizeof(message), "could not inspect image: %s", strerror(errno));
        (void)close(descriptor);
        gdox_error_set(error, GDOX_ERROR_IO, message);
        return false;
    }
    if (!S_ISREG(status.st_mode) || status.st_size < 0) {
        (void)close(descriptor);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "image path is not a regular file");
        return false;
    }
    if ((uint64_t)status.st_size % GDOX_LOGICAL_SECTOR_BYTES != 0U) {
        (void)close(descriptor);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "image size is not a multiple of 2048 bytes"
        );
        return false;
    }

    context = malloc(sizeof(*context));
    if (context == NULL) {
        (void)close(descriptor);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate file source");
        return false;
    }
    context->descriptor = descriptor;
    context->sectors = (uint64_t)status.st_size / GDOX_LOGICAL_SECTOR_BYTES;
    output->context = context;
    output->ops = &file_ops;
    return true;
}
