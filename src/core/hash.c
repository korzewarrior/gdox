#include "gdox/hash.h"

#include "platform/hash_backend.h"

#include <stdlib.h>

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
    size_t index;
    for (index = 0U; index < length; ++index) {
        unsigned int bit;
        crc32 ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc32 & UINT32_C(1)));
            crc32 = (crc32 >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
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
