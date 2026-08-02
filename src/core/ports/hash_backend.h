#ifndef GDOX_CORE_PORTS_HASH_BACKEND_H
#define GDOX_CORE_PORTS_HASH_BACKEND_H

#include "gdox/error.h"
#include "gdox/hash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_digest_backend gdox_digest_backend;

bool gdox_digest_backend_create(
    gdox_digest_backend **output,
    gdox_error *error
);
bool gdox_digest_backend_update(
    gdox_digest_backend *backend,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
);
bool gdox_digest_backend_finish(
    gdox_digest_backend *backend,
    uint8_t md5[GDOX_MD5_BYTES],
    uint8_t sha1[GDOX_SHA1_BYTES],
    uint8_t sha256[GDOX_SHA256_BYTES],
    gdox_error *error
);
void gdox_digest_backend_destroy(gdox_digest_backend *backend);

#endif
