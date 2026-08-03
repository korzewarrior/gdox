#define WIN32_LEAN_AND_MEAN

#include "platform/session_storage.h"
#include "platform/session_storage_policy.h"
#include "platform/windows_support.h"

#include <windows.h>
#include <bcrypt.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_SESSION_PATH_CAPACITY 32768U
static const wchar_t session_owner_marker_name[] = L".gdox-session-owner";
static const wchar_t session_lock_file_name[] = L".gdox-session-lock";

static bool wide_to_utf8(
    const wchar_t *wide,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide,
            -1,
            output,
            (int)GDOX_SESSION_PATH_CAPACITY,
            NULL,
            NULL
        ) == 0) {
        gdox_windows_io_error(
            error,
            "could not encode session storage path",
            GetLastError()
        );
        return false;
    }
    return true;
}

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

static bool append_component(
    wchar_t path[GDOX_WINDOWS_SESSION_PATH_CAPACITY],
    const wchar_t *component,
    gdox_error *error
)
{
    size_t path_bytes = wcslen(path);
    const size_t component_bytes = wcslen(component);

    if (path_bytes + component_bytes + 2U
        > GDOX_WINDOWS_SESSION_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage path is too long"
        );
        return false;
    }
    if (path_bytes != 0U
        && path[path_bytes - 1U] != L'\\'
        && path[path_bytes - 1U] != L'/') {
        path[path_bytes++] = L'\\';
        path[path_bytes] = L'\0';
    }
    memcpy(
        path + path_bytes,
        component,
        (component_bytes + 1U) * sizeof(*component)
    );
    return true;
}

static bool ordinary_directory(const wchar_t *path, gdox_error *error)
{
    const DWORD attributes = GetFileAttributesW(path);

    /*
     * Windows traversal is bounded by reparse-point rejection. Directory
     * access remains governed by the inherited ACL of TEMP or the explicit
     * test override; this function does not assert filesystem ownership.
     */
    if (attributes == INVALID_FILE_ATTRIBUTES
        || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U
        || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "session storage path is not an ordinary directory"
        );
        return false;
    }
    return true;
}

