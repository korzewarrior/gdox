#define WIN32_LEAN_AND_MEAN

#include "platform/preservation_io.h"
#include "platform/windows_support.h"

#include <windows.h>

#include <stdlib.h>

struct gdox_preservation_file {
    HANDLE handle;
};

static bool allocate_file(
    HANDLE handle,
    gdox_preservation_file **output,
    gdox_error *error
)
{
    gdox_preservation_file *file = malloc(sizeof(*file));
    if (file == NULL) {
        (void)CloseHandle(handle);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate preservation file");
        return false;
    }
    file->handle = handle;
    *output = file;
    return true;
}

bool gdox_preservation_file_create(
    const char *path,
    gdox_preservation_file **output,
    gdox_error *error
)
{
    wchar_t *wide;
    HANDLE handle;

    gdox_error_clear(error);
    if (output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "preservation file output is required");
        return false;
    }
    *output = NULL;
    wide = gdox_windows_wide_path(path, error);
    if (wide == NULL) {
        return false;
    }
    handle = CreateFileW(
        wide,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        gdox_windows_io_error(error, "could not create preservation output", GetLastError());
        return false;
    }
    return allocate_file(handle, output, error);
}

bool gdox_preservation_file_open_read(
    const char *path,
    gdox_preservation_file **output,
    uint64_t *length,
    gdox_error *error
)
{
    wchar_t *wide;
    HANDLE handle;
    LARGE_INTEGER size;

    gdox_error_clear(error);
    if (output == NULL || length == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "preservation file and length are required");
        return false;
    }
    *output = NULL;
    wide = gdox_windows_wide_path(path, error);
    if (wide == NULL) {
        return false;
    }
    handle = CreateFileW(
        wide,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    free(wide);
    if (handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(handle, &size)) {
        const DWORD code = GetLastError();
        if (handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(handle);
        }
        gdox_windows_io_error(error, "could not open preservation output", code);
        return false;
    }
    *length = (uint64_t)size.QuadPart;
    return allocate_file(handle, output, error);
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
        const size_t remaining = length - completed;
        const DWORD chunk = remaining > UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        DWORD written = 0U;
        if (!WriteFile(file->handle, bytes + completed, chunk, &written, NULL)
            || written != chunk) {
            gdox_windows_io_error(error, "could not write preservation output", GetLastError());
            return false;
        }
        completed += written;
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
    const DWORD chunk = capacity > UINT32_MAX ? UINT32_MAX : (DWORD)capacity;
    DWORD completed = 0U;
    if (!ReadFile(file->handle, bytes, chunk, &completed, NULL)) {
        gdox_windows_io_error(error, "could not read preservation output", GetLastError());
        return false;
    }
    *read_bytes = completed;
    return true;
}

bool gdox_preservation_file_sync_close(
    gdox_preservation_file *file,
    gdox_error *error
)
{
    const BOOL flushed = FlushFileBuffers(file->handle);
    const DWORD flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    const BOOL closed = CloseHandle(file->handle);
    const DWORD close_error = closed ? ERROR_SUCCESS : GetLastError();
    free(file);
    if (!flushed || !closed) {
        gdox_windows_io_error(
            error,
            "could not synchronize preservation output",
            !flushed ? flush_error : close_error
        );
        return false;
    }
    return true;
}

bool gdox_preservation_file_close(
    gdox_preservation_file *file,
    gdox_error *error
)
{
    BOOL closed;
    if (file == NULL) {
        return true;
    }
    closed = CloseHandle(file->handle);
    free(file);
    if (!closed) {
        gdox_windows_io_error(error, "could not close preservation file", GetLastError());
        return false;
    }
    return true;
}

bool gdox_preservation_path_exists(const char *path)
{
    gdox_error error;
    wchar_t *wide = gdox_windows_wide_path(path, &error);
    DWORD attributes;
    if (wide == NULL) {
        return false;
    }
    attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

bool gdox_preservation_path_remove(const char *path)
{
    gdox_error error;
    wchar_t *wide = gdox_windows_wide_path(path, &error);
    BOOL removed;
    DWORD code;
    if (wide == NULL) {
        return false;
    }
    removed = DeleteFileW(wide);
    code = removed ? ERROR_SUCCESS : GetLastError();
    free(wide);
    return removed || code == ERROR_FILE_NOT_FOUND;
}

bool gdox_preservation_path_commit(
    const char *temporary_path,
    const char *final_path,
    gdox_error *error
)
{
    wchar_t *temporary = gdox_windows_wide_path(temporary_path, error);
    wchar_t *final;
    BOOL moved;
    DWORD code;
    if (temporary == NULL) {
        return false;
    }
    final = gdox_windows_wide_path(final_path, error);
    if (final == NULL) {
        free(temporary);
        return false;
    }
    moved = MoveFileExW(temporary, final, MOVEFILE_WRITE_THROUGH);
    code = moved ? ERROR_SUCCESS : GetLastError();
    free(temporary);
    free(final);
    if (!moved) {
        gdox_windows_io_error(error, "could not commit preservation output", code);
        return false;
    }
    return true;
}

bool gdox_preservation_available_space(
    const char *path,
    uint64_t *bytes,
    gdox_error *error
)
{
    wchar_t *wide = gdox_windows_wide_path(path, error);
    wchar_t *absolute;
    DWORD required;
    wchar_t *slash;
    size_t allocation_bytes;
    ULARGE_INTEGER available;
    if (wide == NULL) {
        return false;
    }
    required = GetFullPathNameW(wide, 0U, NULL, NULL);
    allocation_bytes = (size_t)required * sizeof(*absolute);
    if (required == 0U
        || allocation_bytes / sizeof(*absolute) != (size_t)required) {
        const DWORD code = GetLastError();
        free(wide);
        gdox_windows_io_error(error, "could not resolve preservation output path", code);
        return false;
    }
    absolute = malloc(allocation_bytes);
    if (absolute == NULL) {
        free(wide);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate preservation output path");
        return false;
    }
    if (GetFullPathNameW(wide, required, absolute, NULL) == 0U) {
        const DWORD code = GetLastError();
        free(absolute);
        free(wide);
        gdox_windows_io_error(error, "could not resolve preservation output path", code);
        return false;
    }
    free(wide);
    slash = wcsrchr(absolute, L'\\');
    if (slash == NULL) {
        slash = wcsrchr(absolute, L'/');
    }
    if (slash == NULL) {
        free(absolute);
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "preservation output has no parent directory");
        return false;
    }
    slash[1] = L'\0';
    if (!GetDiskFreeSpaceExW(absolute, &available, NULL, NULL)) {
        const DWORD code = GetLastError();
        free(absolute);
        gdox_windows_io_error(error, "could not inspect output free space", code);
        return false;
    }
    free(absolute);
    *bytes = available.QuadPart;
    return true;
}
