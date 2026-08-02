#include "core/ports/hash_backend.h"

#include <openssl/evp.h>

#include <stdlib.h>

struct gdox_digest_backend {
    EVP_MD_CTX *md5;
    EVP_MD_CTX *sha1;
    EVP_MD_CTX *sha256;
};

static void set_crypto_error(gdox_error *error, const char *message)
{
    gdox_error_set(error, GDOX_ERROR_INTERNAL, message);
}

void gdox_digest_backend_destroy(gdox_digest_backend *backend)
{
    if (backend != NULL) {
        EVP_MD_CTX_free(backend->md5);
        EVP_MD_CTX_free(backend->sha1);
        EVP_MD_CTX_free(backend->sha256);
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
        set_crypto_error(error, "could not allocate digest backend");
        return false;
    }
    backend->md5 = EVP_MD_CTX_new();
    backend->sha1 = EVP_MD_CTX_new();
    backend->sha256 = EVP_MD_CTX_new();
    if (backend->md5 == NULL || backend->sha1 == NULL || backend->sha256 == NULL
        || EVP_DigestInit_ex(backend->md5, EVP_md5(), NULL) != 1
        || EVP_DigestInit_ex(backend->sha1, EVP_sha1(), NULL) != 1
        || EVP_DigestInit_ex(backend->sha256, EVP_sha256(), NULL) != 1) {
        gdox_digest_backend_destroy(backend);
        set_crypto_error(error, "could not initialize archival digest algorithms");
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
    if (backend == NULL
        || EVP_DigestUpdate(backend->md5, bytes, length) != 1
        || EVP_DigestUpdate(backend->sha1, bytes, length) != 1
        || EVP_DigestUpdate(backend->sha256, bytes, length) != 1) {
        set_crypto_error(error, "could not update archival digests");
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
    unsigned int md5_bytes = 0U;
    unsigned int sha1_bytes = 0U;
    unsigned int sha256_bytes = 0U;

    if (backend == NULL
        || EVP_DigestFinal_ex(backend->md5, md5, &md5_bytes) != 1
        || EVP_DigestFinal_ex(backend->sha1, sha1, &sha1_bytes) != 1
        || EVP_DigestFinal_ex(backend->sha256, sha256, &sha256_bytes) != 1
        || md5_bytes != GDOX_MD5_BYTES
        || sha1_bytes != GDOX_SHA1_BYTES
        || sha256_bytes != GDOX_SHA256_BYTES) {
        set_crypto_error(error, "could not finalize archival digests");
        return false;
    }
    return true;
}
