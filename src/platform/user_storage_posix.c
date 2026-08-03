#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "platform/user_storage.h"

#include "gdox/hash.h"

#include <dirent.h>
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

bool gdox_storage_ordinary_file(
    const char *path,
    bool *found,
    gdox_error *error
)
{
    struct stat status;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || found == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "ordinary private file path and result are required"
        );
        return false;
    }
    *found = false;
    if (lstat(path, &status) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        set_errno_error(error, "could not inspect private file", errno);
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "private file is not an ordinary regular file"
        );
        return false;
    }
    *found = true;
    return true;
}

static bool xemu_removal_name(const char *name)
{
    static const char prefix[] = ".gdox-xbox-hdd-removal.";
    const size_t prefix_bytes = sizeof(prefix) - 1U;

    if (name == NULL
        || strlen(name) != prefix_bytes + 17U
        || memcmp(name, prefix, prefix_bytes) != 0
        || name[prefix_bytes + 8U] != '.') {
        return false;
    }
    for (size_t index = prefix_bytes; index < prefix_bytes + 8U; ++index) {
        if (!((name[index] >= '0' && name[index] <= '9')
                || (name[index] >= 'a' && name[index] <= 'f')
                || (name[index] >= 'A' && name[index] <= 'F'))) {
            return false;
        }
    }
    for (size_t index = prefix_bytes + 9U;
         index < prefix_bytes + 17U;
         ++index) {
        if (!((name[index] >= '0' && name[index] <= '9')
                || (name[index] >= 'a' && name[index] <= 'f')
                || (name[index] >= 'A' && name[index] <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool gdox_storage_xemu_pending_hdd(
    const char *managed_path,
    bool *found,
    uint64_t *bytes,
    gdox_error *error
)
{
    char parent[GDOX_STORAGE_PATH_CAPACITY];
    const char *slash;
    const char *basename;
    struct stat parent_status;
    DIR *listing = NULL;
    int parent_fd = -1;
    int listing_fd = -1;
    bool success = false;

    gdox_error_clear(error);
    if (managed_path == NULL || managed_path[0] != '/'
        || found == NULL || bytes == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "managed xemu HDD path and pending-removal result are required"
        );
        return false;
    }
    *found = false;
    *bytes = 0U;
    slash = strrchr(managed_path, '/');
    basename = slash != NULL ? slash + 1U : managed_path;
    if (slash == NULL || slash == managed_path
        || strcmp(basename, "xbox_hdd.qcow2") != 0
        || (size_t)(slash - managed_path) >= sizeof(parent)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "historical managed xemu HDD path is invalid"
        );
        return false;
    }
    memcpy(parent, managed_path, (size_t)(slash - managed_path));
    parent[slash - managed_path] = '\0';
    parent_fd = open(
        parent,
        O_RDONLY | O_DIRECTORY
#ifdef O_NOFOLLOW
            | O_NOFOLLOW
#endif
    );
    if (parent_fd < 0 && errno == ENOENT) {
        return true;
    }
    if (parent_fd < 0 || fstat(parent_fd, &parent_status) != 0
        || !S_ISDIR(parent_status.st_mode)
        || parent_status.st_uid != geteuid()
        || (parent_status.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        const int code = errno;

        if (parent_fd >= 0) {
            (void)close(parent_fd);
        }
        set_errno_error(
            error,
            "could not inspect private xemu storage directory",
            code != 0 ? code : EACCES
        );
        return false;
    }
    listing_fd = dup(parent_fd);
    if (listing_fd < 0) {
        set_errno_error(
            error, "could not duplicate xemu storage directory", errno
        );
        goto done;
    }
    listing = fdopendir(listing_fd);
    if (listing == NULL) {
        const int code = errno;

        (void)close(listing_fd);
        set_errno_error(error, "could not list xemu storage directory", code);
        goto done;
    }
    for (struct dirent *entry = readdir(listing);
         entry != NULL;
         entry = readdir(listing)) {
        struct stat candidate;
        int flags = O_RDONLY;
        int candidate_fd;

        if (!xemu_removal_name(entry->d_name)) {
            continue;
        }
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        candidate_fd = openat(parent_fd, entry->d_name, flags);
        if (candidate_fd < 0) {
            continue;
        }
        if (fstat(candidate_fd, &candidate) != 0
            || !S_ISREG(candidate.st_mode)
            || candidate.st_uid != geteuid()
            || candidate.st_nlink != 1
            || (candidate.st_mode & (S_IRWXG | S_IRWXO)) != 0
            || candidate.st_size <= 0) {
            (void)close(candidate_fd);
            continue;
        }
        (void)close(candidate_fd);
        if (*found) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "multiple pending xemu HDD removals require recovery"
            );
            goto done;
        }
        *found = true;
        *bytes = (uint64_t)candidate.st_size;
    }
    success = true;
done:
    if (listing != NULL) {
        (void)closedir(listing);
    }
    if (parent_fd >= 0) {
        (void)close(parent_fd);
    }
    if (!success) {
        *found = false;
        *bytes = 0U;
    }
    return success;
}

static bool same_file_times(
    const struct stat *left,
    const struct stat *right
)
{
#if defined(__APPLE__)
    return left->st_mtimespec.tv_sec == right->st_mtimespec.tv_sec
        && left->st_mtimespec.tv_nsec == right->st_mtimespec.tv_nsec
        && left->st_ctimespec.tv_sec == right->st_ctimespec.tv_sec
        && left->st_ctimespec.tv_nsec == right->st_ctimespec.tv_nsec;
#else
    return left->st_mtim.tv_sec == right->st_mtim.tv_sec
        && left->st_mtim.tv_nsec == right->st_mtim.tv_nsec
        && left->st_ctim.tv_sec == right->st_ctim.tv_sec
        && left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
#endif
}

bool gdox_storage_resolve_existing_path(
    const char *path,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL
        || realpath(path, output) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "private storage path is unavailable"
        );
        return false;
    }
    return true;
}

