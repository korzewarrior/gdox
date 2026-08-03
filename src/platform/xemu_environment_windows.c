#define WIN32_LEAN_AND_MEAN

#include "platform/xemu_runtime_session.h"
#include "platform/windows_support.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_ENVIRONMENT_CAPACITY 32768U

static bool isolated_name(const wchar_t *value)
{
    static const wchar_t *const names[] = {
        L"APPDATA",
        L"GDOX_XEMU_CONFIG",
        L"HOME",
        L"LOCALAPPDATA",
        L"MESA_SHADER_CACHE_DIR",
        L"MESA_SHADER_CACHE_DISABLE",
        L"TEMP",
        L"TMP",
        L"TMPDIR",
        L"USERPROFILE",
        L"XDG_CACHE_HOME",
        L"XDG_CONFIG_HOME",
        L"XDG_DATA_HOME",
        L"XDG_STATE_HOME",
        L"__GL_SHADER_DISK_CACHE",
        L"__GL_SHADER_DISK_CACHE_PATH",
    };
    size_t index;

    if (value[0] == L'=') {
        return true;
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const size_t characters = wcslen(names[index]);

        if (_wcsnicmp(value, names[index], characters) == 0
            && value[characters] == L'=') {
            return true;
        }
    }
    return false;
}

static int compare_environment_entry(const void *left, const void *right)
{
    const wchar_t *const *left_entry = left;
    const wchar_t *const *right_entry = right;

    return _wcsicmp(*left_entry, *right_entry);
}

void gdox_xemu_environment_destroy(gdox_xemu_environment *environment)
{
    if (environment != NULL) {
        free(environment->block);
        memset(environment, 0, sizeof(*environment));
    }
}

bool gdox_xemu_environment_create(
    const char *session_root,
    gdox_xemu_environment *environment,
    gdox_error *error
)
{
    wchar_t appdata[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t gdox_config[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t home[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t local_appdata[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t mesa_disable[64];
    wchar_t temp[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t tmp[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t tmpdir[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t userprofile[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t xdg_cache[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t xdg_config[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t xdg_data[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t xdg_state[GDOX_WINDOWS_ENVIRONMENT_CAPACITY + 32U];
    wchar_t gl_disable[64];
    wchar_t *overrides[] = {
        appdata,
        gdox_config,
        home,
        local_appdata,
        mesa_disable,
        temp,
        tmp,
        tmpdir,
        userprofile,
        xdg_cache,
        xdg_config,
        xdg_data,
        xdg_state,
        gl_disable,
    };
    wchar_t *root = NULL;
    wchar_t *inherited = NULL;
    const wchar_t *cursor;
    wchar_t **entries = NULL;
    size_t count = 0U;
    size_t index = 0U;
    size_t characters = 1U;
    bool success = false;

    gdox_error_clear(error);
    if (environment == NULL || environment->block != NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "isolated xemu session and empty environment are required"
        );
        return false;
    }
    root = gdox_windows_wide_path(session_root, error);
    inherited = GetEnvironmentStringsW();
    if (root == NULL || inherited == NULL
        || swprintf(appdata, sizeof(appdata) / sizeof(appdata[0]),
                    L"APPDATA=%ls", root) < 0
        || swprintf(gdox_config,
                    sizeof(gdox_config) / sizeof(gdox_config[0]),
                    L"GDOX_XEMU_CONFIG=") < 0
        || swprintf(home, sizeof(home) / sizeof(home[0]),
                    L"HOME=%ls", root) < 0
        || swprintf(local_appdata,
                    sizeof(local_appdata) / sizeof(local_appdata[0]),
                    L"LOCALAPPDATA=%ls", root) < 0
        || swprintf(mesa_disable,
                    sizeof(mesa_disable) / sizeof(mesa_disable[0]),
                    L"MESA_SHADER_CACHE_DISABLE=1") < 0
        || swprintf(temp, sizeof(temp) / sizeof(temp[0]),
                    L"TEMP=%ls", root) < 0
        || swprintf(tmp, sizeof(tmp) / sizeof(tmp[0]),
                    L"TMP=%ls", root) < 0
        || swprintf(tmpdir, sizeof(tmpdir) / sizeof(tmpdir[0]),
                    L"TMPDIR=%ls", root) < 0
        || swprintf(userprofile,
                    sizeof(userprofile) / sizeof(userprofile[0]),
                    L"USERPROFILE=%ls", root) < 0
        || swprintf(xdg_cache,
                    sizeof(xdg_cache) / sizeof(xdg_cache[0]),
                    L"XDG_CACHE_HOME=%ls", root) < 0
        || swprintf(xdg_config,
                    sizeof(xdg_config) / sizeof(xdg_config[0]),
                    L"XDG_CONFIG_HOME=%ls", root) < 0
        || swprintf(xdg_data,
                    sizeof(xdg_data) / sizeof(xdg_data[0]),
                    L"XDG_DATA_HOME=%ls", root) < 0
        || swprintf(xdg_state,
                    sizeof(xdg_state) / sizeof(xdg_state[0]),
                    L"XDG_STATE_HOME=%ls", root) < 0
        || swprintf(gl_disable,
                    sizeof(gl_disable) / sizeof(gl_disable[0]),
                    L"__GL_SHADER_DISK_CACHE=0") < 0) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not build isolated xemu runtime environment"
            );
        }
        goto cleanup;
    }
    for (cursor = inherited; *cursor != L'\0'; cursor += wcslen(cursor) + 1U) {
        if (!isolated_name(cursor)) {
            ++count;
        }
    }
    entries = calloc(
        count + sizeof(overrides) / sizeof(overrides[0]),
        sizeof(*entries)
    );
    if (entries == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate xemu runtime environment"
        );
        goto cleanup;
    }
    for (cursor = inherited; *cursor != L'\0'; cursor += wcslen(cursor) + 1U) {
        if (!isolated_name(cursor)) {
            entries[index++] = (wchar_t *)cursor;
        }
    }
    for (count = 0U; count < sizeof(overrides) / sizeof(overrides[0]); ++count) {
        entries[index++] = overrides[count];
    }
    qsort(entries, index, sizeof(*entries), compare_environment_entry);
    for (count = 0U; count < index; ++count) {
        const size_t entry_characters = wcslen(entries[count]) + 1U;

        if (characters > SIZE_MAX - entry_characters) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "xemu runtime environment is too large"
            );
            goto cleanup;
        }
        characters += entry_characters;
    }
    if (characters > SIZE_MAX / sizeof(*environment->block)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "xemu runtime environment is too large"
        );
        goto cleanup;
    }
    environment->block = calloc(characters, sizeof(*environment->block));
    if (environment->block == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate xemu runtime environment block"
        );
        goto cleanup;
    }
    {
        wchar_t *destination = environment->block;

        for (count = 0U; count < index; ++count) {
            const size_t entry_characters = wcslen(entries[count]) + 1U;

            memcpy(
                destination,
                entries[count],
                entry_characters * sizeof(*destination)
            );
            destination += entry_characters;
        }
        *destination = L'\0';
    }
    success = true;

cleanup:
    free(entries);
    if (inherited != NULL) {
        (void)FreeEnvironmentStringsW(inherited);
    }
    free(root);
    if (!success) {
        gdox_xemu_environment_destroy(environment);
    }
    return success;
}