static bool named_session_parent_path(
    const wchar_t *name,
    wchar_t output[GDOX_WINDOWS_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    wchar_t override[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    DWORD length;

    length = GetEnvironmentVariableW(
        L"GDOX_SESSION_HOME",
        override,
        GDOX_WINDOWS_SESSION_PATH_CAPACITY
    );
    if (length > 0U && length < GDOX_WINDOWS_SESSION_PATH_CAPACITY) {
        if (GetFullPathNameW(
                override,
                GDOX_WINDOWS_SESSION_PATH_CAPACITY,
                output,
                NULL
            ) == 0U) {
            gdox_windows_io_error(
                error,
                "could not resolve session storage base",
                GetLastError()
            );
            return false;
        }
    } else {
        length = GetTempPathW(GDOX_WINDOWS_SESSION_PATH_CAPACITY, output);
        if (length == 0U || length >= GDOX_WINDOWS_SESSION_PATH_CAPACITY) {
            gdox_windows_io_error(
                error,
                "could not locate temporary storage",
                GetLastError()
            );
            return false;
        }
    }
    if (!append_component(output, name, error)) {
        return false;
    }
    return true;
}

static bool session_parent_path(
    wchar_t output[GDOX_WINDOWS_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    return named_session_parent_path(L"gdox-session", output, error);
}

static bool session_parent(
    wchar_t output[GDOX_WINDOWS_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    bool created;

    if (!session_parent_path(output, error)) {
        return false;
    }
    if (!gdox_windows_ensure_private_directory(output, &created, error)) {
        return false;
    }
    if (ordinary_directory(output, error)
        && gdox_windows_verify_private_directory(output, error)) {
        return true;
    }
    if (created) {
        (void)RemoveDirectoryW(output);
    }
    return false;
}

static bool remove_empty_parent(
    const wchar_t *parent,
    gdox_error *error
)
{
    DWORD code;

    if (RemoveDirectoryW(parent)) {
        return true;
    }
    code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
        || code == ERROR_DIR_NOT_EMPTY || code == ERROR_ALREADY_EXISTS) {
        return true;
    }
    gdox_windows_io_error(
        error, "could not remove empty session parent", code
    );
    return false;
}

static bool remove_tree(const wchar_t *path, gdox_error *error)
{
    wchar_t pattern[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    bool success = true;

    if (swprintf(
            pattern,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\*",
            path
        ) < 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "session path is too long");
        return false;
    }
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();

        if (code != ERROR_FILE_NOT_FOUND) {
            gdox_windows_io_error(error, "could not scan session directory", code);
            return false;
        }
    } else {
        do {
            wchar_t child[GDOX_WINDOWS_SESSION_PATH_CAPACITY];

            if (wcscmp(entry.cFileName, L".") == 0
                || wcscmp(entry.cFileName, L"..") == 0) {
                continue;
            }
            if (swprintf(
                    child,
                    GDOX_WINDOWS_SESSION_PATH_CAPACITY,
                    L"%ls\\%ls",
                    path,
                    entry.cFileName
                ) < 0) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_INVALID_ARGUMENT,
                    "session child path is too long"
                );
                success = false;
                break;
            }
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U
                    && !remove_tree(child, error)) {
                    success = false;
                    break;
                }
                if (!RemoveDirectoryW(child)
                    && GetLastError() != ERROR_FILE_NOT_FOUND
                    && GetLastError() != ERROR_PATH_NOT_FOUND) {
                    gdox_windows_io_error(
                        error,
                        "could not remove session directory",
                        GetLastError()
                    );
                    success = false;
                    break;
                }
            } else if (!DeleteFileW(child)
                       && GetLastError() != ERROR_FILE_NOT_FOUND) {
                gdox_windows_io_error(
                    error,
                    "could not remove session file",
                    GetLastError()
                );
                success = false;
                break;
            }
        } while (FindNextFileW(search, &entry));
        if (success && GetLastError() != ERROR_NO_MORE_FILES) {
            gdox_windows_io_error(
                error,
                "could not enumerate session directory",
                GetLastError()
            );
            success = false;
        }
        (void)FindClose(search);
    }
    return success;
}

static bool validate_relative_parents(
    const wchar_t *root,
    const wchar_t *relative,
    gdox_error *error
)
{
    wchar_t current[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    const wchar_t *cursor = relative;
    const wchar_t *slash;

    if (swprintf(
            current,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls",
            root
        ) < 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "managed root is too long");
        return false;
    }
    while ((slash = wcschr(cursor, L'\\')) != NULL) {
        wchar_t component[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
        const size_t bytes = (size_t)(slash - cursor);

        if (bytes == 0U || bytes >= GDOX_WINDOWS_SESSION_PATH_CAPACITY) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "storage path component is invalid"
            );
            return false;
        }
        memcpy(component, cursor, bytes * sizeof(*component));
        component[bytes] = L'\0';
        if (!append_component(current, component, error)) {
            return false;
        }
        {
            const DWORD attributes = GetFileAttributesW(current);

            if (attributes == INVALID_FILE_ATTRIBUTES) {
                return GetLastError() == ERROR_FILE_NOT_FOUND
                    || GetLastError() == ERROR_PATH_NOT_FOUND;
            }
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U
                || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_IO,
                    "storage parent is not an ordinary directory"
                );
                return false;
            }
        }
        cursor = slash + 1U;
    }
    return true;
}

