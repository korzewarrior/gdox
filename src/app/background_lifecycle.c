#include "app/background_lifecycle.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static const char background_argument[] = "--background";

void gdox_background_lifecycle_initialize(
    gdox_background_lifecycle *lifecycle,
    bool start_hidden,
    bool background_available
)
{
    if (lifecycle == NULL) {
        return;
    }
    lifecycle->background_available = background_available;
    lifecycle->window_activation_pending = false;
    lifecycle->state = start_hidden && background_available
        ? GDOX_BACKGROUND_HIDDEN
        : GDOX_BACKGROUND_VISIBLE;
}

void gdox_background_lifecycle_apply(
    gdox_background_lifecycle *lifecycle,
    gdox_background_action action
)
{
    if (lifecycle == NULL || lifecycle->state == GDOX_BACKGROUND_STOPPING) {
        return;
    }
    switch (action) {
        case GDOX_BACKGROUND_NO_ACTION:
            break;
        case GDOX_BACKGROUND_WINDOW_CLOSED:
            lifecycle->state = lifecycle->background_available
                ? GDOX_BACKGROUND_HIDDEN
                : GDOX_BACKGROUND_STOPPING;
            break;
        case GDOX_BACKGROUND_OPEN_REQUESTED:
            lifecycle->state = GDOX_BACKGROUND_VISIBLE;
            lifecycle->window_activation_pending = true;
            break;
        case GDOX_BACKGROUND_QUIT_REQUESTED:
            lifecycle->state = GDOX_BACKGROUND_STOPPING;
            lifecycle->window_activation_pending = false;
            break;
        case GDOX_BACKGROUND_FACILITY_AVAILABLE:
            lifecycle->background_available = true;
            break;
        case GDOX_BACKGROUND_FACILITY_UNAVAILABLE:
            lifecycle->background_available = false;
            if (lifecycle->state == GDOX_BACKGROUND_HIDDEN) {
                lifecycle->state = GDOX_BACKGROUND_VISIBLE;
            }
            break;
    }
}

bool gdox_background_lifecycle_take_window_activation(
    gdox_background_lifecycle *lifecycle
)
{
    bool pending;

    if (lifecycle == NULL) {
        return false;
    }
    pending = lifecycle->window_activation_pending;
    lifecycle->window_activation_pending = false;
    return pending;
}

bool gdox_background_arguments_request_hidden(
    int argument_count,
    const char *const *arguments
)
{
    int index;

    if (argument_count <= 1 || arguments == NULL) {
        return false;
    }
    for (index = 1; index < argument_count; ++index) {
        if (arguments[index] != NULL
            && strcmp(arguments[index], background_argument) == 0) {
            return true;
        }
    }
    return false;
}

bool gdox_background_command_line_requests_hidden(const char *command_line)
{
    const char *cursor = command_line;
    const size_t argument_bytes = sizeof(background_argument) - 1U;

    if (cursor == NULL) {
        return false;
    }
    while (*cursor != '\0') {
        const char *start;
        const char *end;
        char quote = '\0';

        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (*cursor == '\"' || *cursor == '\'') {
            quote = *cursor;
            ++cursor;
        }
        start = cursor;
        if (quote != '\0') {
            while (*cursor != '\0' && *cursor != quote) {
                ++cursor;
            }
            end = cursor;
            if (*cursor == quote) {
                ++cursor;
            }
            if (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
                while (*cursor != '\0'
                    && !isspace((unsigned char)*cursor)) {
                    ++cursor;
                }
                continue;
            }
        } else {
            while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            end = cursor;
        }
        if ((size_t)(end - start) == argument_bytes
            && memcmp(start, background_argument, argument_bytes) == 0) {
            return true;
        }
    }
    return false;
}
