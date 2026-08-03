#define WIN32_LEAN_AND_MEAN

#include "app/xenia_content_migration.h"

#include "app/xenia_content_policy.h"
#include "platform/session_storage.h"
#include "platform/windows_support.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_XENIA_PATH_CAPACITY 32768U

typedef struct content_type_list {
    char (*names)[9];
    size_t count;
    size_t capacity;
} content_type_list;

static bool absolute_path(const char *path)
{
    return path != NULL
        && ((((path[0] >= 'A' && path[0] <= 'Z')
              || (path[0] >= 'a' && path[0] <= 'z'))
             && path[1] == ':'
             && (path[2] == '/' || path[2] == '\\'))
            || (path[0] == '\\' && path[1] == '\\')
            || (path[0] == '/' && path[1] == '/'));
}

static bool wide_name_utf8(
    const wchar_t *name,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            name,
            -1,
            output,
            (int)GDOX_SESSION_PATH_CAPACITY,
            NULL,
            NULL
        ) == 0) {
        gdox_windows_io_error(
            error,
            "could not encode Xenia content entry",
            GetLastError()
        );
        return false;
    }
    return true;
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

static bool ordinary_directory(const wchar_t *path)
{
    const DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

static bool join_wide(
    wchar_t output[GDOX_WINDOWS_XENIA_PATH_CAPACITY],
    const wchar_t *parent,
    const wchar_t *child,
    gdox_error *error
)
{
    const int written = swprintf(
        output,
        GDOX_WINDOWS_XENIA_PATH_CAPACITY,
        L"%ls\\%ls",
        parent,
        child
    );

    if (written < 0
        || (size_t)written >= GDOX_WINDOWS_XENIA_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "Xenia content path is too long");
        return false;
    }
    return true;
}

static bool require_layout_directory(
    const char *root,
    const wchar_t *path,
    const char *relative,
    gdox_error *error
)
{
    if (!ordinary_directory(path)) {
        gdox_xenia_content_layout_error(root, relative, error);
        return false;
    }
    return true;
}

static bool remove_nonpersistent_types(
    const char *root,
    const wchar_t *title_directory,
    const char *title_relative,
    bool scan_headers,
    gdox_error *error
)
{
    content_type_list list = {0};
    wchar_t pattern[GDOX_WINDOWS_XENIA_PATH_CAPACITY];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    bool success = true;
    size_t index;

    if (!join_wide(pattern, title_directory, L"*", error)) {
        return false;
    }
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();

        if (code == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(error, "could not scan Xenia title content", code);
        return false;
    }
    do {
        char name[GDOX_SESSION_PATH_CAPACITY];
        char relative[GDOX_SESSION_PATH_CAPACITY];
        wchar_t entry_path[GDOX_WINDOWS_XENIA_PATH_CAPACITY];

        if (wcscmp(entry.cFileName, L".") == 0
            || wcscmp(entry.cFileName, L"..") == 0) {
            continue;
        }
        if (!wide_name_utf8(entry.cFileName, name, error)
            || !gdox_xenia_content_relative_path(
                relative,
                sizeof(relative),
                title_relative,
                name,
                error
            )) {
            success = false;
            break;
        }
        if (strcmp(name, "Headers") == 0) {
            if (!scan_headers
                || !join_wide(
                    entry_path, title_directory, entry.cFileName, error
                )) {
                if (!gdox_error_is_set(error)) {
                    gdox_xenia_content_layout_error(root, relative, error);
                }
                success = false;
                break;
            }
            if (!require_layout_directory(
                    root, entry_path, relative, error
                )
                || !remove_nonpersistent_types(
                    root, entry_path, relative, false, error
                )) {
                success = false;
                break;
            }
        } else if (!gdox_xenia_content_hexadecimal_name(name, 8U)) {
            gdox_xenia_content_layout_error(root, relative, error);
            success = false;
            break;
        } else if (gdox_xenia_content_persistent_type(name)) {
            if (!join_wide(
                    entry_path, title_directory, entry.cFileName, error
                )
                || !require_layout_directory(
                    root, entry_path, relative, error
                )) {
                success = false;
                break;
            }
        } else if (!remember_type(&list, name, error)) {
            success = false;
            break;
        }
    } while (FindNextFileW(search, &entry));
    if (success && GetLastError() != ERROR_NO_MORE_FILES) {
        gdox_windows_io_error(error, "could not enumerate Xenia title content", GetLastError());
        success = false;
    }
    (void)FindClose(search);
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
    const wchar_t *xuid_directory,
    const char *xuid,
    gdox_error *error
)
{
    wchar_t pattern[GDOX_WINDOWS_XENIA_PATH_CAPACITY];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    bool success = true;

    if (!join_wide(pattern, xuid_directory, L"*", error)) {
        return false;
    }
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();

        if (code == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(error, "could not scan Xenia profile content", code);
        return false;
    }
    do {
        char name[GDOX_SESSION_PATH_CAPACITY];
        char relative[GDOX_SESSION_PATH_CAPACITY];
        wchar_t title_path[GDOX_WINDOWS_XENIA_PATH_CAPACITY];

        if (wcscmp(entry.cFileName, L".") == 0
            || wcscmp(entry.cFileName, L"..") == 0) {
            continue;
        }
        if (!wide_name_utf8(entry.cFileName, name, error)
            || !gdox_xenia_content_relative_path(
                relative, sizeof(relative), xuid, name, error
            )) {
            success = false;
            break;
        }
        if (!gdox_xenia_content_hexadecimal_name(name, 8U)) {
            gdox_xenia_content_layout_error(root, relative, error);
            success = false;
            break;
        }
        if (!join_wide(
                title_path, xuid_directory, entry.cFileName, error
            )
            || !require_layout_directory(
                root, title_path, relative, error
            )
            || !remove_nonpersistent_types(
                root, title_path, relative, true, error
            )) {
            success = false;
            break;
        }
    } while (FindNextFileW(search, &entry));
    if (success && GetLastError() != ERROR_NO_MORE_FILES) {
        gdox_windows_io_error(error, "could not enumerate Xenia profile content", GetLastError());
        success = false;
    }
    (void)FindClose(search);
    return success;
}

