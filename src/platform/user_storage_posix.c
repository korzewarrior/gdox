#define _POSIX_C_SOURCE 200809L

#include "platform/user_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum storage_root {
    STORAGE_CONFIG = 0,
    STORAGE_DATA,
} storage_root;

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

static bool safe_relative(const char *relative)
{
    const char *cursor = relative;
    if (relative == NULL || relative[0] == '\0' || relative[0] == '/') {
        return false;
    }
    while (*cursor != '\0') {
        const char *slash = strchr(cursor, '/');
        const size_t bytes = slash != NULL
            ? (size_t)(slash - cursor)
            : strlen(cursor);
        if (bytes == 0U
            || (bytes == 1U && cursor[0] == '.')
            || (bytes == 2U && cursor[0] == '.' && cursor[1] == '.')) {
            return false;
        }
        if (slash == NULL) {
            break;
        }
        cursor = slash + 1U;
    }
    return true;
}

static bool user_path(
    storage_root kind,
    const char *relative,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    const char *home = getenv("HOME");
    const char *override = kind == STORAGE_CONFIG
        ? getenv("GDOX_CONFIG_HOME")
        : getenv("GDOX_DATA_HOME");
    int result;

    if (!safe_relative(relative)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "safe relative storage path is required");
        return false;
    }
    if (override != NULL && override[0] != '\0') {
        result = snprintf(
            output,
            GDOX_STORAGE_PATH_CAPACITY,
            "%s/%s",
            override,
            relative
        );
#if defined(__APPLE__)
    } else if (home != NULL && home[0] != '\0') {
        result = snprintf(
            output,
            GDOX_STORAGE_PATH_CAPACITY,
            "%s/Library/Application Support/org.gdox.gdox/%s",
            home,
            relative
        );
    } else {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "user storage directory is unavailable");
        return false;
#else
    } else {
        const char *xdg = getenv(
            kind == STORAGE_CONFIG ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME"
        );
        if (xdg != NULL && xdg[0] == '/') {
            result = snprintf(
                output,
                GDOX_STORAGE_PATH_CAPACITY,
                "%s/gdox/%s",
                xdg,
                relative
            );
        } else if (home != NULL && home[0] != '\0') {
            result = snprintf(
                output,
                GDOX_STORAGE_PATH_CAPACITY,
                kind == STORAGE_CONFIG
                    ? "%s/.config/gdox/%s"
                    : "%s/.local/share/gdox/%s",
                home,
                relative
            );
        } else {
            gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "user storage directory is unavailable");
            return false;
        }
#endif
    }
    if (result < 0 || (size_t)result >= GDOX_STORAGE_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "user storage path is too long");
        return false;
    }
    return true;
}

bool gdox_user_config_path(
    const char *relative,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    return user_path(STORAGE_CONFIG, relative, output, error);
}

bool gdox_user_data_path(
    const char *relative,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    return user_path(STORAGE_DATA, relative, output, error);
}

static bool create_parent_directories(
    char path[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    char *cursor = path;
    char *slash = strrchr(path, '/');

    if (slash == NULL) {
        return true;
    }
    *slash = '\0';
    if (path[0] == '/') {
        cursor = path + 1U;
    }
    while (*cursor != '\0') {
        struct stat status;
        char *next = strchr(cursor, '/');
        if (next != NULL) {
            *next = '\0';
        }
        if (mkdir(path, 0700) != 0 && errno != EEXIST) {
            const int code = errno;
            if (next != NULL) {
                *next = '/';
            }
            *slash = '/';
            set_errno_error(error, "could not create private directory", code);
            return false;
        }
        if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) {
            if (next != NULL) {
                *next = '/';
            }
            *slash = '/';
            gdox_error_set(error, GDOX_ERROR_IO, "private storage parent is not a directory");
            return false;
        }
        if (next == NULL) {
            break;
        }
        *next = '/';
        cursor = next + 1U;
    }
    *slash = '/';
    return true;
}

bool gdox_storage_file_size(const char *path, uint64_t *bytes)
{
    struct stat status;
    if (path == NULL || bytes == NULL || stat(path, &status) != 0
        || !S_ISREG(status.st_mode) || status.st_size < 0) {
        return false;
    }
    *bytes = (uint64_t)status.st_size;
    return true;
}

bool gdox_storage_ensure_directory(
    const char *path,
    gdox_error *error
)
{
    char child[GDOX_STORAGE_PATH_CAPACITY];
    struct stat status;
    int formatted;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private directory path is required"
        );
        return false;
    }
    formatted = snprintf(child, sizeof(child), "%s/.gdox", path);
    if (formatted < 0 || (size_t)formatted >= sizeof(child)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private directory path is too long"
        );
        return false;
    }
    if (!create_parent_directories(child, error)) {
        return false;
    }
    if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "private storage path is not a directory"
        );
        return false;
    }
    return true;
}

