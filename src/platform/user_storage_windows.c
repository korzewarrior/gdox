#define WIN32_LEAN_AND_MEAN

#include "platform/user_storage.h"
#include "platform/windows_support.h"

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_PATH_CAPACITY 32768U

typedef enum storage_root {
    STORAGE_CONFIG = 0,
    STORAGE_DATA,
} storage_root;

static bool safe_relative(const char *relative)
{
    const char *cursor = relative;
    if (relative == NULL || relative[0] == '\0'
        || relative[0] == '/' || relative[0] == '\\') {
        return false;
    }
    while (*cursor != '\0') {
        const char *slash = strpbrk(cursor, "/\\");
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

static bool append_component(
    wchar_t path[GDOX_WINDOWS_PATH_CAPACITY],
    const wchar_t *component,
    gdox_error *error
)
{
    const size_t path_bytes = wcslen(path);
    const size_t component_bytes = wcslen(component);
    if (path_bytes + component_bytes + 2U > GDOX_WINDOWS_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "user storage path is too long");
        return false;
    }
    if (path_bytes != 0U
        && path[path_bytes - 1U] != L'\\'
        && path[path_bytes - 1U] != L'/') {
        path[path_bytes] = L'\\';
        path[path_bytes + 1U] = L'\0';
    }
    memcpy(
        path + wcslen(path),
        component,
        (component_bytes + 1U) * sizeof(*component)
    );
    return true;
}

static bool wide_to_utf8(
    const wchar_t *wide,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide,
            -1,
            output,
            (int)GDOX_STORAGE_PATH_CAPACITY,
            NULL,
            NULL
        ) == 0) {
        gdox_windows_io_error(error, "could not encode user storage path", GetLastError());
        return false;
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
    wchar_t base[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t *wide_relative;
    const wchar_t *override = kind == STORAGE_CONFIG
        ? L"GDOX_CONFIG_HOME"
        : L"GDOX_DATA_HOME";
    DWORD length;
    bool success;

    if (!safe_relative(relative)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "safe relative storage path is required");
        return false;
    }
    length = GetEnvironmentVariableW(
        override,
        base,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length == 0U || length >= GDOX_WINDOWS_PATH_CAPACITY) {
        length = GetEnvironmentVariableW(
            L"APPDATA",
            base,
            GDOX_WINDOWS_PATH_CAPACITY
        );
        if (length == 0U || length >= GDOX_WINDOWS_PATH_CAPACITY
            || !append_component(base, L"gdox", error)
            || !append_component(base, L"gdox", error)
            || !append_component(
                base,
                kind == STORAGE_CONFIG ? L"config" : L"data",
                error
            )) {
            if (!gdox_error_is_set(error)) {
                gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "user storage directory is unavailable");
            }
            return false;
        }
    }
    wide_relative = gdox_windows_wide_path(relative, error);
    if (wide_relative == NULL) {
        return false;
    }
    for (wchar_t *cursor = wide_relative; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'/') {
            *cursor = L'\\';
        }
    }
    success = append_component(base, wide_relative, error)
        && wide_to_utf8(base, output, error);
    free(wide_relative);
    return success;
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
    wchar_t *path,
    gdox_error *error
)
{
    wchar_t *cursor;
    wchar_t *slash = wcsrchr(path, L'\\');

    if (slash == NULL) {
        slash = wcsrchr(path, L'/');
    }
    if (slash == NULL) {
        return true;
    }
    *slash = L'\0';
    cursor = path;
    if (path[0] != L'\0' && path[1] == L':') {
        cursor = path + 3U;
    } else if (path[0] == L'\\' && path[1] == L'\\') {
        cursor = path + 2U;
        cursor = wcschr(cursor, L'\\');
        if (cursor != NULL) {
            cursor = wcschr(cursor + 1U, L'\\');
        }
        if (cursor == NULL) {
            *slash = L'\\';
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "user storage UNC path has no directory");
            return false;
        }
        ++cursor;
    }
    while (cursor != NULL && *cursor != L'\0') {
        wchar_t *next = wcschr(cursor, L'\\');
        DWORD attributes;
        if (next != NULL) {
            *next = L'\0';
        }
        if (!CreateDirectoryW(path, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            const DWORD code = GetLastError();
            if (next != NULL) {
                *next = L'\\';
            }
            *slash = L'\\';
            gdox_windows_io_error(error, "could not create private directory", code);
            return false;
        }
        attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES
            || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            if (next != NULL) {
                *next = L'\\';
            }
            *slash = L'\\';
            gdox_error_set(error, GDOX_ERROR_IO, "private storage parent is not a directory");
            return false;
        }
        if (next == NULL) {
            break;
        }
        *next = L'\\';
        cursor = next + 1U;
    }
    *slash = L'\\';
    return true;
}

