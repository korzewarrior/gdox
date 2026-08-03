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
