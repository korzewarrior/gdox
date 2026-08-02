#define WIN32_LEAN_AND_MEAN

#include "core/ports/random_access_file.h"
#include "platform/windows_support.h"

#include <windows.h>

#include <stdlib.h>

struct gdox_random_access_file {
    HANDLE handle;
};

static bool seek_file(
    gdox_random_access_file *file,
    uint64_t offset,
    gdox_error *error
)
{
    LARGE_INTEGER position;
    if (offset > (uint64_t)INT64_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xbox hard-disk offset is too large"
        );
        return false;
    }
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN)) {
        gdox_windows_io_error(
            error,
            "could not seek Xbox hard disk",
            GetLastError()
        );
        return false;
    }
    return true;
}

bool gdox_random_access_file_open_update(
    const char *path,
    gdox_random_access_file **output,
    uint64_t *length,
    gdox_error *error
)
{
    gdox_random_access_file *file;
    wchar_t *wide;
    LARGE_INTEGER size;
    HANDLE handle;

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
    wide = gdox_windows_wide_path(path, error);
    if (wide == NULL) {
        return false;
    }
    handle = CreateFileW(
        wide,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        NULL
    );
    free(wide);
    if (handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(handle, &size)) {
        const DWORD code = GetLastError();
        if (handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(handle);
        }
        gdox_windows_io_error(error, "could not open Xbox hard disk", code);
        return false;
    }
    file = malloc(sizeof(*file));
    if (file == NULL) {
        (void)CloseHandle(handle);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Xbox hard-disk file"
        );
        return false;
    }
    file->handle = handle;
    *length = (uint64_t)size.QuadPart;
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
        || offset > (uint64_t)INT64_MAX
        || (uint64_t)bytes > (uint64_t)INT64_MAX - offset) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access read is invalid"
        );
        return false;
    }
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const DWORD chunk =
            remaining > UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        DWORD received = 0U;
        if (!seek_file(file, offset + completed, error)
            || !ReadFile(
                file->handle,
                output + completed,
                chunk,
                &received,
                NULL
            ) || received != chunk) {
            if (!gdox_error_is_set(error)) {
                gdox_windows_io_error(
                    error,
                    "could not read Xbox hard disk",
                    GetLastError()
                );
            }
            return false;
        }
        completed += received;
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
        || offset > (uint64_t)INT64_MAX
        || (uint64_t)bytes > (uint64_t)INT64_MAX - offset) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access write is invalid"
        );
        return false;
    }
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const DWORD chunk =
            remaining > UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        DWORD written = 0U;
        if (!seek_file(file, offset + completed, error)
            || !WriteFile(
                file->handle,
                input + completed,
                chunk,
                &written,
                NULL
            ) || written != chunk) {
            if (!gdox_error_is_set(error)) {
                gdox_windows_io_error(
                    error,
                    "could not write Xbox hard disk",
                    GetLastError()
                );
            }
            return false;
        }
        completed += written;
    }
    return true;
}

bool gdox_random_access_file_sync_close(
    gdox_random_access_file *file,
    gdox_error *error
)
{
    BOOL flushed;
    BOOL closed;
    DWORD flush_error;
    DWORD close_error;

    if (file == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "random-access file is required"
        );
        return false;
    }
    flushed = FlushFileBuffers(file->handle);
    flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    closed = CloseHandle(file->handle);
    close_error = closed ? ERROR_SUCCESS : GetLastError();
    free(file);
    if (!flushed || !closed) {
        gdox_windows_io_error(
            error,
            "could not synchronize Xbox hard disk",
            !flushed ? flush_error : close_error
        );
        return false;
    }
    return true;
}

bool gdox_random_access_file_close(
    gdox_random_access_file *file,
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
        gdox_windows_io_error(
            error,
            "could not close Xbox hard disk",
            GetLastError()
        );
        return false;
    }
    return true;
}
