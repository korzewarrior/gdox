#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/session_storage.h"
#include "platform/session_storage_policy.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/vfs.h>
#define GDOX_TMPFS_MAGIC 0x01021994L
#endif

#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

#if !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif

enum { GDOX_SESSION_PARENT_RACE_ATTEMPTS = 8U };

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

static bool canonicalize_directory(
    char path[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    char canonical[GDOX_SESSION_PATH_CAPACITY];
    int written;

    if (realpath(path, canonical) == NULL) {
        set_errno_error(error, "could not resolve session storage base", errno);
        return false;
    }
    written = snprintf(path, GDOX_SESSION_PATH_CAPACITY, "%s", canonical);
    if (written < 0 || (size_t)written >= GDOX_SESSION_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage base path is too long"
        );
        return false;
    }
    return true;
}

static bool private_directory(const char *path, gdox_error *error)
{
    struct stat status;

    if (lstat(path, &status) != 0) {
        set_errno_error(error, "could not inspect private directory", errno);
        return false;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid()
        || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "session storage directory is not private and caller-owned"
        );
        return false;
    }
    return true;
}

static bool choose_temporary_base(
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    const char *base = getenv("GDOX_SESSION_HOME");
    int written;

    if (base == NULL || base[0] == '\0') {
#if defined(__APPLE__) && defined(_CS_DARWIN_USER_TEMP_DIR)
        const size_t required = confstr(_CS_DARWIN_USER_TEMP_DIR, NULL, 0U);

        if (required > 1U && required <= GDOX_SESSION_PATH_CAPACITY
            && confstr(_CS_DARWIN_USER_TEMP_DIR, output, required) != 0U) {
            const size_t bytes = strlen(output);

            if (bytes > 1U && output[bytes - 1U] == '/') {
                output[bytes - 1U] = '\0';
            }
            return output[0] == '/'
                && canonicalize_directory(output, error);
        }
#endif
#if defined(__linux__)
        base = getenv("XDG_RUNTIME_DIR");
        if (base != NULL && base[0] == '/'
            && private_directory(base, error)) {
            written = snprintf(output, GDOX_SESSION_PATH_CAPACITY, "%s", base);
            return written >= 0
                && (size_t)written < GDOX_SESSION_PATH_CAPACITY
                && canonicalize_directory(output, error);
        }
        gdox_error_clear(error);
#endif
        base = getenv("TMPDIR");
        if (base == NULL || base[0] != '/') {
            base = "/tmp";
        }
    }
    if (base[0] != '/') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage base must be absolute"
        );
        return false;
    }
    written = snprintf(output, GDOX_SESSION_PATH_CAPACITY, "%s", base);
    if (written < 0 || (size_t)written >= GDOX_SESSION_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage base path is too long"
        );
        return false;
    }
    return canonicalize_directory(output, error);
}

static bool format_named_session_parent(
    const char *base,
    const char *name,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    int written;

    written = snprintf(
        output,
        GDOX_SESSION_PATH_CAPACITY,
        "%s/%s-%lu",
        base,
        name,
        (unsigned long)geteuid()
    );
    if (written < 0 || (size_t)written >= GDOX_SESSION_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage path is too long"
        );
        return false;
    }
    return true;
}

static bool format_session_parent(
    const char *base,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    return format_named_session_parent(
        base, "gdox-session", output, error
    );
}

static bool ensure_session_parent_at(
    const char *base,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    unsigned int attempt;

    if (!format_session_parent(base, output, error)) {
        return false;
    }
    for (attempt = 0U;
         attempt < GDOX_SESSION_PARENT_RACE_ATTEMPTS;
         ++attempt) {
        struct stat status;
        int directory;

        if (mkdir(output, 0700) != 0 && errno != EEXIST) {
            set_errno_error(
                error, "could not create session storage parent", errno
            );
            return false;
        }
        directory = open(
            output, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );
        if (directory < 0) {
            if (errno == ENOENT) {
                continue;
            }
            set_errno_error(
                error, "could not open session storage parent", errno
            );
            return false;
        }
        if (fstat(directory, &status) != 0) {
            const int code = errno;

            (void)close(directory);
            set_errno_error(
                error, "could not inspect session storage parent", code
            );
            return false;
        }
        if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid()
            || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            (void)close(directory);
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "session storage parent is not private and caller-owned"
            );
            return false;
        }
        if (fchmod(directory, 0700) != 0) {
            const int code = errno;

            (void)close(directory);
            set_errno_error(
                error, "could not secure session storage parent", code
            );
            return false;
        }
        (void)close(directory);
        return true;
    }
    set_errno_error(
        error, "session storage parent remained unavailable", ENOENT
    );
    return false;
}

