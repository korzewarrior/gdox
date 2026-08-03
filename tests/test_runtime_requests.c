#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "app/runtime_internal.h"

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
static bool set_config_home(const char *path)
{
    return SetEnvironmentVariableA("GDOX_CONFIG_HOME", path) != 0;
}
static void clear_config_home(void)
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
static bool set_config_home(const char *path)
{
    return setenv("GDOX_CONFIG_HOME", path, 1) == 0;
}
static void clear_config_home(void)
{
    (void)unsetenv("GDOX_CONFIG_HOME");
}
#endif

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            (void)fprintf(                                                     \
                stderr,                                                        \
                "%s:%d: check failed: %s\n",                                   \
                __FILE__,                                                      \
                __LINE__,                                                      \
                #expression                                                    \
            );                                                                 \
            return 1;                                                          \
        }                                                                      \
    } while (false)

static int check_submission(gdox_runtime *runtime)
{
    gdox_runtime_request_entry queued;

    gdox_runtime_request(runtime, GDOX_RUNTIME_START);
    gdox_runtime_request(runtime, GDOX_RUNTIME_USE_PHYSICAL_DISC);
    CHECK(gdox_runtime_request_dequeue(&runtime->requests, &queued));
    CHECK(queued.kind == GDOX_RUNTIME_REQUEST_START);
    CHECK(gdox_runtime_request_dequeue(&runtime->requests, &queued));
    CHECK(queued.kind == GDOX_RUNTIME_REQUEST_USE_PHYSICAL);

    CHECK(gdox_runtime_open_disc_image(runtime, "queued.iso"));
    CHECK(gdox_runtime_request_dequeue(&runtime->requests, &queued));
    CHECK(queued.kind == GDOX_RUNTIME_REQUEST_OPEN_IMAGE);
    CHECK(strcmp(queued.path, "queued.iso") == 0);

    runtime->snapshot.can_preserve = true;
    CHECK(gdox_runtime_begin_preservation(
        runtime, GDOX_PRESERVATION_REDUMP, "preserved.iso", true
    ));
    CHECK(gdox_runtime_request_dequeue(&runtime->requests, &queued));
    CHECK(queued.kind == GDOX_RUNTIME_REQUEST_PRESERVE);
    CHECK(strcmp(queued.path, "preserved.iso") == 0);
    CHECK(queued.preservation_verify);
    return 0;
}

static int check_backpressure(gdox_runtime *runtime)
{
    size_t index;

    memset(&runtime->requests, 0, sizeof(runtime->requests));
    for (index = 0U; index < GDOX_RUNTIME_REQUEST_CAPACITY; ++index) {
        gdox_runtime_request(
            runtime, index % 2U == 0U ? GDOX_RUNTIME_START : GDOX_RUNTIME_CLOSE
        );
    }
    runtime->snapshot.notice[0] = '\0';
    gdox_runtime_request(runtime, GDOX_RUNTIME_EJECT);
    CHECK(
        strcmp(
            runtime->snapshot.notice, "Runtime is busy; try that action again"
        )
        == 0
    );
    CHECK(runtime->requests.count == GDOX_RUNTIME_REQUEST_CAPACITY);
    return 0;
}

static int check_settings_survive_publish(gdox_runtime *runtime)
{
    gdox_runtime_snapshot worker_snapshot = {0};
    gdox_preferences expected;

    gdox_preferences_defaults(&expected);
    expected.auto_start = false;
    expected.internal_resolution_scale = 4U;
    expected.display_aspect = GDOX_EMULATOR_ASPECT_NATIVE;
    expected.display_fit = GDOX_EMULATOR_FIT_CENTER;
    expected.fullscreen = false;
    expected.window_width = 1920U;
    expected.window_height = 1080U;
    memcpy(expected.xemu_override, "/xemu", sizeof "/xemu");
    memcpy(
        expected.preservation_directory,
        "/preserved",
        sizeof "/preserved"
    );
    runtime->snapshot.settings = expected;

    worker_snapshot.settings.auto_start = true;
    worker_snapshot.phase = GDOX_RUNTIME_READY;
    gdox_runtime_publish(runtime, &worker_snapshot);

    CHECK(runtime->snapshot.phase == GDOX_RUNTIME_READY);
    CHECK(runtime->snapshot.settings.auto_start == expected.auto_start);
    CHECK(
        runtime->snapshot.settings.internal_resolution_scale
        == expected.internal_resolution_scale
    );
    CHECK(runtime->snapshot.settings.display_aspect == expected.display_aspect);
    CHECK(runtime->snapshot.settings.display_fit == expected.display_fit);
    CHECK(runtime->snapshot.settings.fullscreen == expected.fullscreen);
    CHECK(runtime->snapshot.settings.window_width == expected.window_width);
    CHECK(runtime->snapshot.settings.window_height == expected.window_height);
    CHECK(strcmp(runtime->snapshot.settings.xemu_override, "/xemu") == 0);
    CHECK(
        strcmp(runtime->snapshot.settings.preservation_directory, "/preserved")
        == 0
    );
    return 0;
}

static int check_handheld_display_policy(gdox_runtime *runtime)
{
    char directory[256];
    char settings_path[320];
    gdox_preferences loaded;
    gdox_runtime_request_entry queued;
    gdox_error error;

    (void)snprintf(
        directory,
        sizeof(directory),
        "./gdox-runtime-settings-%d-%lld",
        gdox_test_getpid(),
        (long long)time(NULL)
    );
    (void)snprintf(
        settings_path,
        sizeof(settings_path),
        "%s/settings.conf",
        directory
    );
    CHECK(gdox_test_mkdir(directory) == 0);
    CHECK(set_config_home(directory));
    memset(&runtime->requests, 0, sizeof(runtime->requests));
    gdox_preferences_defaults(&runtime->snapshot.settings);
    runtime->host_profile = GDOX_HOST_PROFILE_HANDHELD;

    gdox_runtime_set_display(
        runtime,
        4U,
        GDOX_EMULATOR_ASPECT_WIDESCREEN,
        GDOX_EMULATOR_FIT_SCALE,
        true,
        1280U,
        720U
    );

    CHECK(runtime->snapshot.settings.internal_resolution_scale == 1U);
    CHECK(gdox_runtime_request_dequeue(&runtime->requests, &queued));
    CHECK(queued.kind == GDOX_RUNTIME_REQUEST_APPLY_DISPLAY);
    CHECK(gdox_preferences_load(&loaded, &error));
    CHECK(loaded.internal_resolution_scale == 1U);
    clear_config_home();
    CHECK(gdox_test_remove(settings_path) == 0);
    CHECK(gdox_test_rmdir(directory) == 0);
    return 0;
}

int main(void)
{
    gdox_runtime runtime = {0};
    int result;

    CHECK(gdox_mutex_init(&runtime.mutex));
    atomic_init(&runtime.preservation_cancelled, false);
    result = check_submission(&runtime);
    if (result == 0) {
        result = check_backpressure(&runtime);
    }
    if (result == 0) {
        result = check_settings_survive_publish(&runtime);
    }
    if (result == 0) {
        result = check_handheld_display_policy(&runtime);
    }
    gdox_mutex_destroy(&runtime.mutex);
    if (result == 0) {
        (void)puts("GDOX runtime request tests passed");
    }
    return result;
}