bool gdox_session_storage_remove_relative(
    const char *root,
    const char *relative,
    gdox_error *error
)
{
    wchar_t *wide_root;
    wchar_t *wide_relative;
    wchar_t target[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    DWORD attributes;
    bool success;

    gdox_error_clear(error);
    if (!absolute_path(root)
        || !gdox_session_relative_path_is_safe(relative)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "managed root and safe relative path are required"
        );
        return false;
    }
    wide_root = gdox_windows_wide_path(root, error);
    wide_relative = gdox_windows_wide_path(relative, error);
    if (wide_root == NULL || wide_relative == NULL) {
        free(wide_root);
        free(wide_relative);
        return false;
    }
    for (wchar_t *cursor = wide_relative; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'/') {
            *cursor = L'\\';
        }
    }
    if (!ordinary_directory(wide_root, error)
        || !validate_relative_parents(wide_root, wide_relative, error)
        || swprintf(
            target,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            wide_root,
            wide_relative
        ) < 0) {
        free(wide_root);
        free(wide_relative);
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "managed path is too long");
        }
        return false;
    }
    attributes = GetFileAttributesW(target);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();

        free(wide_root);
        free(wide_relative);
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(error, "could not inspect managed storage entry", code);
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        success = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U
            || remove_tree(target, error);
        if (success && !RemoveDirectoryW(target)
            && GetLastError() != ERROR_FILE_NOT_FOUND
            && GetLastError() != ERROR_PATH_NOT_FOUND) {
            gdox_windows_io_error(
                error,
                "could not remove managed storage directory",
                GetLastError()
            );
            success = false;
        }
    } else {
        success = DeleteFileW(target) != 0
            || GetLastError() == ERROR_FILE_NOT_FOUND;
        if (!success) {
            gdox_windows_io_error(
                error,
                "could not remove managed storage file",
                GetLastError()
            );
        }
    }
    free(wide_root);
    free(wide_relative);
    return success;
}

static bool format_wide_owner_marker(
    const wchar_t *name,
    char output[GDOX_SESSION_MARKER_CAPACITY],
    size_t *bytes
)
{
    char utf8_name[256];

    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            name,
            -1,
            utf8_name,
            (int)sizeof(utf8_name),
            NULL,
            NULL
        ) == 0) {
        return false;
    }
    return gdox_session_owner_marker_format(
        utf8_name, output, bytes
    );
}

