#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "app/preferences.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_rmdir _rmdir
#define gdox_test_remove _unlink
static bool set_settings_home(const char *path)
{
    return SetEnvironmentVariableA("GDOX_CONFIG_HOME", path) != 0;
}
static void clear_settings_home(void)
{
    (void)SetEnvironmentVariableA("GDOX_CONFIG_HOME", NULL);
}
#else
#include <sys/stat.h>
#include <unistd.h>
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_rmdir rmdir
#define gdox_test_remove unlink
static bool set_settings_home(const char *path)
{
    return setenv("GDOX_CONFIG_HOME", path, 1) == 0;
}
static void clear_settings_home(void)
{
    (void)unsetenv("GDOX_CONFIG_HOME");
}
#endif

void gdox_test_preferences(void)
{
    char directory[256];
    char path[320];
    char preservation_path[320];
    gdox_preferences saved;
    gdox_preferences loaded;
    gdox_error error;
    FILE *invalid;

    (void)snprintf(
        directory,
        sizeof(directory),
        "./gdox-settings-%d-%lld",
        gdox_test_getpid(),
        (long long)time(NULL)
    );
    (void)snprintf(
        path,
        sizeof(path),
        "%s/settings.conf",
        directory
    );
    (void)snprintf(
        preservation_path,
        sizeof(preservation_path),
        "%s/preservations",
        directory
    );
    (void)gdox_test_remove(path);
    (void)gdox_test_rmdir(directory);
    GDOX_TEST_CHECK(gdox_test_mkdir(directory) == 0);
    GDOX_TEST_CHECK(
        gdox_storage_ensure_directory(preservation_path, &error)
    );
    GDOX_TEST_CHECK(set_settings_home(directory));
    GDOX_TEST_CHECK(gdox_preferences_load(&loaded, &error));
    GDOX_TEST_CHECK(loaded.auto_start);
    GDOX_TEST_CHECK(loaded.internal_resolution_scale == 2U);
    GDOX_TEST_CHECK(loaded.display_aspect == GDOX_EMULATOR_ASPECT_WIDESCREEN);
    GDOX_TEST_CHECK(loaded.display_fit == GDOX_EMULATOR_FIT_SCALE);
    GDOX_TEST_CHECK(loaded.fullscreen);
    GDOX_TEST_CHECK(loaded.window_width == 1280U);
    GDOX_TEST_CHECK(loaded.window_height == 720U);
    GDOX_TEST_CHECK(loaded.xemu_override[0] == '\0');
    GDOX_TEST_CHECK(loaded.hdd_override[0] == '\0');
    GDOX_TEST_CHECK(loaded.preservation_directory[0] == '\0');

    invalid = fopen(path, "wb");
    GDOX_TEST_CHECK(invalid != NULL);
    GDOX_TEST_CHECK(
        fputs(
            "schema=1\n"
            "auto_start=0\n"
            "internal_resolution_scale=3\n"
            "display_aspect=0\n"
            "fullscreen=0\n"
            "window_width=1600\n"
            "window_height=900\n",
            invalid
        ) >= 0
    );
    GDOX_TEST_CHECK(fclose(invalid) == 0);
    GDOX_TEST_CHECK(gdox_preferences_load(&loaded, &error));
    GDOX_TEST_CHECK(loaded.display_fit == GDOX_EMULATOR_FIT_SCALE);

    saved = (gdox_preferences){
        false,
        4U,
        GDOX_EMULATOR_ASPECT_FOUR_THREE,
        GDOX_EMULATOR_FIT_STRETCH,
        false,
        1920U,
        1080U,
        "/opt/xemu/bin/xemu",
        "/example/xbox_hdd.qcow2",
        "/example/Xbox Preservation",
    };
    GDOX_TEST_CHECK(gdox_preferences_save(&saved, &error));
    memset(&loaded, 0, sizeof(loaded));
    GDOX_TEST_CHECK(gdox_preferences_load(&loaded, &error));
    GDOX_TEST_CHECK(loaded.auto_start == saved.auto_start);
    GDOX_TEST_CHECK(
        loaded.internal_resolution_scale == saved.internal_resolution_scale
    );
    GDOX_TEST_CHECK(loaded.display_aspect == saved.display_aspect);
    GDOX_TEST_CHECK(loaded.display_fit == saved.display_fit);
    GDOX_TEST_CHECK(loaded.fullscreen == saved.fullscreen);
    GDOX_TEST_CHECK(loaded.window_width == saved.window_width);
    GDOX_TEST_CHECK(loaded.window_height == saved.window_height);
    GDOX_TEST_CHECK(
        strcmp(loaded.xemu_override, saved.xemu_override) == 0
    );
    GDOX_TEST_CHECK(
        strcmp(loaded.hdd_override, saved.hdd_override) == 0
    );
    GDOX_TEST_CHECK(
        strcmp(
            loaded.preservation_directory,
            saved.preservation_directory
        ) == 0
    );

    invalid = fopen(path, "wb");
    GDOX_TEST_CHECK(invalid != NULL);
    GDOX_TEST_CHECK(fputs("schema=1\nauto_start=maybe\n", invalid) >= 0);
    GDOX_TEST_CHECK(fclose(invalid) == 0);
    GDOX_TEST_CHECK(!gdox_preferences_load(&loaded, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(loaded.auto_start);
    GDOX_TEST_CHECK(loaded.internal_resolution_scale == 2U);

    (void)gdox_test_remove(path);
    (void)gdox_test_rmdir(preservation_path);
    (void)gdox_test_rmdir(directory);
    clear_settings_home();
}
