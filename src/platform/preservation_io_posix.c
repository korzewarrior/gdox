#define _POSIX_C_SOURCE 200809L

#include "platform/preservation_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

struct gdox_preservation_file {
    int descriptor;
};

static void set_errno_error(
    gdox_error *error,
    const char *operation,
    int code
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    (void)snprintf(message, sizeof(message), "%s: %s", operation, strerror(code));
    gdox_error_set(error, GDOX_ERROR_IO, message);
}

static bool allocate_file(
    int descriptor,
    gdox_preservation_file **output,
    gdox_error *error
)
{
    gdox_preservation_file *file = malloc(sizeof(*file));
    if (file == NULL) {
        (void)close(descriptor);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate preservation file");
        return false;
    }
    file->descriptor = descriptor;
    *output = file;
    return true;
}

bool gdox_preservation_file_create(
    const char *path,
    gdox_preservation_file **output,
    gdox_error *error
)
{
    int descriptor;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "output path and file are required");
        return false;
    }
    *output = NULL;
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        set_errno_error(error, "could not create preservation output", errno);
        return false;
    }
    return allocate_file(descriptor, output, error);
}

bool gdox_preservation_file_open_read(
    const char *path,
    gdox_preservation_file **output,
    uint64_t *length,
    gdox_error *error
)
{
    struct stat status;
    int descriptor;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL || length == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "image path, file, and length are required");
        return false;
    }
    *output = NULL;
    descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0 || fstat(descriptor, &status) != 0) {
        const int code = errno;
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        set_errno_error(error, "could not open preservation output", code);
        return false;
    }
    if (status.st_size < 0) {
        (void)close(descriptor);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "preservation output has an invalid length");
        return false;
    }
    *length = (uint64_t)status.st_size;
    return allocate_file(descriptor, output, error);
}

bool gdox_preservation_file_write(
    gdox_preservation_file *file,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
)
{
    size_t completed = 0U;
    while (completed < length) {
        const ssize_t written = write(file->descriptor, bytes + completed, length - completed);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            set_errno_error(error, "could not write preservation output", errno);
            return false;
        }
        completed += (size_t)written;
    }
    return true;
}

bool gdox_preservation_file_read(
    gdox_preservation_file *file,
    uint8_t *bytes,
    size_t capacity,
    size_t *read_bytes,
    gdox_error *error
)
{
    ssize_t result;
    do {
        result = read(file->descriptor, bytes, capacity);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        set_errno_error(error, "could not read preservation output", errno);
        return false;
    }
    *read_bytes = (size_t)result;
    return true;
}

bool gdox_preservation_file_sync_close(
    gdox_preservation_file *file,
    gdox_error *error
)
{
    bool success = true;
    int code = 0;

    if (file == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "preservation file is required");
        return false;
    }
    if (fsync(file->descriptor) != 0) {
        success = false;
        code = errno;
    }
    if (close(file->descriptor) != 0 && success) {
        success = false;
        code = errno;
    }
    free(file);
    if (!success) {
        set_errno_error(error, "could not synchronize preservation output", code);
    }
    return success;
}

bool gdox_preservation_file_close(
    gdox_preservation_file *file,
    gdox_error *error
)
{
    int result;
    if (file == NULL) {
        return true;
    }
    result = close(file->descriptor);
    free(file);
    if (result != 0) {
        set_errno_error(error, "could not close preservation file", errno);
        return false;
    }
    return true;
}

bool gdox_preservation_path_exists(const char *path)
{
    struct stat status;
    return path != NULL && stat(path, &status) == 0;
}

bool gdox_preservation_path_remove(const char *path)
{
    return path != NULL && (unlink(path) == 0 || errno == ENOENT);
}

bool gdox_preservation_path_commit(
    const char *temporary_path,
    const char *final_path,
    gdox_error *error
)
{
    if (link(temporary_path, final_path) != 0) {
        set_errno_error(error, "could not commit preservation output", errno);
        return false;
    }
    if (unlink(temporary_path) != 0) {
        const int code = errno;
        (void)unlink(final_path);
        set_errno_error(error, "could not remove committed temporary output", code);
        return false;
    }
    return true;
}

static void parent_path(const char *path, char *output, size_t output_bytes)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        (void)snprintf(output, output_bytes, "%s", ".");
    } else if (slash == path) {
        (void)snprintf(output, output_bytes, "%s", "/");
    } else {
        const size_t length = (size_t)(slash - path);
        const size_t copy = length < output_bytes - 1U ? length : output_bytes - 1U;
        memcpy(output, path, copy);
        output[copy] = '\0';
    }
}

bool gdox_preservation_available_space(
    const char *path,
    uint64_t *bytes,
    gdox_error *error
)
{
    char parent[4096];
    struct statvfs status;

    if (path == NULL || bytes == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "output path and space result are required");
        return false;
    }
    parent_path(path, parent, sizeof(parent));
    if (statvfs(parent, &status) != 0) {
        set_errno_error(error, "could not inspect output free space", errno);
        return false;
    }
    if (status.f_frsize != 0U && status.f_bavail > UINT64_MAX / status.f_frsize) {
        *bytes = UINT64_MAX;
    } else {
        *bytes = (uint64_t)status.f_bavail * (uint64_t)status.f_frsize;
    }
    return true;
}