static bool ordinary_file_information(
    const BY_HANDLE_FILE_INFORMATION *information
)
{
    return (information->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U
        && (information->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

static bool owner_marker_matches(
    const wchar_t *session,
    const wchar_t *name
)
{
    wchar_t marker_path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    char expected[GDOX_SESSION_MARKER_CAPACITY];
    char actual[GDOX_SESSION_MARKER_CAPACITY];
    BY_HANDLE_FILE_INFORMATION information;
    size_t expected_bytes;
    DWORD read_bytes = 0U;
    HANDLE marker;
    bool matches = false;

    if (!format_wide_owner_marker(name, expected, &expected_bytes)
        || swprintf(
            marker_path,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            session,
            session_owner_marker_name
        ) < 0) {
        return false;
    }
    marker = CreateFileW(
        marker_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );
    if (marker != INVALID_HANDLE_VALUE
        && GetFileInformationByHandle(marker, &information)
        && ordinary_file_information(&information)
        && information.nFileSizeHigh == 0U
        && information.nFileSizeLow == (DWORD)expected_bytes
        && ReadFile(
            marker,
            actual,
            (DWORD)expected_bytes,
            &read_bytes,
            NULL
        ) && read_bytes == (DWORD)expected_bytes
        && memcmp(actual, expected, expected_bytes) == 0) {
        matches = true;
    }
    if (marker != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(marker);
    }
    return matches;
}

static gdox_session_recovery_state inspect_session_lock(
    const wchar_t *parent,
    const wchar_t *name,
    HANDLE *lock_handle,
    gdox_error *error
)
{
    wchar_t session[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    wchar_t lock_path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    BY_HANDLE_FILE_INFORMATION information;
    gdox_error ignored;
    OVERLAPPED overlapped = {0};
    DWORD attributes;
    HANDLE lock;

    *lock_handle = INVALID_HANDLE_VALUE;
    if (wcsncmp(name, L"session-", 8U) != 0
        || swprintf(
            session,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            parent,
            name
        ) < 0) {
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    attributes = GetFileAttributesW(session);
    if (attributes == INVALID_FILE_ATTRIBUTES
        || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U
        || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U
        || !gdox_windows_verify_private_directory(session, &ignored)
        || !owner_marker_matches(session, name)
        || swprintf(
            lock_path,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            session,
            session_lock_file_name
        ) < 0) {
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    lock = CreateFileW(
        lock_path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );
    if (lock == INVALID_HANDLE_VALUE
        || !GetFileInformationByHandle(lock, &information)
        || !ordinary_file_information(&information)) {
        if (lock != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(lock);
        }
        return gdox_session_recovery_decide(
            false, GDOX_SESSION_LOCK_NOT_INSPECTED
        );
    }
    if (LockFileEx(
            lock,
            LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0U,
            1U,
            0U,
            &overlapped
        )) {
        *lock_handle = lock;
        return gdox_session_recovery_decide(
            true, GDOX_SESSION_LOCK_ACQUIRED
        );
    }
    if (GetLastError() == ERROR_LOCK_VIOLATION) {
        (void)CloseHandle(lock);
        return gdox_session_recovery_decide(
            true, GDOX_SESSION_LOCK_CONTENDED
        );
    }
    gdox_windows_io_error(
        error, "could not verify session ownership lock", GetLastError()
    );
    (void)CloseHandle(lock);
    return gdox_session_recovery_decide(
        true, GDOX_SESSION_LOCK_FAILED
    );
}

static bool recover_named_parent(
    const wchar_t *name,
    gdox_error *error
)
{
    wchar_t parent[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    char utf8_parent[GDOX_SESSION_PATH_CAPACITY];
    DWORD attributes;
    DWORD code;

    if (!named_session_parent_path(name, parent, error)) {
        return false;
    }
    attributes = GetFileAttributesW(parent);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(
            error, "could not inspect session parent", code
        );
        return false;
    }
    if (!ordinary_directory(parent, error)
        || !gdox_windows_verify_private_directory(parent, error)
        || !wide_to_utf8(parent, utf8_parent, error)) {
        return false;
    }
    {
        wchar_t pattern[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
        WIN32_FIND_DATAW entry;
        HANDLE search;
        bool success = true;

        if (swprintf(
                pattern,
                GDOX_WINDOWS_SESSION_PATH_CAPACITY,
                L"%ls\\session-*",
                parent
            ) < 0) {
            gdox_error_set(
                error, GDOX_ERROR_INVALID_ARGUMENT, "session path is too long"
            );
            return false;
        }
        search = FindFirstFileW(pattern, &entry);
        if (search == INVALID_HANDLE_VALUE) {
            code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND) {
                gdox_windows_io_error(
                    error, "could not enumerate session parent", code
                );
                return false;
            }
        } else {
            do {
                HANDLE lock_handle;
                const gdox_session_recovery_state state = inspect_session_lock(
                    parent, entry.cFileName, &lock_handle, error
                );

                if (state == GDOX_SESSION_RECOVERY_ERROR) {
                    success = false;
                    break;
                }
                if (state == GDOX_SESSION_RECOVERY_STALE) {
                    char relative[GDOX_SESSION_PATH_CAPACITY];
                    OVERLAPPED overlapped = {0};

                    (void)UnlockFileEx(
                        lock_handle, 0U, 1U, 0U, &overlapped
                    );
                    (void)CloseHandle(lock_handle);
                    success = wide_to_utf8(
                        entry.cFileName, relative, error
                    ) && gdox_session_storage_remove_relative(
                        utf8_parent, relative, error
                    );
                    if (!success) {
                        break;
                    }
                }
            } while (FindNextFileW(search, &entry));
            if (success && GetLastError() != ERROR_NO_MORE_FILES) {
                gdox_windows_io_error(
                    error,
                    "could not enumerate session parent",
                    GetLastError()
                );
                success = false;
            }
            (void)FindClose(search);
        }
        if (!success) {
            return false;
        }
    }
    return remove_empty_parent(parent, error);
}

bool gdox_session_storage_recover(gdox_error *error)
{
    gdox_error_clear(error);
    return recover_named_parent(L"gdox-session", error);
}

bool gdox_session_storage_recover_memory(gdox_error *error)
{
    gdox_error_clear(error);
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "Windows has no verified memory-backed session filesystem"
    );
    return false;
}

static bool write_owner_marker(
    const wchar_t *session,
    const wchar_t *name,
    gdox_error *error
)
{
    wchar_t marker_path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    char marker[GDOX_SESSION_MARKER_CAPACITY];
    size_t marker_bytes;
    DWORD written_bytes = 0U;
    HANDLE file;
    bool success;

    if (!format_wide_owner_marker(name, marker, &marker_bytes)
        || swprintf(
            marker_path,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            session,
            session_owner_marker_name
        ) < 0) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "session marker is too long"
        );
        return false;
    }
    file = CreateFileW(
        marker_path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        gdox_windows_io_error(
            error, "could not create session owner marker", GetLastError()
        );
        return false;
    }
    success = WriteFile(
        file,
        marker,
        (DWORD)marker_bytes,
        &written_bytes,
        NULL
    ) && written_bytes == (DWORD)marker_bytes
        && FlushFileBuffers(file);
    if (!success) {
        gdox_windows_io_error(
            error, "could not publish session owner marker", GetLastError()
        );
    }
    (void)CloseHandle(file);
    return success;
}

static HANDLE create_ownership_lock(
    const wchar_t *session,
    gdox_error *error
)
{
    wchar_t lock_path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    OVERLAPPED overlapped = {0};
    HANDLE lock;

    if (swprintf(
            lock_path,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            session,
            session_lock_file_name
        ) < 0) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "session lock path is too long"
        );
        return INVALID_HANDLE_VALUE;
    }
    lock = CreateFileW(
        lock_path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
        NULL
    );
    if (lock == INVALID_HANDLE_VALUE
        || !LockFileEx(
            lock,
            LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0U,
            1U,
            0U,
            &overlapped
        )) {
        const DWORD code = GetLastError();

        if (lock != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(lock);
        }
        gdox_windows_io_error(
            error, "could not acquire session ownership lock", code
        );
        return INVALID_HANDLE_VALUE;
    }
    return lock;
}

bool gdox_session_storage_create(
    gdox_session_storage *storage,
    gdox_error *error
)
{
    static const wchar_t hex[] = L"0123456789abcdef";
    wchar_t parent[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    gdox_error ignored;
    unsigned int attempt;

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
    if (!session_parent(parent, error)) {
        return false;
    }
    for (attempt = 0U; attempt < 32U; ++attempt) {
        uint8_t random[16];
        wchar_t name[8U + 10U + 1U + sizeof(random) * 2U + 1U] = {0};
        wchar_t path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
        HANDLE lock;
        bool created;
        int prefix;
        size_t index;

        if (BCryptGenRandom(
                NULL,
                random,
                (ULONG)sizeof(random),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG
            ) != 0) {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not generate a session identifier"
            );
            (void)remove_empty_parent(parent, &ignored);
            return false;
        }
        prefix = swprintf(
            name,
            sizeof(name) / sizeof(name[0]),
            L"session-%lu-",
            (unsigned long)GetCurrentProcessId()
        );
        if (prefix < 0) {
            gdox_error_set(
                error, GDOX_ERROR_INTERNAL, "could not format session identifier"
            );
            (void)remove_empty_parent(parent, &ignored);
            return false;
        }
        for (index = 0U; index < sizeof(random); ++index) {
            name[(size_t)prefix + index * 2U] = hex[random[index] >> 4U];
            name[(size_t)prefix + index * 2U + 1U] =
                hex[random[index] & 0x0fU];
        }
        if (swprintf(
                path,
                GDOX_WINDOWS_SESSION_PATH_CAPACITY,
                L"%ls\\%ls",
                parent,
                name
            ) < 0) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "session path is too long");
            (void)remove_empty_parent(parent, &ignored);
            return false;
        }
        if (!gdox_windows_ensure_private_directory(path, &created, error)) {
            (void)remove_empty_parent(parent, &ignored);
            return false;
        }
        if (created) {
            if (!ordinary_directory(path, error)
                || !gdox_windows_verify_private_directory(path, error)
                || !write_owner_marker(path, name, error)
                || (lock = create_ownership_lock(path, error))
                    == INVALID_HANDLE_VALUE) {
                (void)remove_tree(path, &ignored);
                (void)RemoveDirectoryW(path);
                (void)remove_empty_parent(parent, &ignored);
                return false;
            }
            if (!wide_to_utf8(path, storage->root, error)) {
                OVERLAPPED overlapped = {0};

                (void)UnlockFileEx(lock, 0U, 1U, 0U, &overlapped);
                (void)CloseHandle(lock);
                (void)remove_tree(path, &ignored);
                (void)RemoveDirectoryW(path);
                (void)remove_empty_parent(parent, &ignored);
                return false;
            }
            storage->lock_handle = (intptr_t)lock;
            storage->active = true;
            return true;
        }
    }
    gdox_error_set(
        error,
        GDOX_ERROR_IO,
        "could not allocate a unique session directory"
    );
    (void)remove_empty_parent(parent, &ignored);
    return false;
}