bool gdox_storage_file_size(const char *path, uint64_t *bytes)
{
    gdox_error ignored;
    wchar_t *wide;
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (path == NULL || path[0] == '\0' || bytes == NULL) {
        return false;
    }
    wide = gdox_windows_wide_path(path, &ignored);
    if (wide == NULL
        || !GetFileAttributesExW(wide, GetFileExInfoStandard, &attributes)
        || (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        free(wide);
        return false;
    }
    free(wide);
    *bytes = (uint64_t)attributes.nFileSizeHigh << 32U
        | attributes.nFileSizeLow;
    return true;
}

bool gdox_storage_ensure_directory(
    const char *path,
    gdox_error *error
)
{
    wchar_t *wide;
    wchar_t child[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t *separator;
    DWORD attributes;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private directory path is required"
        );
        return false;
    }
    wide = gdox_windows_wide_path(path, error);
    if (wide == NULL) {
        return false;
    }
    if (wcslen(wide) >= GDOX_WINDOWS_PATH_CAPACITY) {
        free(wide);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private directory path is too long"
        );
        return false;
    }
    memcpy(child, wide, (wcslen(wide) + 1U) * sizeof(*wide));
    free(wide);
    if (!append_component(child, L".gdox", error)
        || !create_parent_directories(child, error)) {
        return false;
    }
    separator = wcsrchr(child, L'\\');
    if (separator == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not inspect private directory"
        );
        return false;
    }
    *separator = L'\0';
    attributes = GetFileAttributesW(child);
    if (attributes == INVALID_FILE_ATTRIBUTES
        || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
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
    wchar_t *wide;
    HANDLE file;
    LARGE_INTEGER length;
    uint8_t *buffer;
    size_t completed = 0U;

    gdox_error_clear(error);
    if (path == NULL || data == NULL || bytes == NULL || found == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "storage read outputs are required");
        return false;
    }
    *data = NULL;
    *bytes = 0U;
    *found = false;
    wide = gdox_windows_wide_path(path, error);
    if (wide == NULL) {
        return false;
    }
    file = CreateFileW(
        wide,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    free(wide);
    if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (file == INVALID_HANDLE_VALUE) {
        gdox_windows_io_error(error, "could not open private file", GetLastError());
        return false;
    }
    if (!GetFileSizeEx(file, &length)) {
        const DWORD code = GetLastError();
        (void)CloseHandle(file);
        gdox_windows_io_error(error, "could not inspect private file", code);
        return false;
    }
    if (length.QuadPart < 0
        || (uint64_t)length.QuadPart > maximum_bytes) {
        (void)CloseHandle(file);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "private file is too large");
        return false;
    }
    buffer = malloc((size_t)length.QuadPart + 1U);
    if (buffer == NULL) {
        (void)CloseHandle(file);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate private file");
        return false;
    }
    while (completed < (size_t)length.QuadPart) {
        const size_t remaining = (size_t)length.QuadPart - completed;
        const DWORD request = remaining > UINT32_MAX
            ? UINT32_MAX
            : (DWORD)remaining;
        DWORD received = 0U;
        if (!ReadFile(file, buffer + completed, request, &received, NULL)
            || received == 0U) {
            const DWORD code = GetLastError();
            free(buffer);
            (void)CloseHandle(file);
            gdox_windows_io_error(error, "could not read private file", code);
            return false;
        }
        completed += received;
    }
    if (!CloseHandle(file)) {
        const DWORD code = GetLastError();
        free(buffer);
        gdox_windows_io_error(error, "could not close private file", code);
        return false;
    }
    buffer[completed] = 0U;
    *data = buffer;
    *bytes = completed;
    *found = true;
    return true;
}

static bool write_all(
    HANDLE file,
    const uint8_t *data,
    size_t bytes,
    gdox_error *error
)
{
    size_t completed = 0U;
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const DWORD request = remaining > UINT32_MAX
            ? UINT32_MAX
            : (DWORD)remaining;
        DWORD written = 0U;
        if (!WriteFile(file, data + completed, request, &written, NULL)
            || written != request) {
            gdox_windows_io_error(error, "could not write private file", GetLastError());
            return false;
        }
        completed += written;
    }
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
    wchar_t *wide;
    wchar_t temporary[GDOX_WINDOWS_PATH_CAPACITY];
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD attempt;
    bool success;

    gdox_error_clear(error);
    if (path == NULL || (bytes != 0U && data == NULL)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "private file path and data are required");
        return false;
    }
    wide = gdox_windows_wide_path(path, error);
    if (wide == NULL || !create_parent_directories(wide, error)) {
        free(wide);
        return false;
    }
    if (!replace && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES) {
        free(wide);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "private file already exists");
        return false;
    }
    for (attempt = 0U; attempt < 32U; ++attempt) {
        const int written = swprintf(
            temporary,
            GDOX_WINDOWS_PATH_CAPACITY,
            L"%ls.%lu.%llu.tmp",
            wide,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long long)(GetTickCount64() + attempt)
        );
        if (written < 0 || (size_t)written >= GDOX_WINDOWS_PATH_CAPACITY) {
            free(wide);
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "private file path is too long");
            return false;
        }
        file = CreateFileW(
            temporary,
            GENERIC_WRITE,
            0U,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            NULL
        );
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) {
            break;
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        free(wide);
        gdox_windows_io_error(error, "could not create private file update", code);
        return false;
    }
    success = write_all(file, data, bytes, error);
    if (success && !FlushFileBuffers(file)) {
        gdox_windows_io_error(error, "could not synchronize private file", GetLastError());
        success = false;
    }
    if (!CloseHandle(file) && success) {
        gdox_windows_io_error(error, "could not close private file", GetLastError());
        success = false;
    }
    if (success && !MoveFileExW(
            temporary,
            wide,
            MOVEFILE_WRITE_THROUGH
                | (replace ? MOVEFILE_REPLACE_EXISTING : 0U)
        )) {
        gdox_windows_io_error(error, "could not commit private file", GetLastError());
        success = false;
    }
    if (!success) {
        (void)DeleteFileW(temporary);
    }
    free(wide);
    return success;
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
