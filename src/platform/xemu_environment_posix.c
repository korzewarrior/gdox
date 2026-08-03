#define _POSIX_C_SOURCE 200809L

#include "platform/xemu_runtime_session.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

static bool isolated_name(const char *value)
{
    static const char *const names[] = {
        "GDOX_XEMU_CONFIG",
        "HOME",
        "LD_PRELOAD",
        "MESA_SHADER_CACHE_DIR",
        "MESA_SHADER_CACHE_DISABLE",
        "TMPDIR",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "__GL_SHADER_DISK_CACHE",
        "__GL_SHADER_DISK_CACHE_PATH",
    };
    size_t index;

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const size_t bytes = strlen(names[index]);

        if (strncmp(value, names[index], bytes) == 0
            && value[bytes] == '=') {
            return true;
        }
    }
    return false;
}

static char *environment_value(
    const char *name,
    const char *value,
    gdox_error *error
)
{
    const size_t name_bytes = strlen(name);
    const size_t value_bytes = strlen(value);
    char *entry;

    if (name_bytes > SIZE_MAX - value_bytes - 2U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu runtime environment is too large"
        );
        return NULL;
    }
    entry = malloc(name_bytes + value_bytes + 2U);
    if (entry == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate xemu runtime environment"
        );
        return NULL;
    }
    (void)snprintf(
        entry, name_bytes + value_bytes + 2U, "%s=%s", name, value
    );
    return entry;
}

void gdox_xemu_environment_destroy(gdox_xemu_environment *environment)
{
    size_t index;

    if (environment == NULL) {
        return;
    }
    for (index = 0U; index < environment->count; ++index) {
        free(environment->values[index]);
    }
    free(environment->values);
    memset(environment, 0, sizeof(*environment));
}

bool gdox_xemu_environment_create(
    const char *session_root,
    gdox_xemu_environment *environment,
    gdox_error *error
)
{
    static const char *const names[] = {
        "HOME",
        "TMPDIR",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
    };
    static const struct {
        const char *name;
        const char *value;
    } fixed[] = {
        {"MESA_SHADER_CACHE_DISABLE", "1"},
        {"__GL_SHADER_DISK_CACHE", "0"},
    };
    size_t inherited = 0U;
    size_t retained = 0U;
    size_t index;

    gdox_error_clear(error);
    if (session_root == NULL || session_root[0] != '/'
        || environment == NULL || environment->values != NULL
        || environment->count != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "isolated xemu session and empty environment are required"
        );
        return false;
    }
    while (environ[inherited] != NULL) {
        if (!isolated_name(environ[inherited])) {
            ++retained;
        }
        ++inherited;
    }
    if (retained > SIZE_MAX / sizeof(*environment->values)
        - sizeof(names) / sizeof(names[0])
        - sizeof(fixed) / sizeof(fixed[0]) - 1U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "xemu runtime environment is too large"
        );
        return false;
    }
    environment->values = calloc(
        retained + sizeof(names) / sizeof(names[0])
            + sizeof(fixed) / sizeof(fixed[0]) + 1U,
        sizeof(*environment->values)
    );
    if (environment->values == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate xemu runtime environment"
        );
        return false;
    }
    for (index = 0U; index < inherited; ++index) {
        if (!isolated_name(environ[index])) {
            environment->values[environment->count] = strdup(environ[index]);
            if (environment->values[environment->count] == NULL) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_INTERNAL,
                    "could not copy xemu runtime environment"
                );
                gdox_xemu_environment_destroy(environment);
                return false;
            }
            ++environment->count;
        }
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        environment->values[environment->count] = environment_value(
            names[index], session_root, error
        );
        if (environment->values[environment->count] == NULL) {
            gdox_xemu_environment_destroy(environment);
            return false;
        }
        ++environment->count;
    }
    for (index = 0U; index < sizeof(fixed) / sizeof(fixed[0]); ++index) {
        environment->values[environment->count] = environment_value(
            fixed[index].name, fixed[index].value, error
        );
        if (environment->values[environment->count] == NULL) {
            gdox_xemu_environment_destroy(environment);
            return false;
        }
        ++environment->count;
    }
    environment->values[environment->count] = NULL;
    return true;
}