bool gdox_session_storage_create_memory(
    gdox_session_storage *storage,
    gdox_error *error
)
{
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
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "Windows has no verified memory-backed session filesystem"
    );
    return false;
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
    written = snprintf(
        output,
        GDOX_SESSION_PATH_CAPACITY,
        "%s/%s",
        storage->root,
        relative
    );
    if (written < 0 || (size_t)written >= GDOX_SESSION_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "session path is too long");
        return false;
    }
    return true;
}

static bool same_file_identity(
    const BY_HANDLE_FILE_INFORMATION *left,
    const BY_HANDLE_FILE_INFORMATION *right
)
{
    return left->dwVolumeSerialNumber == right->dwVolumeSerialNumber
        && left->nFileIndexHigh == right->nFileIndexHigh
        && left->nFileIndexLow == right->nFileIndexLow;
}

static bool remove_owned_session_contents(
    const wchar_t *path,
    gdox_error *error
)
{
    wchar_t pattern[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    bool success = true;

    if (swprintf(
            pattern,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\*",
            path
        ) < 0) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "owned session path is too long"
        );
        return false;
    }
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        gdox_windows_io_error(
            error, "could not scan owned session", GetLastError()
        );
        return false;
    }
    do {
        wchar_t child[GDOX_WINDOWS_SESSION_PATH_CAPACITY];

        if (wcscmp(entry.cFileName, L".") == 0
            || wcscmp(entry.cFileName, L"..") == 0
            || wcscmp(entry.cFileName, session_owner_marker_name) == 0
            || wcscmp(entry.cFileName, session_lock_file_name) == 0) {
            continue;
        }
        if (swprintf(
                child,
                GDOX_WINDOWS_SESSION_PATH_CAPACITY,
                L"%ls\\%ls",
                path,
                entry.cFileName
            ) < 0) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "owned session child path is too long"
            );
            success = false;
            break;
        }
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U
                && !remove_tree(child, error)) {
                success = false;
                break;
            }
            if (!RemoveDirectoryW(child)) {
                gdox_windows_io_error(
                    error,
                    "could not remove owned session directory",
                    GetLastError()
                );
                success = false;
                break;
            }
        } else if (!DeleteFileW(child)) {
            gdox_windows_io_error(
                error,
                "could not remove owned session file",
                GetLastError()
            );
            success = false;
            break;
        }
    } while (FindNextFileW(search, &entry));
    if (success && GetLastError() != ERROR_NO_MORE_FILES) {
        gdox_windows_io_error(
            error, "could not enumerate owned session", GetLastError()
        );
        success = false;
    }
    (void)FindClose(search);
    return success;
}

