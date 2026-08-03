#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_xemu_helper.h"

#include "core/xemu_capabilities.h"
#include "platform/xemu_helper_process.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

static bool capability_environment_isolated(void)
{
    static const char *const names[] = {
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "TMPDIR",
#if defined(_WIN32)
        "APPDATA",
        "LOCALAPPDATA",
        "TEMP",
        "TMP",
        "USERPROFILE",
#endif
    };
    const char *home = getenv("HOME");
    size_t index;

    if (home == NULL || home[0] == '\0') {
        return false;
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *value = getenv(names[index]);

        if (value == NULL || strcmp(value, home) != 0) {
            return false;
        }
    }
    if (getenv("MESA_SHADER_CACHE_DISABLE") == NULL
        || strcmp(getenv("MESA_SHADER_CACHE_DISABLE"), "1") != 0
        || getenv("__GL_SHADER_DISK_CACHE") == NULL
        || strcmp(getenv("__GL_SHADER_DISK_CACHE"), "0") != 0
        || getenv("MESA_SHADER_CACHE_DIR") != NULL
        || getenv("__GL_SHADER_DISK_CACHE_PATH") != NULL
#if !defined(_WIN32)
        || getenv("LD_PRELOAD") != NULL
#endif
    ) {
        return false;
    }
#if defined(_WIN32)
    {
        wchar_t current[32768];
        wchar_t profile[32768];
        const DWORD current_length = GetCurrentDirectoryW(
            (DWORD)(sizeof(current) / sizeof(current[0])), current
        );
        const DWORD profile_length = GetEnvironmentVariableW(
            L"HOME",
            profile,
            (DWORD)(sizeof(profile) / sizeof(profile[0]))
        );

        if (current_length == 0U
            || current_length >= sizeof(current) / sizeof(current[0])
            || profile_length == 0U
            || profile_length >= sizeof(profile) / sizeof(profile[0])
            || _wcsicmp(current, profile) != 0) {
            return false;
        }
    }
#endif
    return true;
}

int gdox_test_xemu_capabilities(void)
{
    const char *mode = getenv("GDOX_TEST_XEMU_CAPABILITY_MODE");

    if (mode != NULL && strcmp(mode, "hang") == 0) {
#if defined(_WIN32)
        Sleep(10000U);
#else
        const struct timespec duration = {10, 0};
        (void)nanosleep(&duration, NULL);
#endif
    }
    if (mode != NULL && strcmp(mode, "stderr") == 0) {
        (void)fputs("unexpected diagnostic\n", stderr);
    }
    if (mode != NULL && strcmp(mode, "nonzero") == 0) {
        return 7;
    }
    if (mode != NULL && strcmp(mode, "oversized") == 0) {
        unsigned int index;
        for (index = 0U;
             index < GDOX_XEMU_HELPER_CAPTURE_BYTES * 2U;
             ++index) {
            (void)fputc('x', stdout);
        }
        return 0;
    }
    if (mode != NULL && strcmp(mode, "malformed") == 0) {
        (void)puts("{\"schema\":2}");
        return 0;
    }
    if (mode != NULL && strcmp(mode, "unknown-field") == 0) {
        (void)puts(
            "{\"schema\":1,\"runtime\":\"xemu\",\"storage\":{"
            "\"full_hdd_ram_cow\":true,\"backing_writes\":false,"
            "\"persistent_save_export\":false,"
            "\"max_dirty_bytes\":4294967296,\"unexpected\":true}}"
        );
        return 0;
    }
    if (mode != NULL && strcmp(mode, "isolation-check") == 0
        && !capability_environment_isolated()) {
        return 9;
    }
    if (mode != NULL && strcmp(mode, "profile-write") == 0) {
        const char *home = getenv("HOME");
        char path[4096];
        FILE *file;

        if (home == NULL
            || snprintf(path, sizeof(path), "%s/probe.bin", home) < 0) {
            return 8;
        }
        file = fopen(path, "wb");
        if (file == NULL) {
            return 8;
        }
        if (fputs("probe", file) < 0 || fclose(file) != 0) {
            return 8;
        }
    }
    (void)puts(
        mode != NULL && strcmp(mode, "save-export") == 0
            ? GDOX_XEMU_CAPABILITIES_TRUE_RESPONSE
            : GDOX_XEMU_CAPABILITIES_FALSE_RESPONSE
    );
    return 0;
}
