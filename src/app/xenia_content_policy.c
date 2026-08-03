#include "app/xenia_content_policy.h"

#include <stdio.h>
#include <string.h>

bool gdox_xenia_content_hexadecimal_name(
    const char *name,
    size_t characters
)
{
    size_t index;

    if (name == NULL || strlen(name) != characters) {
        return false;
    }
    for (index = 0U; index < characters; ++index) {
        const char character = name[index];

        if (!((character >= '0' && character <= '9')
              || (character >= 'a' && character <= 'f')
              || (character >= 'A' && character <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool gdox_xenia_content_persistent_type(const char *name)
{
    return strcmp(name, "00000001") == 0
        || strcmp(name, "00010000") == 0
        || strcmp(name, "00060000") == 0;
}

bool gdox_xenia_content_relative_path(
    char *output,
    size_t capacity,
    const char *parent,
    const char *child,
    gdox_error *error
)
{
    int written;

    if (output == NULL || capacity == 0U || child == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia content path output is required"
        );
        return false;
    }
    written = parent == NULL || parent[0] == '\0'
        ? snprintf(output, capacity, "%s", child)
        : snprintf(output, capacity, "%s/%s", parent, child);
    if (written < 0 || (size_t)written >= capacity) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia content path is too long"
        );
        return false;
    }
    return true;
}

void gdox_xenia_content_layout_error(
    const char *content_root,
    const char *relative,
    gdox_error *error
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    int written;

    written = snprintf(
        message,
        sizeof(message),
        "unsupported Xenia persistent content entry: %s/%s",
        content_root,
        relative
    );
    if (written < 0 || (size_t)written >= sizeof(message)) {
        (void)snprintf(
            message,
            sizeof(message),
            "unsupported Xenia persistent content entry relative to its "
            "content root: %s",
            relative
        );
    }
    gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, message);
}
