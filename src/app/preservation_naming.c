#include "app/preservation_naming.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool forbidden_ascii(unsigned char value)
{
    return value < 0x20U
        || value == 0x7fU
        || value == '<'
        || value == '>'
        || value == ':'
        || value == '"'
        || value == '/'
        || value == '\\'
        || value == '|'
        || value == '?'
        || value == '*';
}

static size_t copy_safe_title(
    const char *title,
    char *output,
    size_t output_capacity
)
{
    size_t written = 0U;
    bool separator_pending = false;
    const unsigned char *cursor;

    if (title == NULL || title[0] == '\0') {
        output[0] = '\0';
        return 0U;
    }
    cursor = (const unsigned char *)title;

    while (*cursor != '\0') {
        const bool separator = *cursor < 0x80U
            && (isspace(*cursor) != 0 || forbidden_ascii(*cursor));
        if (separator) {
            separator_pending = written != 0U;
            ++cursor;
            continue;
        }
        if (separator_pending) {
            if (written + 1U >= output_capacity) {
                break;
            }
            output[written++] = ' ';
            separator_pending = false;
        }
        if (written + 1U >= output_capacity) {
            break;
        }
        output[written++] = (char)*cursor++;
    }
    while (written != 0U
        && (output[written - 1U] == ' ' || output[written - 1U] == '.')) {
        --written;
    }
    output[written] = '\0';
    return written;
}

bool gdox_preservation_suggest_filename(
    const char *title,
    gdox_preservation_format format,
    char *output,
    size_t output_capacity
)
{
    const char *suffix;
    size_t title_length;
    size_t suffix_length;

    if (output == NULL || output_capacity == 0U) {
        return false;
    }
    output[0] = '\0';
    if (format == GDOX_PRESERVATION_XISO_COMPACT) {
        suffix = "-xiso.iso";
    } else if (format == GDOX_PRESERVATION_REDUMP) {
        suffix = "-full-disc.iso";
    } else {
        return false;
    }

    suffix_length = strlen(suffix);
    if (suffix_length + 1U > output_capacity) {
        return false;
    }
    title_length = copy_safe_title(
        title,
        output,
        output_capacity - suffix_length
    );
    if (title_length == 0U) {
        static const char fallback[] = "Xbox game";
        if (sizeof(fallback) + suffix_length > output_capacity) {
            return false;
        }
        (void)memcpy(output, fallback, sizeof(fallback));
        title_length = sizeof(fallback) - 1U;
    }
    (void)snprintf(
        output + title_length,
        output_capacity - title_length,
        "%s",
        suffix
    );
    return true;
}