bool gdox_storage_read(
    const char *path,
    size_t maximum_bytes,
    uint8_t **data,
    size_t *bytes,
    bool *found,
    gdox_error *error
)
{
    struct stat status;
    uint8_t *buffer;
    size_t completed = 0U;
    int file;

    gdox_error_clear(error);
    if (path == NULL || data == NULL || bytes == NULL || found == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "storage read outputs are required");
        return false;
    }
    *data = NULL;
    *bytes = 0U;
    *found = false;
    file = open(path, O_RDONLY | O_CLOEXEC
#if defined(O_NOFOLLOW)
        | O_NOFOLLOW
#endif
    );
    if (file < 0 && errno == ENOENT) {
        return true;
    }
    if (file < 0) {
        set_errno_error(error, "could not open private file", errno);
        return false;
    }
    if (fstat(file, &status) != 0) {
        const int code = errno;
        (void)close(file);
        set_errno_error(error, "could not inspect private file", code);
        return false;
    }
    if (!S_ISREG(status.st_mode) || status.st_size < 0
        || (uint64_t)status.st_size > maximum_bytes) {
        (void)close(file);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "private file is not a bounded regular file");
        return false;
    }
    buffer = malloc((size_t)status.st_size + 1U);
    if (buffer == NULL) {
        (void)close(file);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate private file");
        return false;
    }
    while (completed < (size_t)status.st_size) {
        const ssize_t received = read(
            file,
            buffer + completed,
            (size_t)status.st_size - completed
        );
        if (received > 0) {
            completed += (size_t)received;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            const int code = errno;
            free(buffer);
            (void)close(file);
            set_errno_error(error, "could not read private file", code);
            return false;
        }
    }
    if (close(file) != 0) {
        const int code = errno;
        free(buffer);
        set_errno_error(error, "could not close private file", code);
        return false;
    }
    buffer[completed] = 0U;
    *data = buffer;
    *bytes = completed;
    *found = true;
    return true;
}

bool gdox_storage_write_private(
    const char *path,
    const uint8_t *data,
    size_t bytes,
    bool replace,
    gdox_error *error
)
{
    char mutable_path[GDOX_STORAGE_PATH_CAPACITY];
    char temporary[GDOX_STORAGE_PATH_CAPACITY + 48U];
    size_t completed = 0U;
    int formatted;
    int file;

    gdox_error_clear(error);
    if (path == NULL || (bytes != 0U && data == NULL)
        || strlen(path) >= sizeof(mutable_path)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "private file path and data are required");
        return false;
    }
    memcpy(mutable_path, path, strlen(path) + 1U);
    if (!create_parent_directories(mutable_path, error)) {
        return false;
    }
    if (!replace && access(path, F_OK) == 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "private file already exists");
        return false;
    }
    formatted = snprintf(
        temporary,
        sizeof(temporary),
        "%s.%ld.tmp",
        path,
        (long)getpid()
    );
    if (formatted < 0 || (size_t)formatted >= sizeof(temporary)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "private file path is too long");
        return false;
    }
    (void)unlink(temporary);
    file = open(
        temporary,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
        0600
    );
    if (file < 0) {
        set_errno_error(error, "could not create private file update", errno);
        return false;
    }
    while (completed < bytes) {
        const ssize_t written = write(file, data + completed, bytes - completed);
        if (written > 0) {
            completed += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            const int code = errno;
            (void)close(file);
            (void)unlink(temporary);
            set_errno_error(error, "could not write private file", code);
            return false;
        }
    }
    if (fsync(file) != 0 || close(file) != 0) {
        const int code = errno;
        (void)unlink(temporary);
        set_errno_error(error, "could not synchronize private file", code);
        return false;
    }
    if (replace) {
        if (rename(temporary, path) != 0) {
            const int code = errno;
            (void)unlink(temporary);
            set_errno_error(error, "could not commit private file", code);
            return false;
        }
    } else {
        if (link(temporary, path) != 0 || unlink(temporary) != 0) {
            const int code = errno;
            (void)unlink(temporary);
            set_errno_error(error, "could not commit private file", code);
            return false;
        }
    }
    return true;
}

bool gdox_storage_copy_private(
    const char *source,
    const char *destination,
    bool replace,
    gdox_error *error
)
{
    uint8_t *data = NULL;
    size_t bytes = 0U;
    bool found = false;
    bool success;

    if (!gdox_storage_read(
            source,
            (size_t)64U * 1024U * 1024U,
            &data,
            &bytes,
            &found,
            error
        )) {
        return false;
    }
    if (!found) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "private file source was not found");
        return false;
    }
    success = gdox_storage_write_private(
        destination,
        data,
        bytes,
        replace,
        error
    );
    free(data);
    return success;
}
