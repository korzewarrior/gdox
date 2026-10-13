#include "core/disc_uri.h"
#include <stdint.h>
#include <string.h>

static bool lower_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool gdox_disc_uri_validate(const char *disc_uri, gdox_error *error)
{
    static const char prefix[] = "nbd://127.0.0.1:";
    const char *cursor;
    uint32_t port = 0U;
    size_t digits = 0U;
    size_t token_bytes = 0U;

    gdox_error_clear(error);
    if (disc_uri == NULL
        || strncmp(disc_uri, prefix, sizeof(prefix) - 1U) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a private GDOX loopback disc URI is required"
        );
        return false;
    }
    cursor = disc_uri + sizeof(prefix) - 1U;
    if (*cursor < '1' || *cursor > '9') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "GDOX disc URI port is invalid"
        );
        return false;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        if (digits == 5U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "GDOX disc URI port is invalid"
            );
            return false;
        }
        port = port * 10U + (uint32_t)(*cursor - '0');
        ++cursor;
        ++digits;
    }
    if (*cursor != '/' || port == 0U || port > UINT16_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "GDOX disc URI port is invalid"
        );
        return false;
    }
    ++cursor;
    while (lower_hex(*cursor)) {
        ++cursor;
        ++token_bytes;
    }
    if (*cursor != '\0' || token_bytes != 32U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "GDOX disc URI session token is invalid"
        );
        return false;
    }
    return true;
}