bool gdox_xenia_content_migrate(
    const char *content_root,
    gdox_error *error
)
{
    wchar_t *root;
    wchar_t pattern[GDOX_WINDOWS_XENIA_PATH_CAPACITY];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    bool success = true;

    gdox_error_clear(error);
    if (!absolute_path(content_root)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "absolute Xenia content root is required");
        return false;
    }
    root = gdox_windows_wide_path(content_root, error);
    if (root == NULL) {
        return false;
    }
    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();

        free(root);
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(error, "could not inspect Xenia content root", code);
        return false;
    }
    if (!ordinary_directory(root) || !join_wide(pattern, root, L"*", error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_IO, "Xenia content root is not an ordinary directory");
        }
        free(root);
        return false;
    }
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();

        free(root);
        if (code == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(error, "could not scan Xenia content root", code);
        return false;
    }
    do {
        char name[GDOX_SESSION_PATH_CAPACITY];
        const size_t characters = wcslen(entry.cFileName);
        wchar_t child_path[GDOX_WINDOWS_XENIA_PATH_CAPACITY];

        if (wcscmp(entry.cFileName, L".") == 0
            || wcscmp(entry.cFileName, L"..") == 0) {
            continue;
        }
        if (!wide_name_utf8(entry.cFileName, name, error)) {
            success = false;
            break;
        }
        if ((characters != 8U && characters != 16U)
            || !gdox_xenia_content_hexadecimal_name(name, characters)) {
            gdox_xenia_content_layout_error(content_root, name, error);
            success = false;
            break;
        }
        if (!join_wide(child_path, root, entry.cFileName, error)
            || !require_layout_directory(
                content_root, child_path, name, error
            )) {
            success = false;
            break;
        }
        success = characters == 8U
            ? remove_nonpersistent_types(
                content_root, child_path, name, true, error
            )
            : scan_xuid_directory(
                content_root, child_path, name, error
            );
        if (!success) {
            break;
        }
    } while (FindNextFileW(search, &entry));
    if (success && GetLastError() != ERROR_NO_MORE_FILES) {
        gdox_windows_io_error(error, "could not enumerate Xenia content root", GetLastError());
        success = false;
    }
    (void)FindClose(search);
    free(root);
    return success;
}
