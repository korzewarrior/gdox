#include "app/preferences.h"

#include "platform/user_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_PREFERENCES_SCHEMA 1U

typedef struct decoded_fields {
    bool schema;
    bool auto_start;
    bool scale;
    bool aspect;
    bool fit;
    bool fullscreen;
    bool width;
    bool height;
    bool xemu_override;
    bool hdd_override;
    bool preservation_directory;
} decoded_fields;

void gdox_preferences_defaults(gdox_preferences *preferences)
{
    if (preferences == NULL) {
        return;
    }
    *preferences = (gdox_preferences){
        true,
        2U,
        GDOX_EMULATOR_ASPECT_WIDESCREEN,
        GDOX_EMULATOR_FIT_SCALE,
        true,
        1280U,
        720U,
        "",
        "",
        "",
    };
}

static bool parse_unsigned(
    const char *text,
    size_t bytes,
    unsigned long *value
)
{
    char buffer[32];
    char *end;
    unsigned long parsed;

    if (bytes == 0U || bytes >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, text, bytes);
    buffer[bytes] = '\0';
    errno = 0;
    parsed = strtoul(buffer, &end, 10);
    if (errno != 0 || end != buffer + bytes) {
        return false;
    }
    *value = parsed;
    return true;
}

typedef enum numeric_field_kind {
    NUMERIC_SCHEMA = 0,
    NUMERIC_AUTO_START,
    NUMERIC_SCALE,
    NUMERIC_ASPECT,
    NUMERIC_FIT,
    NUMERIC_FULLSCREEN,
    NUMERIC_WIDTH,
    NUMERIC_HEIGHT
} numeric_field_kind;

typedef struct numeric_field_description {
    const char *key;
    size_t key_bytes;
    numeric_field_kind kind;
} numeric_field_description;

typedef struct string_field_binding {
    const char *key;
    size_t key_bytes;
    char *output;
    size_t capacity;
    bool *seen;
} string_field_binding;

static const numeric_field_description numeric_fields[] = {
    {"schema", 6U, NUMERIC_SCHEMA},
    {"auto_start", 10U, NUMERIC_AUTO_START},
    {"internal_resolution_scale", 25U, NUMERIC_SCALE},
    {"display_aspect", 14U, NUMERIC_ASPECT},
    {"display_fit", 11U, NUMERIC_FIT},
    {"fullscreen", 10U, NUMERIC_FULLSCREEN},
    {"window_width", 12U, NUMERIC_WIDTH},
    {"window_height", 13U, NUMERIC_HEIGHT},
};

static bool key_matches(
    const char *key,
    size_t key_bytes,
    const char *expected,
    size_t expected_bytes
)
{
    return key_bytes == expected_bytes
        && memcmp(key, expected, expected_bytes) == 0;
}

static bool claim_field(bool *seen)
{
    if (*seen) {
        return false;
    }
    *seen = true;
    return true;
}

static const numeric_field_description *find_numeric_field(
    const char *key,
    size_t key_bytes
)
{
    size_t index;
    for (index = 0U;
        index < sizeof(numeric_fields) / sizeof(numeric_fields[0]);
        ++index) {
        if (key_matches(
                key,
                key_bytes,
                numeric_fields[index].key,
                numeric_fields[index].key_bytes
            )) {
            return &numeric_fields[index];
        }
    }
    return NULL;
}

static bool assign_string_field(
    const string_field_binding *binding,
    const char *value,
    size_t value_bytes
)
{
    if (value_bytes >= binding->capacity || !claim_field(binding->seen)) {
        return false;
    }
    memcpy(binding->output, value, value_bytes);
    binding->output[value_bytes] = '\0';
    return true;
}

