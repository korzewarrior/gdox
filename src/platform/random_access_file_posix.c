#define _POSIX_C_SOURCE 200809L
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "core/ports/random_access_file.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

struct gdox_random_access_file {
    int descriptor;
};

static void set_file_error(
    gdox_error *error,
    const char *operation,
    int code
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    (void)snprintf(message, sizeof(message), "%s: %s", operation, strerror(code));
    gdox_error_set(error, GDOX_ERROR_IO, message);
}

static bool valid_range(uint64_t offset, size_t bytes)
{
    return offset <= (uint64_t)INT64_MAX
        && (uint64_t)bytes <= (uint64_t)INT64_MAX - offset;
}

bool gdox_random_access_file_open_update(
    const char *path,
    gdox_random_access_file **output,
    uint64_t *length,
    gdox_error *error
)
{
    gdox_random_access_file *file;
    struct stat status;
    int descriptor;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL || length == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access file path, output, and length are required"
        );
        return false;
    }
    *output = NULL;
    descriptor = open(path, O_RDWR | O_CLOEXEC);
    if (descriptor < 0 || fstat(descriptor, &status) != 0) {
        const int code = errno;
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        set_file_error(error, "could not open Xbox hard disk", code);
        return false;
    }
    if (status.st_size < 0) {
        (void)close(descriptor);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "Xbox hard disk has an invalid file length"
        );
        return false;
    }
    /*
     * QEMU guards its images with fcntl byte-range locks; an OFD whole-file
     * write lock conflicts with them, so an image still open in an emulator
     * is refused instead of edited. flock covers platforms without OFD locks.
     */
#if defined(__linux__)
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(descriptor, F_OFD_SETLK, &lock) != 0) {
            (void)close(descriptor);
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "Xbox hard disk is in use by another process"
            );
            return false;
        }
    }
#else
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        (void)close(descriptor);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "Xbox hard disk is in use by another process"
        );
        return false;
    }
#endif
    file = malloc(sizeof(*file));
    if (file == NULL) {
        (void)close(descriptor);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Xbox hard-disk file"
        );
        return false;
    }
    file->descriptor = descriptor;
    *length = (uint64_t)status.st_size;
    *output = file;
    return true;
}

bool gdox_random_access_file_read(
    gdox_random_access_file *file,
    uint64_t offset,
    uint8_t *output,
    size_t bytes,
    gdox_error *error
)
{
    size_t completed = 0U;

    if (file == NULL || (bytes != 0U && output == NULL)
        || !valid_range(offset, bytes)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access read is invalid"
        );
        return false;
    }
    while (completed < bytes) {
        const ssize_t received = pread(
            file->descriptor,
            output + completed,
            bytes - completed,
            (off_t)(offset + completed)
        );
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            set_file_error(
                error,
                "could not read Xbox hard disk",
                received < 0 ? errno : EIO
            );
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

bool gdox_random_access_file_write(
    gdox_random_access_file *file,
    uint64_t offset,
    const uint8_t *input,
    size_t bytes,
    gdox_error *error
)
{
    size_t completed = 0U;

    if (file == NULL || (bytes != 0U && input == NULL)
        || !valid_range(offset, bytes)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access write is invalid"
        );
        return false;
    }
    while (completed < bytes) {
        const ssize_t written = pwrite(
            file->descriptor,
            input + completed,
            bytes - completed,
            (off_t)(offset + completed)
        );
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            set_file_error(
                error,
                "could not write Xbox hard disk",
                written < 0 ? errno : EIO
            );
            return false;
        }
        completed += (size_t)written;
    }
    return true;
}

bool gdox_random_access_file_sync_close(
    gdox_random_access_file *file,
    gdox_error *error
)
{
    int code = 0;
    bool success = true;

    if (file == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access file is required"
        );
        return false;
    }
    if (fsync(file->descriptor) != 0) {
        code = errno;
        success = false;
    }
    if (close(file->descriptor) != 0 && success) {
        code = errno;
        success = false;
    }
    free(file);
    if (!success) {
        set_file_error(error, "could not synchronize Xbox hard disk", code);
    }
    return success;
}

bool gdox_random_access_file_close(
    gdox_random_access_file *file,
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
        set_file_error(error, "could not close Xbox hard disk", errno);
        return false;
    }
    return true;
}
