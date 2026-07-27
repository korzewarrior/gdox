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

static bool assign_field(
    gdox_preferences *preferences,
    decoded_fields *fields,
    const char *key,
    size_t key_bytes,
    const char *value,
    size_t value_bytes
)
{
    unsigned long parsed = 0U;
    bool *seen = NULL;

    if (key_bytes == 13U && memcmp(key, "xemu_override", 13U) == 0) {
        if (value_bytes >= sizeof(preferences->xemu_override)) {
            return false;
        }
        seen = &fields->xemu_override;
        memcpy(preferences->xemu_override, value, value_bytes);
        preferences->xemu_override[value_bytes] = '\0';
    } else if (key_bytes == 12U
        && memcmp(key, "hdd_override", 12U) == 0) {
        if (value_bytes >= sizeof(preferences->hdd_override)) {
            return false;
        }
        seen = &fields->hdd_override;
        memcpy(preferences->hdd_override, value, value_bytes);
        preferences->hdd_override[value_bytes] = '\0';
    } else if (key_bytes == 22U
        && memcmp(key, "preservation_directory", 22U) == 0) {
        if (value_bytes >= sizeof(preferences->preservation_directory)) {
            return false;
        }
        seen = &fields->preservation_directory;
        memcpy(preferences->preservation_directory, value, value_bytes);
        preferences->preservation_directory[value_bytes] = '\0';
    } else {
        if (!parse_unsigned(value, value_bytes, &parsed)) {
            return false;
        }
    }
    if (seen != NULL) {
        if (*seen) {
            return false;
        }
        *seen = true;
        return true;
    }
    if (key_bytes == 6U && memcmp(key, "schema", 6U) == 0) {
        seen = &fields->schema;
        if (parsed != GDOX_PREFERENCES_SCHEMA) {
            return false;
        }
    } else if (key_bytes == 10U && memcmp(key, "auto_start", 10U) == 0) {
        seen = &fields->auto_start;
        if (parsed > 1U) {
            return false;
        }
        preferences->auto_start = parsed != 0U;
    } else if (key_bytes == 25U
        && memcmp(key, "internal_resolution_scale", 25U) == 0) {
        seen = &fields->scale;
        if (parsed < 1U || parsed > 10U) {
            return false;
        }
        preferences->internal_resolution_scale = (uint8_t)parsed;
    } else if (key_bytes == 14U
        && memcmp(key, "display_aspect", 14U) == 0) {
        seen = &fields->aspect;
        if (parsed > (unsigned long)GDOX_EMULATOR_ASPECT_NATIVE) {
            return false;
        }
        preferences->display_aspect = (gdox_emulator_aspect)parsed;
    } else if (key_bytes == 11U
        && memcmp(key, "display_fit", 11U) == 0) {
        seen = &fields->fit;
        if (parsed > (unsigned long)GDOX_EMULATOR_FIT_STRETCH) {
            return false;
        }
        preferences->display_fit = (gdox_emulator_fit)parsed;
    } else if (key_bytes == 10U && memcmp(key, "fullscreen", 10U) == 0) {
        seen = &fields->fullscreen;
        if (parsed > 1U) {
            return false;
        }
        preferences->fullscreen = parsed != 0U;
    } else if (key_bytes == 12U
        && memcmp(key, "window_width", 12U) == 0) {
        seen = &fields->width;
        if (parsed < 640U || parsed > 7680U) {
            return false;
        }
        preferences->window_width = (uint16_t)parsed;
    } else if (key_bytes == 13U
        && memcmp(key, "window_height", 13U) == 0) {
        seen = &fields->height;
        if (parsed < 480U || parsed > 4320U) {
            return false;
        }
        preferences->window_height = (uint16_t)parsed;
    } else {
        return true;
    }
    if (*seen) {
        return false;
    }
    *seen = true;
    return true;
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