static bool assign_numeric_field(
    gdox_preferences *preferences,
    decoded_fields *fields,
    numeric_field_kind kind,
    unsigned long value
)
{
    bool *seen;

    switch (kind) {
        case NUMERIC_SCHEMA:
            seen = &fields->schema;
            if (value != GDOX_PREFERENCES_SCHEMA) {
                return false;
            }
            break;
        case NUMERIC_AUTO_START:
            seen = &fields->auto_start;
            if (value > 1U) {
                return false;
            }
            preferences->auto_start = value != 0U;
            break;
        case NUMERIC_SCALE:
            seen = &fields->scale;
            if (value < 1U || value > 10U) {
                return false;
            }
            preferences->internal_resolution_scale = (uint8_t)value;
            break;
        case NUMERIC_ASPECT:
            seen = &fields->aspect;
            if (value > (unsigned long)GDOX_EMULATOR_ASPECT_NATIVE) {
                return false;
            }
            preferences->display_aspect = (gdox_emulator_aspect)value;
            break;
        case NUMERIC_FIT:
            seen = &fields->fit;
            if (value > (unsigned long)GDOX_EMULATOR_FIT_STRETCH) {
                return false;
            }
            preferences->display_fit = (gdox_emulator_fit)value;
            break;
        case NUMERIC_FULLSCREEN:
            seen = &fields->fullscreen;
            if (value > 1U) {
                return false;
            }
            preferences->fullscreen = value != 0U;
            break;
        case NUMERIC_WIDTH:
            seen = &fields->width;
            if (value < 640U || value > 7680U) {
                return false;
            }
            preferences->window_width = (uint16_t)value;
            break;
        case NUMERIC_HEIGHT:
            seen = &fields->height;
            if (value < 480U || value > 4320U) {
                return false;
            }
            preferences->window_height = (uint16_t)value;
            break;
        default:
            return false;
    }
    return claim_field(seen);
}

static bool assign_field(
    gdox_preferences *preferences,
    decoded_fields *fields,
    const char *key,
    size_t key_bytes,
    const char *value,
    size_t value_bytes
)
{
    const string_field_binding strings[] = {
        {
            "xemu_override",
            13U,
            preferences->xemu_override,
            sizeof(preferences->xemu_override),
            &fields->xemu_override,
        },
        {
            "hdd_override",
            12U,
            preferences->hdd_override,
            sizeof(preferences->hdd_override),
            &fields->hdd_override,
        },
        {
            "preservation_directory",
            22U,
            preferences->preservation_directory,
            sizeof(preferences->preservation_directory),
            &fields->preservation_directory,
        },
    };
    const numeric_field_description *numeric;
    unsigned long parsed;
    size_t index;

    for (index = 0U; index < sizeof(strings) / sizeof(strings[0]); ++index) {
        if (key_matches(
                key,
                key_bytes,
                strings[index].key,
                strings[index].key_bytes
            )) {
            return assign_string_field(&strings[index], value, value_bytes);
        }
    }
    if (!parse_unsigned(value, value_bytes, &parsed)) {
        return false;
    }
    numeric = find_numeric_field(key, key_bytes);
    return numeric == NULL
        || assign_numeric_field(preferences, fields, numeric->kind, parsed);
}

static bool decode(
    const char *text,
    gdox_preferences *preferences
)
{
    const char *cursor = text;
    decoded_fields fields = {0};

    gdox_preferences_defaults(preferences);
    while (*cursor != '\0') {
        const char *newline = strchr(cursor, '\n');
        const char *end = newline != NULL ? newline : cursor + strlen(cursor);
        const char *equals;

        if (end > cursor && end[-1] == '\r') {
            --end;
        }
        if (end != cursor) {
            equals = memchr(cursor, '=', (size_t)(end - cursor));
            if (equals == NULL || equals == cursor || equals + 1U == end
                || !assign_field(
                    preferences,
                    &fields,
                    cursor,
                    (size_t)(equals - cursor),
                    equals + 1U,
                    (size_t)(end - equals - 1U)
                )) {
                return false;
            }
        }
        if (newline == NULL) {
            break;
        }
        cursor = newline + 1U;
    }
    return fields.schema;
}

