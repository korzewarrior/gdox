#include "core/emulator_configuration.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct text_buffer {
    char *data;
    size_t bytes;
    size_t capacity;
} text_buffer;

static bool text_reserve(
    text_buffer *buffer,
    size_t additional,
    gdox_error *error
)
{
    size_t needed;
    size_t capacity;
    char *resized;

    if (additional > SIZE_MAX - buffer->bytes - 1U) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "configuration text is too large");
        return false;
    }
    needed = buffer->bytes + additional + 1U;
    if (needed <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0U ? 1024U : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    resized = realloc(buffer->data, capacity);
    if (resized == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate configuration text");
        return false;
    }
    buffer->data = resized;
    buffer->capacity = capacity;
    return true;
}

static bool text_append(
    text_buffer *buffer,
    const char *text,
    size_t bytes,
    gdox_error *error
)
{
    if (!text_reserve(buffer, bytes, error)) {
        return false;
    }
    memcpy(buffer->data + buffer->bytes, text, bytes);
    buffer->bytes += bytes;
    buffer->data[buffer->bytes] = '\0';
    return true;
}

static bool line_is_section(
    const char *line,
    size_t bytes,
    const char *section
)
{
    const char *begin = line;
    const char *end = line + bytes;
    const size_t section_bytes = strlen(section);

    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    while (end > begin
        && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    return (size_t)(end - begin) == section_bytes + 2U
        && begin[0] == '[' && begin[section_bytes + 1U] == ']'
        && memcmp(begin + 1U, section, section_bytes) == 0;
}

static bool line_is_any_section(const char *line, size_t bytes)
{
    const char *begin = line;
    const char *end = line + bytes;
    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    while (end > begin
        && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    return end > begin + 2U && begin[0] == '[' && end[-1] == ']';
}

static bool line_is_key(const char *line, size_t bytes, const char *key)
{
    const char *begin = line;
    const char *end = line + bytes;
    const char *equals;
    const size_t key_bytes = strlen(key);

    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    equals = memchr(begin, '=', (size_t)(end - begin));
    if (equals == NULL) {
        return false;
    }
    while (equals > begin && (equals[-1] == ' ' || equals[-1] == '\t')) {
        --equals;
    }
    return (size_t)(equals - begin) == key_bytes
        && memcmp(begin, key, key_bytes) == 0;
}

static bool append_assignment(
    text_buffer *output,
    const char *key,
    const char *value,
    gdox_error *error
)
{
    return text_append(output, key, strlen(key), error)
        && text_append(output, " = ", 3U, error)
        && text_append(output, value, strlen(value), error)
        && text_append(output, "\n", 1U, error);
}

typedef struct toml_update {
    text_buffer output;
    const char *section;
    const char *key;
    const char *value;
    bool in_section;
    bool saw_section;
    bool saw_key;
} toml_update;

static bool append_line_break_if_needed(
    text_buffer *output,
    gdox_error *error
)
{
    return output->bytes == 0U || output->data[output->bytes - 1U] == '\n'
        || text_append(output, "\n", 1U, error);
}

static bool enter_toml_line(
    toml_update *update,
    const char *line,
    size_t line_bytes,
    gdox_error *error
)
{
    const bool target_section =
        line_is_section(line, line_bytes, update->section);
    const bool any_section = line_is_any_section(line, line_bytes);

    if (any_section && update->in_section && !target_section
        && !update->saw_key) {
        if (!append_assignment(
            &update->output,
            update->key,
            update->value,
            error
        )) {
            return false;
        }
        update->saw_key = true;
    }
    if (target_section && update->saw_section) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "xemu configuration repeats a managed section"
        );
        return false;
    }
    if (target_section) {
        update->saw_section = true;
        update->in_section = true;
    } else if (any_section) {
        update->in_section = false;
    }
    return true;
}

static bool write_toml_line(
    toml_update *update,
    const char *line,
    size_t line_bytes,
    size_t original_bytes,
    gdox_error *error
)
{
    if (!update->in_section
        || !line_is_key(line, line_bytes, update->key)) {
        return text_append(&update->output, line, original_bytes, error);
    }
    if (update->saw_key) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "xemu configuration repeats a managed key"
        );
        return false;
    }
    update->saw_key = true;
    return append_assignment(
        &update->output,
        update->key,
        update->value,
        error
    );
}

static bool finish_toml_update(toml_update *update, gdox_error *error)
{
    if (update->in_section && !update->saw_key) {
        return append_line_break_if_needed(&update->output, error)
            && append_assignment(
                &update->output,
                update->key,
                update->value,
                error
            );
    }
    if (update->saw_section) {
        return true;
    }
    return append_line_break_if_needed(&update->output, error)
        && (update->output.bytes == 0U
            || text_append(&update->output, "\n", 1U, error))
        && text_append(&update->output, "[", 1U, error)
        && text_append(
            &update->output,
            update->section,
            strlen(update->section),
            error
        )
        && text_append(&update->output, "]\n", 2U, error)
        && append_assignment(
            &update->output,
            update->key,
            update->value,
            error
        );
}

