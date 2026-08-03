#ifndef GDOX_HASH_H
#define GDOX_HASH_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_MD5_BYTES 16U
#define GDOX_SHA1_BYTES 20U
#define GDOX_SHA256_BYTES 32U

typedef struct gdox_hashes {
    uint32_t crc32;
    uint8_t md5[GDOX_MD5_BYTES];
    uint8_t sha1[GDOX_SHA1_BYTES];
    uint8_t sha256[GDOX_SHA256_BYTES];
} gdox_hashes;

typedef struct gdox_hash_stream gdox_hash_stream;

bool gdox_hash_stream_create(gdox_hash_stream **output, gdox_error *error);
bool gdox_hash_stream_update(
    gdox_hash_stream *stream,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
);
bool gdox_hash_stream_finish(
    gdox_hash_stream *stream,
    gdox_hashes *output,
    gdox_error *error
);
void gdox_hash_stream_destroy(gdox_hash_stream *stream);

bool gdox_hash_buffer(
    const uint8_t *bytes,
    size_t length,
    gdox_hashes *output,
    gdox_error *error
);

/* Streams a regular file through the native digest backend without loading
 * the complete file into memory. */
bool gdox_hash_file(
    const char *path,
    gdox_hashes *output,
    uint64_t *length,
    gdox_error *error
);

uint32_t gdox_crc32_buffer(const uint8_t *bytes, size_t length);

void gdox_hash_hex(
    const uint8_t *bytes,
    size_t length,
    bool uppercase,
    char *output
);

#ifdef __cplusplus
}
#endif

#endif
