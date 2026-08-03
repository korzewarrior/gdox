#define _POSIX_C_SOURCE 200809L

#include "app/xenia_content_migration.h"

#include "app/xenia_content_policy.h"
#include "platform/session_storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

#if !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif

typedef struct content_type_list {
    char (*names)[9];
    size_t count;
    size_t capacity;
} content_type_list;

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

static bool remember_type(
    content_type_list *list,
    const char *name,
    gdox_error *error
)
{
    if (list->count == list->capacity) {
        const size_t capacity = list->capacity == 0U
            ? 8U : list->capacity * 2U;
        char (*grown)[9];

        if (capacity < list->capacity
            || capacity > SIZE_MAX / sizeof(*grown)) {
            gdox_error_set(error, GDOX_ERROR_IO, "Xenia content list is too large");
            return false;
        }
        grown = realloc(list->names, capacity * sizeof(*grown));
        if (grown == NULL) {
            gdox_error_set(error, GDOX_ERROR_IO, "could not allocate Xenia content list");
            return false;
        }
        list->names = grown;
        list->capacity = capacity;
    }
    memcpy(list->names[list->count++], name, 9U);
    return true;
}

static int open_layout_directory(
    const char *root,
    int parent,
    const char *name,
    const char *relative,
    gdox_error *error
)
{
    struct stat status;
    int directory;

    if (fstatat(parent, name, &status, AT_SYMLINK_NOFOLLOW) != 0) {
        set_errno_error(error, "could not inspect Xenia content entry", errno);
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        gdox_xenia_content_layout_error(root, relative, error);
        return -1;
    }
    directory = openat(
        parent,
        name,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (directory < 0) {
        if (errno == ENOTDIR || errno == ELOOP) {
            gdox_xenia_content_layout_error(root, relative, error);
        } else {
            set_errno_error(error, "could not open Xenia content entry", errno);
        }
    }
    return directory;
}

static bool remove_nonpersistent_types(
    const char *root,
    int title_directory,
    const char *title_relative,
    bool scan_headers,
    gdox_error *error
)
{
    content_type_list list = {0};
    const int duplicate = dup(title_directory);
    DIR *stream;
    struct dirent *entry;
    bool success = true;
    size_t index;

    if (duplicate < 0) {
        set_errno_error(error, "could not scan Xenia title content", errno);
        return false;
    }
    stream = fdopendir(duplicate);
    if (stream == NULL) {
        const int code = errno;

        (void)close(duplicate);
        set_errno_error(error, "could not scan Xenia title content", code);
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        char relative[GDOX_SESSION_PATH_CAPACITY];

        if (strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (!gdox_xenia_content_relative_path(
                relative,
                sizeof(relative),
                title_relative,
                entry->d_name,
                error
            )) {
            success = false;
            break;
        }
        if (strcmp(entry->d_name, "Headers") == 0) {
            int headers;

            if (!scan_headers) {
                gdox_xenia_content_layout_error(root, relative, error);
                success = false;
                break;
            }
            headers = open_layout_directory(
                root, title_directory, entry->d_name, relative, error
            );
            if (headers < 0) {
                success = false;
                break;
            }
            success = remove_nonpersistent_types(
                root, headers, relative, false, error
            );
            (void)close(headers);
            if (!success) {
                break;
            }
        } else if (!gdox_xenia_content_hexadecimal_name(
                       entry->d_name, 8U
                   )) {
            gdox_xenia_content_layout_error(root, relative, error);
            success = false;
            break;
        } else if (gdox_xenia_content_persistent_type(entry->d_name)) {
            const int persistent = open_layout_directory(
                root, title_directory, entry->d_name, relative, error
            );

            if (persistent < 0) {
                success = false;
                break;
            }
            (void)close(persistent);
        } else if (!remember_type(&list, entry->d_name, error)) {
            success = false;
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        set_errno_error(error, "could not enumerate Xenia title content", errno);
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        set_errno_error(error, "could not close Xenia title content", errno);
        success = false;
    }
    for (index = 0U; success && index < list.count; ++index) {
        char relative[GDOX_SESSION_PATH_CAPACITY];

        success = gdox_xenia_content_relative_path(
            relative,
            sizeof(relative),
            title_relative,
            list.names[index],
            error
        ) && gdox_session_storage_remove_relative(root, relative, error);
    }
    free(list.names);
    return success;
}

static bool scan_xuid_directory(
    const char *root,
    int xuid_directory,
    const char *xuid,
    gdox_error *error
)
{
    const int duplicate = dup(xuid_directory);
    DIR *stream;
    struct dirent *entry;
    bool success = true;

    if (duplicate < 0) {
        set_errno_error(error, "could not scan Xenia profile content", errno);
        return false;
    }
    stream = fdopendir(duplicate);
    if (stream == NULL) {
        const int code = errno;

        (void)close(duplicate);
        set_errno_error(error, "could not scan Xenia profile content", code);
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        char relative[GDOX_SESSION_PATH_CAPACITY];
        int title;

        if (strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (!gdox_xenia_content_relative_path(
                relative, sizeof(relative), xuid, entry->d_name, error
            )) {
            success = false;
            break;
        }
        if (!gdox_xenia_content_hexadecimal_name(entry->d_name, 8U)) {
            gdox_xenia_content_layout_error(root, relative, error);
            success = false;
            break;
        }
        title = open_layout_directory(
            root, xuid_directory, entry->d_name, relative, error
        );
        if (title < 0) {
            success = false;
            break;
        }
        success = remove_nonpersistent_types(
            root, title, relative, true, error
        );
        (void)close(title);
        if (!success) {
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        set_errno_error(error, "could not enumerate Xenia profile content", errno);
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        set_errno_error(error, "could not close Xenia profile content", errno);
        success = false;
    }
    return success;
}

bool gdox_xenia_content_migrate(
    const char *content_root,
    gdox_error *error
)
{
    struct stat status;
    int root;
    int duplicate;
    DIR *stream;
    struct dirent *entry;
    bool success = true;

    gdox_error_clear(error);
    if (content_root == NULL || content_root[0] != '/') {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "absolute Xenia content root is required");
        return false;
    }
    if (lstat(content_root, &status) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        set_errno_error(error, "could not inspect Xenia content root", errno);
        return false;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid()
        || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        gdox_error_set(error, GDOX_ERROR_IO, "Xenia content root is not caller-owned");
        return false;
    }
    root = open(
        content_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root < 0) {
        set_errno_error(error, "could not open Xenia content root", errno);
        return false;
    }
    duplicate = dup(root);
    if (duplicate < 0) {
        const int code = errno;

        (void)close(root);
        set_errno_error(error, "could not duplicate Xenia content root", code);
        return false;
    }
    stream = fdopendir(duplicate);
    if (stream == NULL) {
        const int code = errno;

        (void)close(duplicate);
        (void)close(root);
        set_errno_error(error, "could not scan Xenia content root", code);
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        const size_t characters = strlen(entry->d_name);
        char relative[GDOX_SESSION_PATH_CAPACITY];
        int child;

        if (strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (!gdox_xenia_content_relative_path(
                relative, sizeof(relative), "", entry->d_name, error
            )) {
            success = false;
            break;
        }
        if ((characters != 8U && characters != 16U)
            || !gdox_xenia_content_hexadecimal_name(
                entry->d_name, characters
            )) {
            gdox_xenia_content_layout_error(content_root, relative, error);
            success = false;
            break;
        }
        child = open_layout_directory(
            content_root, root, entry->d_name, relative, error
        );
        if (child < 0) {
            success = false;
            break;
        }
        success = characters == 8U
            ? remove_nonpersistent_types(
                content_root, child, relative, true, error
            )
            : scan_xuid_directory(
                content_root, child, relative, error
            );
        (void)close(child);
        if (!success) {
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        set_errno_error(error, "could not enumerate Xenia content root", errno);
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        set_errno_error(error, "could not close Xenia content root", errno);
        success = false;
    }
    (void)close(root);
    return success;
}
