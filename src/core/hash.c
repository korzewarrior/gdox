#include "gdox/hash.h"

#include "core/ports/hash_backend.h"
#include "core/ports/preservation_io.h"

#include <stdlib.h>

#define GDOX_HASH_FILE_BUFFER_BYTES ((size_t)1024U * 1024U)

struct gdox_hash_stream {
    gdox_digest_backend *backend;
    uint32_t crc32;
    bool finished;
};

static uint32_t crc32_update(
    uint32_t crc32,
    const uint8_t *bytes,
    size_t length
)
{
    static const uint32_t table[16] = {
        UINT32_C(0x00000000), UINT32_C(0x1db71064),
        UINT32_C(0x3b6e20c8), UINT32_C(0x26d930ac),
        UINT32_C(0x76dc4190), UINT32_C(0x6b6b51f4),
        UINT32_C(0x4db26158), UINT32_C(0x5005713c),
        UINT32_C(0xedb88320), UINT32_C(0xf00f9344),
        UINT32_C(0xd6d6a3e8), UINT32_C(0xcb61b38c),
        UINT32_C(0x9b64c2b0), UINT32_C(0x86d3d2d4),
        UINT32_C(0xa00ae278), UINT32_C(0xbdbdf21c),
    };
    size_t index;

    for (index = 0U; index < length; ++index) {
        crc32 ^= bytes[index];
        crc32 = table[crc32 & UINT32_C(0x0f)] ^ (crc32 >> 4U);
        crc32 = table[crc32 & UINT32_C(0x0f)] ^ (crc32 >> 4U);
    }
    return crc32;
}

bool gdox_hash_stream_create(gdox_hash_stream **output, gdox_error *error)
{
    gdox_hash_stream *stream;

    gdox_error_clear(error);
    if (output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "hash stream output is required");
        return false;
    }
    *output = NULL;
    stream = calloc(1U, sizeof(*stream));
    if (stream == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate hash stream");
        return false;
    }
    stream->crc32 = UINT32_MAX;
    if (!gdox_digest_backend_create(&stream->backend, error)) {
        free(stream);
        return false;
    }
    *output = stream;
    return true;
}

bool gdox_hash_stream_update(
    gdox_hash_stream *stream,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (stream == NULL || stream->finished || (length != 0U && bytes == NULL)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "active hash stream and input are required");
        return false;
    }
    stream->crc32 = crc32_update(stream->crc32, bytes, length);
    return gdox_digest_backend_update(stream->backend, bytes, length, error);
}

bool gdox_hash_stream_finish(
    gdox_hash_stream *stream,
    gdox_hashes *output,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (stream == NULL || output == NULL || stream->finished) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "unfinished hash stream and output are required");
        return false;
    }
    if (!gdox_digest_backend_finish(
            stream->backend,
            output->md5,
            output->sha1,
            output->sha256,
            error
        )) {
        return false;
    }
    output->crc32 = ~stream->crc32;
    stream->finished = true;
    return true;
}

void gdox_hash_stream_destroy(gdox_hash_stream *stream)
{
    if (stream != NULL) {
        gdox_digest_backend_destroy(stream->backend);
        free(stream);
    }
}

bool gdox_hash_buffer(
    const uint8_t *bytes,
    size_t length,
    gdox_hashes *output,
    gdox_error *error
)
{
    gdox_hash_stream *stream = NULL;
    bool success;

    if (!gdox_hash_stream_create(&stream, error)) {
        return false;
    }
    success = gdox_hash_stream_update(stream, bytes, length, error)
        && gdox_hash_stream_finish(stream, output, error);
    gdox_hash_stream_destroy(stream);
    return success;
}

bool gdox_hash_file(
    const char *path,
    gdox_hashes *output,
    uint64_t *length,
    gdox_error *error
)
{
    gdox_preservation_file *file = NULL;
    gdox_hash_stream *stream = NULL;
    uint8_t *buffer = NULL;
    uint64_t file_length = 0U;
    uint64_t completed = 0U;
    bool success = false;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "file path and hash output are required"
        );
        return false;
    }
    if (!gdox_preservation_file_open_read(
            path,
            &file,
            &file_length,
            error
        )) {
        return false;
    }
    buffer = malloc(GDOX_HASH_FILE_BUFFER_BYTES);
    if (buffer == NULL || !gdox_hash_stream_create(&stream, error)) {
        if (buffer == NULL) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "could not allocate file hashing buffer"
            );
        }
        goto cleanup;
    }
    while (completed < file_length) {
        const uint64_t remaining = file_length - completed;
        const size_t request = remaining < GDOX_HASH_FILE_BUFFER_BYTES
            ? (size_t)remaining
            : GDOX_HASH_FILE_BUFFER_BYTES;
        size_t received = 0U;

        if (!gdox_preservation_file_read(
                file,
                buffer,
                request,
                &received,
                error
            ) || received == 0U
            || !gdox_hash_stream_update(stream, buffer, received, error)) {
            if (!gdox_error_is_set(error)) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_IO,
                    "file ended while hashing"
                );
            }
            goto cleanup;
        }
        completed += received;
    }
    if (!gdox_hash_stream_finish(stream, output, error)) {
        goto cleanup;
    }
    if (length != NULL) {
        *length = file_length;
    }
    success = true;

cleanup:
    free(buffer);
    gdox_hash_stream_destroy(stream);
    if (file != NULL) {
        gdox_error close_error;
        if (!gdox_preservation_file_close(file, &close_error) && success) {
            *error = close_error;
            success = false;
        }
    }
    return success;
}

uint32_t gdox_crc32_buffer(const uint8_t *bytes, size_t length)
{
    if (length != 0U && bytes == NULL) {
        return 0U;
    }
    return ~crc32_update(UINT32_MAX, bytes, length);
}

void gdox_hash_hex(
    const uint8_t *bytes,
    size_t length,
    bool uppercase,
    char *output
)
{
    const char *digits = uppercase
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
    size_t index;

    if (output == NULL) {
        return;
    }
    for (index = 0U; index < length; ++index) {
        output[index * 2U] = digits[bytes[index] >> 4U];
        output[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    output[length * 2U] = '\0';
}