static bool session_parent(
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    char base[GDOX_SESSION_PATH_CAPACITY];

    return choose_temporary_base(base, error)
        && ensure_session_parent_at(base, output, error);
}

static bool memory_session_base(
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
#if defined(__linux__)
    static const char base[] = "/dev/shm";
    struct statfs filesystem;
    int written;

    if (statfs(base, &filesystem) != 0) {
        set_errno_error(error, "could not inspect memory session storage", errno);
        return false;
    }
    if ((long)filesystem.f_type != GDOX_TMPFS_MAGIC) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "a verified memory-backed session filesystem is required"
        );
        return false;
    }
    written = snprintf(output, GDOX_SESSION_PATH_CAPACITY, "%s", base);
    return written >= 0 && (size_t)written < GDOX_SESSION_PATH_CAPACITY;
#else
    (void)output;
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "this platform has no verified memory-backed session filesystem"
    );
    return false;
#endif
}

static bool memory_session_parent(
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    char base[GDOX_SESSION_PATH_CAPACITY];

    return memory_session_base(base, error)
        && ensure_session_parent_at(base, output, error);
}

static bool remove_entry_at(
    int parent,
    const char *name,
    gdox_error *error
);

static bool private_regular_file(const struct stat *status)
{
    return S_ISREG(status->st_mode) && status->st_uid == geteuid()
        && (status->st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

static bool owner_marker_matches(int directory, const char *name)
{
    char expected[GDOX_SESSION_MARKER_CAPACITY];
    char actual[GDOX_SESSION_MARKER_CAPACITY];
    struct stat status;
    size_t expected_bytes;
    size_t offset = 0U;
    int marker;

    if (!gdox_session_owner_marker_format(
            name, expected, &expected_bytes
        )) {
        return false;
    }
    marker = openat(
        directory,
        GDOX_SESSION_OWNER_MARKER,
        O_RDONLY | O_NOFOLLOW | O_CLOEXEC
    );
    if (marker < 0 || fstat(marker, &status) != 0
        || !private_regular_file(&status)
        || status.st_size < 0
        || (uint64_t)status.st_size != (uint64_t)expected_bytes) {
        if (marker >= 0) {
            (void)close(marker);
        }
        return false;
    }
    while (offset < expected_bytes) {
        const ssize_t read_bytes = read(
            marker, actual + offset, expected_bytes - offset
        );

        if (read_bytes <= 0) {
            (void)close(marker);
            return false;
        }
        offset += (size_t)read_bytes;
    }
    (void)close(marker);
    return memcmp(actual, expected, expected_bytes) == 0;
}

static gdox_session_recovery_state inspect_session_lock(
    int parent,
    const char *name,
    int *lock_handle,
    gdox_error *error
)
{
    struct stat directory_status;
    struct stat lock_status;
    int directory;
    int lock;

    *lock_handle = -1;
    if (strncmp(name, "session-", 8U) != 0
        || fstatat(parent, name, &directory_status, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISDIR(directory_status.st_mode)
        || directory_status.st_uid != geteuid()
        || (directory_status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    directory = openat(
        parent,
        name,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (directory < 0) {
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    if (!owner_marker_matches(directory, name)) {
        (void)close(directory);
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    lock = openat(
        directory,
        GDOX_SESSION_LOCK_FILE,
        O_RDWR | O_NOFOLLOW | O_CLOEXEC
    );
    if (lock < 0 || fstat(lock, &lock_status) != 0
        || !private_regular_file(&lock_status)) {
        if (lock >= 0) {
            (void)close(lock);
        }
        (void)close(directory);
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    (void)close(directory);
    if (flock(lock, LOCK_EX | LOCK_NB) == 0) {
        *lock_handle = lock;
        return gdox_session_recovery_decide(
            true, GDOX_SESSION_LOCK_ACQUIRED
        );
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        (void)close(lock);
        return gdox_session_recovery_decide(
            true, GDOX_SESSION_LOCK_CONTENDED
        );
    }
    set_errno_error(error, "could not verify session ownership lock", errno);
    (void)close(lock);
    return gdox_session_recovery_decide(
        true, GDOX_SESSION_LOCK_FAILED
    );
}

static bool recover_sessions(int directory, gdox_error *error)
{
    int scan;
    DIR *stream;
    struct dirent *entry;
    bool success = true;

    scan = dup(directory);
    stream = scan >= 0 ? fdopendir(scan) : NULL;
    if (stream == NULL) {
        const int code = errno;

        if (scan >= 0) {
            (void)close(scan);
        }
        set_errno_error(error, "could not scan session parent", code);
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        int lock_handle;
        const gdox_session_recovery_state state = inspect_session_lock(
            directory, entry->d_name, &lock_handle, error
        );

        if (state == GDOX_SESSION_RECOVERY_ERROR) {
            success = false;
            break;
        }
        if (state == GDOX_SESSION_RECOVERY_STALE) {
            success = remove_entry_at(directory, entry->d_name, error);
            (void)close(lock_handle);
            if (!success) {
                break;
            }
        }
        errno = 0;
    }
    if (success && errno != 0) {
        set_errno_error(error, "could not enumerate session parent", errno);
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        set_errno_error(error, "could not close session parent", errno);
        success = false;
    }
    return success;
}

static bool recover_named_parent(
    const char *base,
    const char *name,
    gdox_error *error
)
{
    char parent[GDOX_SESSION_PATH_CAPACITY];
    struct stat status;
    int directory;
    bool success;

    if (!format_named_session_parent(base, name, parent, error)) {
        return false;
    }
    directory = open(
        parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (directory < 0) {
        if (errno == ENOENT) {
            return true;
        }
        set_errno_error(error, "could not open session parent", errno);
        return false;
    }
    if (fstat(directory, &status) != 0
        || !S_ISDIR(status.st_mode) || status.st_uid != geteuid()
        || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        const int code = errno;

        (void)close(directory);
        if (code != 0) {
            set_errno_error(error, "could not inspect session parent", code);
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "session storage parent is not private and caller-owned"
            );
        }
        return false;
    }
    success = recover_sessions(directory, error);
    (void)close(directory);
    return success;
}

static bool recover_parent(
    const char *base,
    gdox_error *error
)
{
    return recover_named_parent(base, "gdox-session", error);
}

static bool remove_directory_contents(int directory, gdox_error *error)
{
    DIR *stream;
    struct dirent *entry;
    int scan;
    bool success = true;

    scan = dup(directory);
    if (scan < 0) {
        set_errno_error(error, "could not scan session directory", errno);
        return false;
    }
    stream = fdopendir(scan);
    if (stream == NULL) {
        const int code = errno;

        (void)close(scan);
        set_errno_error(error, "could not scan session directory", code);
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        struct stat status;

        if (strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (fstatat(
                directory, entry->d_name, &status, AT_SYMLINK_NOFOLLOW
            ) != 0) {
            if (errno == ENOENT) {
                errno = 0;
                continue;
            }
            set_errno_error(error, "could not inspect session entry", errno);
            success = false;
            break;
        }
        if (S_ISDIR(status.st_mode)) {
            const int child = openat(
                directory,
                entry->d_name,
                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
            );

            if (child < 0) {
                set_errno_error(error, "could not open session directory", errno);
                success = false;
                break;
            }
            success = remove_directory_contents(child, error);
            (void)close(child);
            if (!success) {
                break;
            }
            if (unlinkat(directory, entry->d_name, AT_REMOVEDIR) != 0
                && errno != ENOENT) {
                set_errno_error(error, "could not remove session directory", errno);
                success = false;
                break;
            }
        } else if (unlinkat(directory, entry->d_name, 0) != 0
                   && errno != ENOENT) {
            set_errno_error(error, "could not remove session file", errno);
            success = false;
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        set_errno_error(error, "could not enumerate session directory", errno);
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        set_errno_error(error, "could not close session directory", errno);
        success = false;
    }
    return success;
}

static bool remove_entry_at(
    int parent,
    const char *name,
    gdox_error *error
)
{
    struct stat status;

    if (fstatat(parent, name, &status, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        set_errno_error(error, "could not inspect private storage entry", errno);
        return false;
    }
    if (S_ISDIR(status.st_mode)) {
        const int child = openat(
            parent,
            name,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );

        if (child < 0) {
            set_errno_error(error, "could not open private storage entry", errno);
            return false;
        }
        if (!remove_directory_contents(child, error)) {
            (void)close(child);
            return false;
        }
        (void)close(child);
        if (unlinkat(parent, name, AT_REMOVEDIR) != 0 && errno != ENOENT) {
            set_errno_error(error, "could not remove private storage directory", errno);
            return false;
        }
        return true;
    }
    if (unlinkat(parent, name, 0) != 0 && errno != ENOENT) {
        set_errno_error(error, "could not remove private storage file", errno);
        return false;
    }
    return true;
}

bool gdox_session_storage_remove_relative(
    const char *root,
    const char *relative,
    gdox_error *error
)
{
    char path[GDOX_SESSION_PATH_CAPACITY];
    char *component;
    char *next;
    int directory;
    bool success = false;

    gdox_error_clear(error);
    if (root == NULL || root[0] != '/'
        || !gdox_session_relative_path_is_safe(relative)
        || !private_directory(root, error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "private root and safe relative path are required"
            );
        }
        return false;
    }
    if (snprintf(path, sizeof(path), "%s", relative) < 0
        || strlen(relative) >= sizeof(path)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "relative path is too long");
        return false;
    }
    directory = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) {
        set_errno_error(error, "could not open private storage root", errno);
        return false;
    }
    component = path;
    while ((next = strchr(component, '/')) != NULL) {
        int child;

        *next = '\0';
        child = openat(
            directory,
            component,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );
        if (child < 0) {
            if (errno == ENOENT) {
                success = true;
            } else {
                set_errno_error(error, "could not open private storage parent", errno);
            }
            (void)close(directory);
            return success;
        }
        (void)close(directory);
        directory = child;
        component = next + 1U;
    }
    success = remove_entry_at(directory, component, error);
    (void)close(directory);
    return success;
}

bool gdox_session_storage_recover(gdox_error *error)
{
    char base[GDOX_SESSION_PATH_CAPACITY];

    gdox_error_clear(error);
    return choose_temporary_base(base, error)
        && recover_parent(base, error);
}

bool gdox_session_storage_recover_memory(gdox_error *error)
{
    char base[GDOX_SESSION_PATH_CAPACITY];

    gdox_error_clear(error);
    return memory_session_base(base, error)
        && recover_parent(base, error);
}

static bool create_session(
    gdox_session_storage *storage,
    bool memory_backed,
    gdox_error *error
)
{
    char parent[GDOX_SESSION_PATH_CAPACITY];
    char marker[GDOX_SESSION_MARKER_CAPACITY];
    const char *name;
    size_t marker_bytes;
    size_t marker_offset = 0U;
    int directory = -1;
    int marker_file = -1;
    int lock_file = -1;
    int written = -1;
    int creation_error = 0;
    unsigned int attempt;
    bool created = false;

    gdox_error_clear(error);
    if (storage == NULL || storage->active) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "inactive session storage is required"
        );
        return false;
    }
    memset(storage, 0, sizeof(*storage));
    storage->lock_handle = -1;
    for (attempt = 0U;
         attempt < GDOX_SESSION_PARENT_RACE_ATTEMPTS;
         ++attempt) {
        if (!(memory_backed
                ? memory_session_parent(parent, error)
                : session_parent(parent, error))) {
            return false;
        }
        written = snprintf(
            storage->root,
            sizeof(storage->root),
            "%s/session-%lu-XXXXXX",
            parent,
            (unsigned long)getpid()
        );
        if (written < 0 || (size_t)written >= sizeof(storage->root)) {
            break;
        }
        if (mkdtemp(storage->root) != NULL) {
            created = true;
            break;
        }
        creation_error = errno;
        if (creation_error != ENOENT && creation_error != EINVAL) {
            break;
        }
    }
    if (!created) {
        if (written >= 0 && (size_t)written < sizeof(storage->root)) {
            set_errno_error(
                error,
                "could not create session directory",
                creation_error != 0 ? creation_error : ENOENT
            );
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "session directory path is too long"
            );
        }
        storage->root[0] = '\0';
        return false;
    }
    if (chmod(storage->root, 0700) != 0) {
        const int code = errno;
        char relative[64];
        gdox_error ignored;

        name = strrchr(storage->root, '/');
        if (name != NULL) {
            (void)snprintf(relative, sizeof(relative), "%s", name + 1U);
            (void)gdox_session_storage_remove_relative(parent, relative, &ignored);
        }
        storage->root[0] = '\0';
        set_errno_error(error, "could not secure session directory", code);
        return false;
    }
    name = strrchr(storage->root, '/');
    directory = open(
        storage->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (name == NULL || directory < 0
        || !gdox_session_owner_marker_format(
            name + 1U, marker, &marker_bytes
        )) {
        if (directory >= 0) {
            (void)close(directory);
        }
        gdox_error_set(
            error, GDOX_ERROR_IO, "could not initialize session ownership"
        );
        goto ownership_failure;
    }
    lock_file = openat(
        directory,
        GDOX_SESSION_LOCK_FILE,
        O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0600
    );
    if (lock_file < 0 || flock(lock_file, LOCK_EX | LOCK_NB) != 0) {
        const int code = errno;

        if (lock_file >= 0) {
            (void)close(lock_file);
            lock_file = -1;
        }
        set_errno_error(error, "could not acquire session ownership lock", code);
        goto ownership_failure;
    }
    marker_file = openat(
        directory,
        GDOX_SESSION_OWNER_MARKER,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        0600
    );
    if (marker_file < 0) {
        set_errno_error(error, "could not create session owner marker", errno);
        goto ownership_failure;
    }
    while (marker_offset < marker_bytes) {
        const ssize_t written_bytes = write(
            marker_file,
            marker + marker_offset,
            marker_bytes - marker_offset
        );

        if (written_bytes <= 0) {
            set_errno_error(error, "could not write session owner marker", errno);
            goto ownership_failure;
        }
        marker_offset += (size_t)written_bytes;
    }
    if (fsync(marker_file) != 0) {
        const int code = errno;

        (void)close(marker_file);
        marker_file = -1;
        set_errno_error(error, "could not publish session owner marker", code);
        goto ownership_failure;
    }
    if (close(marker_file) != 0) {
        const int code = errno;

        marker_file = -1;
        set_errno_error(error, "could not close session owner marker", code);
        goto ownership_failure;
    }
    marker_file = -1;
    (void)close(directory);
    storage->lock_handle = (intptr_t)lock_file;
    storage->active = true;
    storage->memory_backed = memory_backed;
    return true;

ownership_failure:
    {
        gdox_error ignored;
        char relative[64];

        if (marker_file >= 0) {
            (void)close(marker_file);
        }
        if (lock_file >= 0) {
            (void)close(lock_file);
        }
        if (directory >= 0) {
            (void)close(directory);
        }
        if (name != NULL) {
            (void)snprintf(relative, sizeof(relative), "%s", name + 1U);
            (void)gdox_session_storage_remove_relative(
                parent, relative, &ignored
            );
        }
        memset(storage, 0, sizeof(*storage));
        storage->lock_handle = -1;
        return false;
    }
}

bool gdox_session_storage_create(
    gdox_session_storage *storage,
    gdox_error *error
)
{
    return create_session(storage, false, error);
}

bool gdox_session_storage_create_memory(
    gdox_session_storage *storage,
    gdox_error *error
)
{
    return create_session(storage, true, error);
}

bool gdox_session_storage_path(
    const gdox_session_storage *storage,
    const char *relative,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    int written;

    gdox_error_clear(error);
    if (storage == NULL || !storage->active
        || !gdox_session_relative_path_is_safe(relative)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "active session storage and safe relative path are required"
        );
        return false;
    }
    written = snprintf(output, GDOX_SESSION_PATH_CAPACITY, "%s/%s", storage->root, relative);
    if (written < 0 || (size_t)written >= GDOX_SESSION_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "session path is too long");
        return false;
    }
    return true;
}

static bool same_file_identity(
    const struct stat *left,
    const struct stat *right
)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static bool remove_owned_session_contents(
    int directory,
    gdox_error *error
)
{
    const int scan = dup(directory);
    DIR *stream;
    struct dirent *entry;
    bool success = true;

    if (scan < 0) {
        set_errno_error(error, "could not scan owned session", errno);
        return false;
    }
    stream = fdopendir(scan);
    if (stream == NULL) {
        const int code = errno;

        (void)close(scan);
        set_errno_error(error, "could not scan owned session", code);
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0
            || strcmp(entry->d_name, GDOX_SESSION_OWNER_MARKER) == 0
            || strcmp(entry->d_name, GDOX_SESSION_LOCK_FILE) == 0) {
            errno = 0;
            continue;
        }
        if (!remove_entry_at(directory, entry->d_name, error)) {
            success = false;
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        set_errno_error(error, "could not enumerate owned session", errno);
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        set_errno_error(error, "could not close owned session", errno);
        success = false;
    }
    return success;
}

bool gdox_session_storage_cleanup(
    gdox_session_storage *storage,
    gdox_error *error
)
{
    char parent[GDOX_SESSION_PATH_CAPACITY];
    const char *name;
    struct stat held_lock;
    struct stat path_lock;
    int parent_directory = -1;
    int session_directory = -1;
    bool removed = false;

    gdox_error_clear(error);
    if (storage == NULL || !storage->active) {
        return true;
    }
    name = strrchr(storage->root, '/');
    if (name == NULL || strncmp(name + 1U, "session-", 8U) != 0
        || storage->lock_handle < 0
        || !(storage->memory_backed
            ? memory_session_parent(parent, error)
            : session_parent(parent, error))
        || (size_t)(name - storage->root) != strlen(parent)
        || strncmp(storage->root, parent, strlen(parent)) != 0) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "session storage ownership could not be verified"
            );
        }
        return false;
    }
    parent_directory = open(
        parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (parent_directory >= 0) {
        session_directory = openat(
            parent_directory,
            name + 1U,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );
    }
    if (parent_directory < 0 || session_directory < 0
        || !owner_marker_matches(session_directory, name + 1U)
        || fstat((int)storage->lock_handle, &held_lock) != 0
        || fstatat(
            session_directory,
            GDOX_SESSION_LOCK_FILE,
            &path_lock,
            AT_SYMLINK_NOFOLLOW
        ) != 0
        || !private_regular_file(&path_lock)
        || !same_file_identity(&held_lock, &path_lock)) {
        if (session_directory >= 0) {
            (void)close(session_directory);
        }
        if (parent_directory >= 0) {
            (void)close(parent_directory);
        }
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage marker and lock identity could not be verified"
        );
        return false;
    }
    removed = remove_owned_session_contents(session_directory, error);
    if (removed
        && unlinkat(
            session_directory, GDOX_SESSION_OWNER_MARKER, 0
        ) != 0 && errno != ENOENT) {
        set_errno_error(error, "could not remove session owner marker", errno);
        removed = false;
    }
    if (removed
        && unlinkat(
            session_directory, GDOX_SESSION_LOCK_FILE, 0
        ) != 0 && errno != ENOENT) {
        set_errno_error(error, "could not remove session ownership lock", errno);
        removed = false;
    }
    (void)close(session_directory);
    if (removed
        && unlinkat(parent_directory, name + 1U, AT_REMOVEDIR) != 0
        && errno != ENOENT) {
        set_errno_error(error, "could not remove owned session", errno);
        removed = false;
    }
    (void)close(parent_directory);
    if (!removed) {
        return false;
    }
    (void)close((int)storage->lock_handle);
    memset(storage, 0, sizeof(*storage));
    storage->lock_handle = -1;
    return true;
}