static bool set_toml_key(
    const char *input,
    const char *section,
    const char *key,
    const char *value,
    char **updated,
    gdox_error *error
)
{
    toml_update update = {
        .section = section,
        .key = key,
        .value = value,
    };
    const char *cursor = input;

    while (*cursor != '\0') {
        const char *newline = strchr(cursor, '\n');
        const size_t line_bytes =
            newline != NULL ? (size_t)(newline - cursor) : strlen(cursor);
        const size_t original_bytes = line_bytes + (newline != NULL ? 1U : 0U);

        if (!enter_toml_line(&update, cursor, line_bytes, error)
            || !write_toml_line(
                &update,
                cursor,
                line_bytes,
                original_bytes,
                error
            )) {
            free(update.output.data);
            return false;
        }
        cursor += original_bytes;
    }
    if (!finish_toml_update(&update, error)) {
        free(update.output.data);
        return false;
    }
    *updated = update.output.data;
    return true;
}

static bool supported_file_key(const char *key)
{
    return key != NULL
        && (strcmp(key, "bootrom_path") == 0
            || strcmp(key, "flashrom_path") == 0
            || strcmp(key, "hdd_path") == 0
            || strcmp(key, "eeprom_path") == 0);
}

static bool decode_string(
    const char *input,
    size_t bytes,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    size_t input_index;
    size_t output_index = 0U;
    char quote;

    while (bytes != 0U && (*input == ' ' || *input == '\t')) {
        ++input;
        --bytes;
    }
    while (bytes != 0U
        && (input[bytes - 1U] == ' ' || input[bytes - 1U] == '\t'
            || input[bytes - 1U] == '\r')) {
        --bytes;
    }
    if (bytes < 2U || (input[0] != '\'' && input[0] != '"')
        || input[bytes - 1U] != input[0]) {
        return false;
    }
    quote = input[0];
    for (input_index = 1U; input_index + 1U < bytes; ++input_index) {
        char value = input[input_index];
        if (quote == '"' && value == '\\') {
            if (++input_index + 1U >= bytes) {
                return false;
            }
            value = input[input_index];
            if (value != '\\' && value != '"') {
                return false;
            }
        } else if (value == quote || (unsigned char)value < 0x20U) {
            return false;
        }
        if (output_index + 1U >= GDOX_EMULATOR_PATH_CAPACITY) {
            return false;
        }
        output[output_index++] = value;
    }
    output[output_index] = '\0';
    return output_index != 0U;
}

bool gdox_emulator_configuration_get_file(
    const char *configuration,
    const char *key,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    const char *cursor = configuration;
    bool in_files = false;

    gdox_error_clear(error);
    if (configuration == NULL || !supported_file_key(key) || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "configuration, file key, and output are required");
        return false;
    }
    output[0] = '\0';
    while (*cursor != '\0') {
        const char *newline = strchr(cursor, '\n');
        const size_t line_bytes = newline != NULL
            ? (size_t)(newline - cursor)
            : strlen(cursor);
        if (line_is_any_section(cursor, line_bytes)) {
            in_files = line_is_section(cursor, line_bytes, "sys.files");
        } else if (in_files && line_is_key(cursor, line_bytes, key)) {
            const char *equals = memchr(cursor, '=', line_bytes);
            if (equals == NULL
                || !decode_string(
                    equals + 1U,
                    (size_t)(cursor + line_bytes - equals - 1U),
                    output
                )) {
                gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "xemu file path is invalid");
                return false;
            }
            return true;
        }
        if (newline == NULL) {
            break;
        }
        cursor = newline + 1U;
    }
    gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "xemu file path is not configured");
    return false;
}

static bool quote_path(
    const char *path,
    char **quoted,
    gdox_error *error
)
{
    text_buffer output = {0};
    const char *cursor = path;

    if (!text_append(&output, "\"", 1U, error)) {
        return false;
    }
    while (*cursor != '\0') {
        if ((unsigned char)*cursor < 0x20U) {
            free(output.data);
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "xemu file path contains a control character");
            return false;
        }
        if ((*cursor == '\\' || *cursor == '"')
            && !text_append(&output, "\\", 1U, error)) {
            free(output.data);
            return false;
        }
        if (!text_append(&output, cursor, 1U, error)) {
            free(output.data);
            return false;
        }
        ++cursor;
    }
    if (!text_append(&output, "\"", 1U, error)) {
        free(output.data);
        return false;
    }
    *quoted = output.data;
    return true;
}