bool gdox_preferences_load(
    gdox_preferences *preferences,
    gdox_error *error
)
{
    char *text = NULL;
    char path[GDOX_STORAGE_PATH_CAPACITY];
    bool found = false;
    size_t bytes = 0U;
    bool success;

    gdox_error_clear(error);
    if (preferences == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "preferences output is required");
        return false;
    }
    gdox_preferences_defaults(preferences);
    if (!gdox_user_config_path("settings.conf", path, error)
        || !gdox_storage_read(
            path,
            (size_t)16U * 1024U,
            (uint8_t **)&text,
            &bytes,
            &found,
            error
        )) {
        return false;
    }
    if (!found) {
        return true;
    }
    success = decode(text, preferences);
    free(text);
    if (!success) {
        gdox_preferences_defaults(preferences);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "saved settings are invalid");
        return false;
    }
    return true;
}

bool gdox_preferences_save(
    const gdox_preferences *preferences,
    gdox_error *error
)
{
    char text[GDOX_EMULATOR_PATH_CAPACITY * 3U + 512U];
    int bytes;
    const bool xemu_override =
        preferences != NULL && preferences->xemu_override[0] != '\0';
    const bool hdd_override =
        preferences != NULL && preferences->hdd_override[0] != '\0';
    const bool preservation_directory =
        preferences != NULL && preferences->preservation_directory[0] != '\0';

    gdox_error_clear(error);
    if (preferences == NULL
        || preferences->internal_resolution_scale < 1U
        || preferences->internal_resolution_scale > 10U
        || preferences->display_aspect > GDOX_EMULATOR_ASPECT_NATIVE
        || preferences->display_fit > GDOX_EMULATOR_FIT_STRETCH
        || strchr(preferences->xemu_override, '\n') != NULL
        || strchr(preferences->xemu_override, '\r') != NULL
        || strchr(preferences->hdd_override, '\n') != NULL
        || strchr(preferences->hdd_override, '\r') != NULL
        || strchr(preferences->preservation_directory, '\n') != NULL
        || strchr(preferences->preservation_directory, '\r') != NULL
        || preferences->window_width < 640U
        || preferences->window_width > 7680U
        || preferences->window_height < 480U
        || preferences->window_height > 4320U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid preferences are required");
        return false;
    }
    bytes = snprintf(
        text,
        sizeof(text),
        "schema=%u\n"
        "auto_start=%u\n"
        "internal_resolution_scale=%u\n"
        "display_aspect=%u\n"
        "display_fit=%u\n"
        "fullscreen=%u\n"
        "window_width=%u\n"
        "window_height=%u\n"
        "%s%s%s"
        "%s%s%s"
        "%s%s%s",
        GDOX_PREFERENCES_SCHEMA,
        preferences->auto_start ? 1U : 0U,
        (unsigned int)preferences->internal_resolution_scale,
        (unsigned int)preferences->display_aspect,
        (unsigned int)preferences->display_fit,
        preferences->fullscreen ? 1U : 0U,
        (unsigned int)preferences->window_width,
        (unsigned int)preferences->window_height,
        xemu_override ? "xemu_override=" : "",
        xemu_override ? preferences->xemu_override : "",
        xemu_override ? "\n" : "",
        hdd_override ? "hdd_override=" : "",
        hdd_override ? preferences->hdd_override : "",
        hdd_override ? "\n" : "",
        preservation_directory ? "preservation_directory=" : "",
        preservation_directory ? preferences->preservation_directory : "",
        preservation_directory ? "\n" : ""
    );
    if (bytes < 0 || (size_t)bytes >= sizeof(text)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not encode preferences");
        return false;
    }
    {
        char path[GDOX_STORAGE_PATH_CAPACITY];
        return gdox_user_config_path("settings.conf", path, error)
            && gdox_storage_write_private(
                path,
                (const uint8_t *)text,
                (size_t)bytes,
                true,
                error
            );
    }
}
