#define WIN32_LEAN_AND_MEAN

#include "core/ports/hash_backend.h"

#include <windows.h>
#include <bcrypt.h>

#include <limits.h>
#include <stdlib.h>

typedef struct gdox_bcrypt_hash {
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    uint8_t *object;
    DWORD object_bytes;
} gdox_bcrypt_hash;

struct gdox_digest_backend {
    gdox_bcrypt_hash md5;
    gdox_bcrypt_hash sha1;
    gdox_bcrypt_hash sha256;
};

static void hash_close(gdox_bcrypt_hash *hash)
{
    if (hash != NULL) {
        if (hash->hash != NULL) {
            (void)BCryptDestroyHash(hash->hash);
        }
        if (hash->algorithm != NULL) {
            (void)BCryptCloseAlgorithmProvider(hash->algorithm, 0U);
        }
        free(hash->object);
        *hash = (gdox_bcrypt_hash){0};
    }
}

static bool hash_open(
    gdox_bcrypt_hash *hash,
    LPCWSTR name,
    gdox_error *error
)
{
    DWORD returned = 0U;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hash->algorithm,
            name,
            NULL,
            0U
        ))
        || !BCRYPT_SUCCESS(BCryptGetProperty(
            hash->algorithm,
            BCRYPT_OBJECT_LENGTH,
            (PUCHAR)&hash->object_bytes,
            sizeof(hash->object_bytes),
            &returned,
            0U
        ))
        || returned != sizeof(hash->object_bytes)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not open a Windows digest provider");
        return false;
    }
    hash->object = malloc(hash->object_bytes);
    if (hash->object == NULL
        || !BCRYPT_SUCCESS(BCryptCreateHash(
            hash->algorithm,
            &hash->hash,
            hash->object,
            hash->object_bytes,
            NULL,
            0U,
            0U
        ))) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not create a Windows digest");
        return false;
    }
    return true;
}

void gdox_digest_backend_destroy(gdox_digest_backend *backend)
{
    if (backend != NULL) {
        hash_close(&backend->md5);
        hash_close(&backend->sha1);
        hash_close(&backend->sha256);
        free(backend);
    }
}

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
    if (backend == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate digest backend");
        return false;
    }
    if (!hash_open(&backend->md5, BCRYPT_MD5_ALGORITHM, error)
        || !hash_open(&backend->sha1, BCRYPT_SHA1_ALGORITHM, error)
        || !hash_open(&backend->sha256, BCRYPT_SHA256_ALGORITHM, error)) {
        gdox_digest_backend_destroy(backend);
        return false;
    }
    *output = backend;
    return true;
}

static bool hash_update(
    gdox_bcrypt_hash *hash,
    const uint8_t *bytes,
    size_t length
)
{
    size_t completed = 0U;
    while (completed < length) {
        const size_t remaining = length - completed;
        const ULONG chunk = remaining > ULONG_MAX
            ? ULONG_MAX
            : (ULONG)remaining;
        if (!BCRYPT_SUCCESS(BCryptHashData(
                hash->hash,
                (PUCHAR)(bytes + completed),
                chunk,
                0U
            ))) {
            return false;
        }
        completed += chunk;
    }
    return true;
}

bool gdox_digest_backend_update(
    gdox_digest_backend *backend,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
)
{
    if (backend == NULL
        || !hash_update(&backend->md5, bytes, length)
        || !hash_update(&backend->sha1, bytes, length)
        || !hash_update(&backend->sha256, bytes, length)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not update Windows archival digests");
        return false;
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
        || !BCRYPT_SUCCESS(BCryptFinishHash(backend->md5.hash, md5, GDOX_MD5_BYTES, 0U))
        || !BCRYPT_SUCCESS(BCryptFinishHash(backend->sha1.hash, sha1, GDOX_SHA1_BYTES, 0U))
        || !BCRYPT_SUCCESS(BCryptFinishHash(
            backend->sha256.hash,
            sha256,
            GDOX_SHA256_BYTES,
            0U
        ))) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not finalize Windows archival digests");
        return false;
    }
    return true;
}
