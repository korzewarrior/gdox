#include "platform/windows_command.h"

#include "platform/windows_support.h"

#include <stdint.h>
#include <stdlib.h>

static bool reserve(
    gdox_windows_command *command,
    size_t additional,
    gdox_error *error
)
{
    size_t needed;
    size_t capacity;
    wchar_t *resized;

    if (command == NULL || additional > SIZE_MAX - command->length - 1U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "Windows command line is too long"
        );
        return false;
    }
    needed = command->length + additional + 1U;
    if (needed <= command->capacity) {
        return true;
    }
    capacity = command->capacity == 0U ? 1024U : command->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    resized = realloc(command->text, capacity * sizeof(*resized));
    if (resized == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Windows command line"
        );
        return false;
    }
    command->text = resized;
    command->capacity = capacity;
    return true;
}

static bool append_character(
    gdox_windows_command *command,
    wchar_t character,
    gdox_error *error
)
{
    if (!reserve(command, 1U, error)) {
        return false;
    }
    command->text[command->length++] = character;
    command->text[command->length] = L'\0';
    return true;
}

static bool append_repeat(
    gdox_windows_command *command,
    wchar_t character,
    size_t count,
    gdox_error *error
)
{
    size_t index;

    if (!reserve(command, count, error)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        command->text[command->length++] = character;
    }
    command->text[command->length] = L'\0';
    return true;
}

static bool append_backslashes(
    gdox_windows_command *command,
    size_t count,
    bool before_quote,
    bool closes_argument,
    gdox_error *error
)
{
    size_t output_count = count;

    if (before_quote || closes_argument) {
        const size_t extra = before_quote ? 1U : 0U;
        if (count > (SIZE_MAX - extra) / 2U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "Windows command line is too long"
            );
            return false;
        }
        output_count = count * 2U + extra;
    }
    return append_repeat(command, L'\\', output_count, error);
}

bool gdox_windows_command_add_wide(
    gdox_windows_command *command,
    const wchar_t *argument,
    gdox_error *error
)
{
    const wchar_t *cursor = argument;
    size_t backslashes = 0U;

    if (command == NULL || argument == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Windows command and argument are required"
        );
        return false;
    }
    if (command->length != 0U
        && !append_character(command, L' ', error)) {
        return false;
    }
    if (!append_character(command, L'"', error)) {
        return false;
    }
    while (*cursor != L'\0') {
        if (*cursor == L'\\') {
            ++backslashes;
            ++cursor;
            continue;
        }
        if (!append_backslashes(
                command,
                backslashes,
                *cursor == L'"',
                false,
                error
            )) {
            return false;
        }
        backslashes = 0U;
        if (!append_character(command, *cursor++, error)) {
            return false;
        }
    }
    return append_backslashes(
        command, backslashes, false, true, error
    ) && append_character(command, L'"', error);
}

bool gdox_windows_command_add_utf8(
    gdox_windows_command *command,
    const char *argument,
    gdox_error *error
)
{
    wchar_t *wide = gdox_windows_wide_text(argument, error);
    bool success;

    if (wide == NULL) {
        return false;
    }
    success = gdox_windows_command_add_wide(command, wide, error);
    free(wide);
    return success;
}

void gdox_windows_command_destroy(gdox_windows_command *command)
{
    if (command != NULL) {
        free(command->text);
        command->text = NULL;
        command->length = 0U;
        command->capacity = 0U;
    }
}