bool gdox_session_storage_cleanup(
    gdox_session_storage *storage,
    gdox_error *error
)
{
    wchar_t parent[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    wchar_t session[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    wchar_t lock_path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    wchar_t marker_path[GDOX_WINDOWS_SESSION_PATH_CAPACITY];
    char utf8_parent[GDOX_SESSION_PATH_CAPACITY];
    const char *separator;
    const wchar_t *wide_name;
    wchar_t *wide_session = NULL;
    BY_HANDLE_FILE_INFORMATION held_information;
    BY_HANDLE_FILE_INFORMATION path_information;
    HANDLE held_lock;
    HANDLE path_lock = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped = {0};
    bool released = false;

    gdox_error_clear(error);
    if (storage == NULL || !storage->active) {
        return true;
    }
    separator = strrchr(storage->root, '/');
    if (separator == NULL) {
        separator = strrchr(storage->root, '\\');
    }
    held_lock = (HANDLE)storage->lock_handle;
    if (separator == NULL || strncmp(separator + 1U, "session-", 8U) != 0
        || held_lock == NULL || held_lock == INVALID_HANDLE_VALUE
        || !session_parent_path(parent, error)
        || !ordinary_directory(parent, error)
        || !gdox_windows_verify_private_directory(parent, error)
        || !wide_to_utf8(parent, utf8_parent, error)
        || (size_t)(separator - storage->root) != strlen(utf8_parent)
        || _strnicmp(storage->root, utf8_parent, strlen(utf8_parent)) != 0) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "session storage path identity could not be verified"
            );
        }
        return false;
    }
    wide_session = gdox_windows_wide_path(storage->root, error);
    if (wide_session == NULL) {
        return false;
    }
    wide_name = wcsrchr(wide_session, L'\\');
    if (wide_name == NULL) {
        wide_name = wcsrchr(wide_session, L'/');
    }
    if (wide_name == NULL
        || swprintf(
            session,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            parent,
            wide_name + 1U
        ) < 0
        || _wcsicmp(session, wide_session) != 0
        || !ordinary_directory(session, error)
        || !gdox_windows_verify_private_directory(session, error)
        || !owner_marker_matches(session, wide_name + 1U)
        || swprintf(
            lock_path,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            session,
            session_lock_file_name
        ) < 0
        || swprintf(
            marker_path,
            GDOX_WINDOWS_SESSION_PATH_CAPACITY,
            L"%ls\\%ls",
            session,
            session_owner_marker_name
        ) < 0
        || !GetFileInformationByHandle(held_lock, &held_information)) {
        free(wide_session);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage marker and lock identity could not be verified"
        );
        return false;
    }
    path_lock = CreateFileW(
        lock_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );
    if (path_lock == INVALID_HANDLE_VALUE
        || !GetFileInformationByHandle(path_lock, &path_information)
        || !ordinary_file_information(&path_information)
        || !same_file_identity(&held_information, &path_information)) {
        if (path_lock != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(path_lock);
        }
        free(wide_session);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "session storage lock identity could not be verified"
        );
        return false;
    }
    (void)CloseHandle(path_lock);
    if (!remove_owned_session_contents(session, error)
        || !DeleteFileW(marker_path)) {
        if (!gdox_error_is_set(error)) {
            gdox_windows_io_error(
                error, "could not remove session owner marker", GetLastError()
            );
        }
        free(wide_session);
        return false;
    }
    (void)UnlockFileEx(held_lock, 0U, 1U, 0U, &overlapped);
    (void)CloseHandle(held_lock);
    storage->lock_handle = -1;
    released = DeleteFileW(lock_path) != 0 && RemoveDirectoryW(session) != 0;
    if (!released) {
        gdox_windows_io_error(
            error, "could not remove owned session", GetLastError()
        );
        free(wide_session);
        memset(storage, 0, sizeof(*storage));
        storage->lock_handle = -1;
        return false;
    }
    free(wide_session);
    if (!remove_empty_parent(parent, error)) {
        memset(storage, 0, sizeof(*storage));
        storage->lock_handle = -1;
        return false;
    }
    memset(storage, 0, sizeof(*storage));
    storage->lock_handle = -1;
    return true;
}