bool gdox_storage_directory_exists(const char *path)
{
    struct stat status;

    return path != NULL && lstat(path, &status) == 0
        && S_ISDIR(status.st_mode);
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

bool gdox_storage_ensure_private_directory(
    const char *path,
    gdox_error *error
)
{
    struct stat status;

    if (!gdox_storage_ensure_directory(path, error)) {
        return false;
    }
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode)
        || status.st_uid != geteuid() || (status.st_mode & 077U) != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "private storage directory must be owned by the current user, must not be a symlink, and must not grant group or other access"
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

bool gdox_storage_remove_exact_file(
    const char *path,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[GDOX_SHA256_BYTES],
    gdox_storage_remove_result *result,
    gdox_error *error
)
{
    uint8_t buffer[32U * 1024U];
    gdox_hash_stream *stream = NULL;
    gdox_hashes hashes;
    struct stat before;
    struct stat after;
    struct stat named;
    uint64_t completed = 0U;
    int file;
    bool success = false;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || expected_sha256 == NULL
        || result == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "exact file path, digest, and result are required"
        );
        return false;
    }
    *result = GDOX_STORAGE_REMOVE_NOT_FOUND;
    file = open(
        path,
        O_RDONLY | O_CLOEXEC
#if defined(O_NOFOLLOW)
            | O_NOFOLLOW
#endif
    );
    if (file < 0 && errno == ENOENT) {
        return true;
    }
#if defined(O_NOFOLLOW)
    if (file < 0 && errno == ELOOP) {
        *result = GDOX_STORAGE_REMOVE_MISMATCH;
        return true;
    }
#endif
    if (file < 0) {
        set_errno_error(error, "could not open exact private file", errno);
        return false;
    }
    if (fstat(file, &before) != 0) {
        set_errno_error(error, "could not inspect exact private file", errno);
        goto cleanup;
    }
    if (!S_ISREG(before.st_mode) || before.st_size < 0
        || (uint64_t)before.st_size != expected_bytes) {
        *result = GDOX_STORAGE_REMOVE_MISMATCH;
        success = true;
        goto cleanup;
    }
    if (!gdox_hash_stream_create(&stream, error)) {
        goto cleanup;
    }
    while (completed < expected_bytes) {
        const uint64_t remaining = expected_bytes - completed;
        const size_t request = remaining < sizeof(buffer)
            ? (size_t)remaining : sizeof(buffer);
        ssize_t received;

        do {
            received = read(file, buffer, request);
        } while (received < 0 && errno == EINTR);
        if (received <= 0) {
            if (received < 0) {
                set_errno_error(
                    error,
                    "could not read exact private file",
                    errno
                );
            } else {
                gdox_error_set(
                    error,
                    GDOX_ERROR_IO,
                    "exact private file changed while it was read"
                );
            }
            goto cleanup;
        }
        if (!gdox_hash_stream_update(
                stream,
                buffer,
                (size_t)received,
                error
            )) {
            goto cleanup;
        }
        completed += (size_t)received;
    }
    if (!gdox_hash_stream_finish(stream, &hashes, error)
        || fstat(file, &after) != 0) {
        if (!gdox_error_is_set(error)) {
            set_errno_error(
                error,
                "could not recheck exact private file",
                errno
            );
        }
        goto cleanup;
    }
    if (before.st_dev != after.st_dev || before.st_ino != after.st_ino
        || before.st_size != after.st_size
        || !same_file_times(&before, &after)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "exact private file changed while it was verified"
        );
        goto cleanup;
    }
    if (memcmp(
            hashes.sha256,
            expected_sha256,
            GDOX_SHA256_BYTES
        ) != 0) {
        *result = GDOX_STORAGE_REMOVE_MISMATCH;
        success = true;
        goto cleanup;
    }
    if (lstat(path, &named) != 0
        || named.st_dev != before.st_dev || named.st_ino != before.st_ino
        || !S_ISREG(named.st_mode) || named.st_size != before.st_size
        || !same_file_times(&before, &named)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "exact private file changed before removal"
        );
        goto cleanup;
    }
    if (unlink(path) != 0) {
        set_errno_error(error, "could not remove exact private file", errno);
        goto cleanup;
    }
    *result = GDOX_STORAGE_REMOVE_REMOVED;
    success = true;

cleanup:
    gdox_hash_stream_destroy(stream);
    (void)close(file);
    return success;
}
