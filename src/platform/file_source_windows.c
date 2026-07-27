#include "gdox/source.h"

#include "platform/windows_support.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct gdox_file_context {
    HANDLE handle;
    uint64_t sectors;
} gdox_file_context;

static uint64_t file_sector_count(const void *context)
{
    const gdox_file_context *file = context;
    return file->sectors;
}

static bool file_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_file_context *file = context;
    uint64_t offset = lba * GDOX_LOGICAL_SECTOR_BYTES;
    size_t completed = 0U;

    (void)blocks;
    while (completed < output_bytes) {
        const size_t remaining = output_bytes - completed;
        const DWORD request = remaining > (size_t)MAXDWORD ? MAXDWORD : (DWORD)remaining;
        OVERLAPPED operation = {0};
        DWORD received = 0U;
        BOOL started;

        operation.Offset = (DWORD)(offset & UINT64_C(0xffffffff));
        operation.OffsetHigh = (DWORD)(offset >> 32U);
        operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (operation.hEvent == NULL) {
            gdox_windows_io_error(error, "could not create image read event", GetLastError());
            return false;
        }
        started = ReadFile(
            file->handle,
            output + completed,
            request,
            &received,
            &operation
        );
        if (!started && GetLastError() == ERROR_IO_PENDING) {
            started = GetOverlappedResult(file->handle, &operation, &received, TRUE);
        }
        if (!started) {
            const DWORD code = GetLastError();
            (void)CloseHandle(operation.hEvent);
            gdox_windows_io_error(error, "could not read image", code);
            return false;
        }
        (void)CloseHandle(operation.hEvent);
        if (received == 0U) {
            gdox_error_set(error, GDOX_ERROR_IO, "file ended during a sector read");
            return false;
        }
        completed += (size_t)received;
        offset += received;
    }
    return true;
}

static bool file_media_present(const void *context)
{
    (void)context;
    return true;
}

static bool file_close(void *context, gdox_error *error)
{
    gdox_file_context *file = context;
    const BOOL closed = CloseHandle(file->handle);
    const DWORD code = closed ? ERROR_SUCCESS : GetLastError();
    free(file);
    if (!closed) {
        gdox_windows_io_error(error, "could not close image", code);
        return false;
    }
    return true;
}

static const gdox_sector_source_ops file_ops = {
    file_sector_count,
    file_read,
    file_media_present,
    file_close,
    NULL,
    NULL,
    NULL,
};

bool gdox_source_open_file(
    const char *path,
    gdox_sector_source *output,
    gdox_error *error
)
{
    wchar_t *wide_path;
    HANDLE handle;
    LARGE_INTEGER length;
    gdox_file_context *context;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "file path and output are required");
        return false;
    }
    wide_path = gdox_windows_wide_path(path, error);
    if (wide_path == NULL) {
        return false;
    }
    handle = CreateFileW(
        wide_path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    free(wide_path);
    if (handle == INVALID_HANDLE_VALUE) {
        gdox_windows_io_error(error, "could not open image", GetLastError());
        return false;
    }
    if (!GetFileSizeEx(handle, &length)) {
        const DWORD code = GetLastError();
        (void)CloseHandle(handle);
        gdox_windows_io_error(error, "could not inspect image size", code);
        return false;
    }
    if (length.QuadPart < 0
        || (uint64_t)length.QuadPart % GDOX_LOGICAL_SECTOR_BYTES != 0U) {
        (void)CloseHandle(handle);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "image size is not a multiple of 2048 bytes"
        );
        return false;
    }

    context = malloc(sizeof(*context));
    if (context == NULL) {
        (void)CloseHandle(handle);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate file source");
        return false;
    }
    context->handle = handle;
    context->sectors = (uint64_t)length.QuadPart / GDOX_LOGICAL_SECTOR_BYTES;
    output->context = context;
    output->ops = &file_ops;
    return true;
}