bool gdox_emulator_configuration_set_file(
    const char *configuration,
    const char *key,
    const char *path,
    char **updated,
    gdox_error *error
)
{
    char *quoted = NULL;
    bool success;

    gdox_error_clear(error);
    if (configuration == NULL || !supported_file_key(key)
        || path == NULL || path[0] == '\0' || updated == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "configuration, file key, path, and output are required");
        return false;
    }
    *updated = NULL;
    if (!quote_path(path, &quoted, error)) {
        return false;
    }
    success = set_toml_key(
        configuration,
        "sys.files",
        key,
        quoted,
        updated,
        error
    );
    free(quoted);
    return success;
}

bool gdox_emulator_configuration_update(
    const gdox_emulator_options *options,
    const char *original,
    char **updated,
    gdox_error *error
)
{
    char scale[16];
    char width[16];
    char height[16];
    const char *aspect;
    const char *fit;
    char *general = NULL;
    char *system = NULL;
    char *performance = NULL;
    char *quality = NULL;
    char *ui_aspect = NULL;
    char *ui_fit = NULL;
    char *window = NULL;
    char *window_size = NULL;
    char *window_width = NULL;
    char *window_height = NULL;
    bool success = false;

    gdox_error_clear(error);
    if (options == NULL || original == NULL || updated == NULL
        || options->internal_resolution_scale < 1U
        || options->internal_resolution_scale > 10U
        || options->aspect > GDOX_EMULATOR_ASPECT_NATIVE
        || options->fit > GDOX_EMULATOR_FIT_STRETCH
        || options->window_width < 640U
        || options->window_width > 7680U
        || options->window_height < 480U
        || options->window_height > 4320U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid display scale and window size are required");
        return false;
    }
    *updated = NULL;
    if (options->aspect == GDOX_EMULATOR_ASPECT_AUTOMATIC) {
        aspect = "\"auto\"";
    } else if (options->aspect == GDOX_EMULATOR_ASPECT_WIDESCREEN) {
        aspect = "\"16x9\"";
    } else if (options->aspect == GDOX_EMULATOR_ASPECT_FOUR_THREE) {
        aspect = "\"4x3\"";
    } else if (options->aspect == GDOX_EMULATOR_ASPECT_NATIVE) {
        aspect = "\"native\"";
    } else {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "invalid xemu aspect setting");
        return false;
    }
    if (options->fit == GDOX_EMULATOR_FIT_CENTER) {
        fit = "\"center\"";
    } else if (options->fit == GDOX_EMULATOR_FIT_SCALE) {
        fit = "\"scale\"";
    } else {
        fit = "\"stretch\"";
    }
    (void)snprintf(
        scale,
        sizeof(scale),
        "%u",
        (unsigned int)options->internal_resolution_scale
    );
    (void)snprintf(
        width,
        sizeof(width),
        "%u",
        (unsigned int)options->window_width
    );
    (void)snprintf(
        height,
        sizeof(height),
        "%u",
        (unsigned int)options->window_height
    );
    if (!set_toml_key(
            original,
            "general",
            "show_welcome",
            "false",
            &general,
            error
        )
        || !set_toml_key(
            general,
            "sys",
            "volatile_hard_disk",
            "true",
            &system,
            error
        )
        || !set_toml_key(
            system,
            "perf",
            "cache_shaders",
            "false",
            &performance,
            error
        )
        || !set_toml_key(
            performance,
            "display.quality",
            "surface_scale",
            scale,
            &quality,
            error
        )
        || !set_toml_key(
            quality,
            "display.ui",
            "aspect_ratio",
            aspect,
            &ui_aspect,
            error
        )
        || !set_toml_key(
            ui_aspect,
            "display.ui",
            "fit",
            fit,
            &ui_fit,
            error
        )
        || !set_toml_key(
            ui_fit,
            "display.window",
            "fullscreen_on_startup",
            options->fullscreen ? "true" : "false",
            &window,
            error
        )
        || !set_toml_key(
            window,
            "display.window",
            "startup_size",
            "\"last_used\"",
            &window_size,
            error
        )
        || !set_toml_key(
            window_size,
            "display.window",
            "last_width",
            width,
            &window_width,
            error
        )
        || !set_toml_key(
            window_width,
            "display.window",
            "last_height",
            height,
            &window_height,
            error
        )) {
        goto cleanup;
    }
    *updated = window_height;
    window_height = NULL;
    success = true;

cleanup:
    free(general);
    free(system);
    free(performance);
    free(quality);
    free(ui_aspect);
    free(ui_fit);
    free(window);
    free(window_size);
    free(window_width);
    free(window_height);
    return success;
}
