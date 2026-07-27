#include "platform/hash_backend.h"

#include <CommonCrypto/CommonDigest.h>

#include <stdlib.h>

struct gdox_digest_backend {
    CC_MD5_CTX md5;
    CC_SHA1_CTX sha1;
    CC_SHA256_CTX sha256;
};

bool gdox_digest_backend_create(
    gdox_digest_backend **output,
    gdox_error *error
)
{
    gdox_digest_backend *backend;

    if (output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "digest backend output is required");
        return false;
    }
    *output = NULL;
    backend = calloc(1U, sizeof(*backend));
    if (backend == NULL || CC_MD5_Init(&backend->md5) != 1
        || CC_SHA1_Init(&backend->sha1) != 1
        || CC_SHA256_Init(&backend->sha256) != 1) {
        free(backend);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize archival digest algorithms");
        return false;
    }
    *output = backend;
    return true;
}

bool gdox_digest_backend_update(
    gdox_digest_backend *backend,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
)
{
    size_t completed = 0U;

    if (backend == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "digest backend is required");
        return false;
    }
    while (completed < length) {
        const size_t remaining = length - completed;
        const CC_LONG chunk = remaining > UINT32_MAX
            ? UINT32_MAX
            : (CC_LONG)remaining;
        if (CC_MD5_Update(&backend->md5, bytes + completed, chunk) != 1
            || CC_SHA1_Update(&backend->sha1, bytes + completed, chunk) != 1
            || CC_SHA256_Update(&backend->sha256, bytes + completed, chunk) != 1) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not update archival digests");
            return false;
        }
        completed += chunk;
    }
    return true;
}

bool gdox_digest_backend_finish(
    gdox_digest_backend *backend,
    uint8_t md5[GDOX_MD5_BYTES],
    uint8_t sha1[GDOX_SHA1_BYTES],
    uint8_t sha256[GDOX_SHA256_BYTES],
    gdox_error *error
)
{
    if (backend == NULL
        || CC_MD5_Final(md5, &backend->md5) != 1
        || CC_SHA1_Final(sha1, &backend->sha1) != 1
        || CC_SHA256_Final(sha256, &backend->sha256) != 1) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not finalize archival digests");
        return false;
    }
    return true;
}

void gdox_digest_backend_destroy(gdox_digest_backend *backend)
{
    free(backend);
}
