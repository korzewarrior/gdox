#include "gdox/error.h"

#include <stdio.h>

void gdox_error_clear(gdox_error *error)
{
    if (error == NULL) {
        return;
    }
    error->code = GDOX_ERROR_NONE;
    error->message[0] = '\0';
}

void gdox_error_set(gdox_error *error, gdox_error_code code, const char *message)
{
    if (error == NULL) {
        return;
    }
    error->code = code;
    if (message == NULL) {
        error->message[0] = '\0';
        return;
    }
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

bool gdox_error_is_set(const gdox_error *error)
{
    return error != NULL && error->code != GDOX_ERROR_NONE;
}

const char *gdox_error_code_name(gdox_error_code code)
{
    switch (code) {
        case GDOX_ERROR_NONE:
            return "none";
        case GDOX_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";
        case GDOX_ERROR_OUT_OF_BOUNDS:
            return "out-of-bounds";
        case GDOX_ERROR_INVALID_SOURCE:
            return "invalid-source";
        case GDOX_ERROR_INVALID_VOLUME:
            return "invalid-volume";
        case GDOX_ERROR_NOT_FOUND:
            return "not-found";
        case GDOX_ERROR_PROTOCOL:
            return "protocol";
        case GDOX_ERROR_TRANSPORT:
            return "transport";
        case GDOX_ERROR_IO:
            return "io";
        case GDOX_ERROR_UNSUPPORTED:
            return "unsupported";
        case GDOX_ERROR_CANCELLED:
            return "cancelled";
        case GDOX_ERROR_INTERNAL:
            return "internal";
    }
    return "unknown";
}
