#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/nbd_token.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#else

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

#endif

static bool random_bytes(
    uint8_t output[GDOX_NBD_TOKEN_BYTES],
    gdox_error *error
)
{
#if defined(_WIN32)
    if (BCryptGenRandom(
            NULL,
            output,
            GDOX_NBD_TOKEN_BYTES,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        ) < 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not create private NBD token"
        );
        return false;
    }
#elif defined(__linux__)
    size_t completed = 0U;

    while (completed < GDOX_NBD_TOKEN_BYTES) {
        const ssize_t received = getrandom(
            output + completed,
            GDOX_NBD_TOKEN_BYTES - completed,
            0U
        );

        if (received > 0) {
            completed += (size_t)received;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not create private NBD token"
            );
            return false;
        }
    }
#else
    int random_file = open("/dev/urandom", O_RDONLY);
    size_t completed = 0U;

    if (random_file < 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not open the operating-system random source"
        );
        return false;
    }
    while (completed < GDOX_NBD_TOKEN_BYTES) {
        const ssize_t received = read(
            random_file,
            output + completed,
            GDOX_NBD_TOKEN_BYTES - completed
        );

        if (received > 0) {
            completed += (size_t)received;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            (void)close(random_file);
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not create private NBD token"
            );
            return false;
        }
    }
    (void)close(random_file);
#endif
    return true;
}

bool gdox_nbd_create_token(
    char output[GDOX_NBD_TOKEN_TEXT_BYTES],
    gdox_error *error
)
{
    static const char alphabet[] = "0123456789abcdef";
    uint8_t token[GDOX_NBD_TOKEN_BYTES];
    size_t index;

    if (!random_bytes(token, error)) {
        return false;
    }
    for (index = 0U; index < GDOX_NBD_TOKEN_BYTES; ++index) {
        output[index * 2U] = alphabet[token[index] >> 4U];
        output[index * 2U + 1U] = alphabet[token[index] & 0x0fU];
    }
    output[(size_t)GDOX_NBD_TOKEN_BYTES * 2U] = '\0';
    return true;
}
