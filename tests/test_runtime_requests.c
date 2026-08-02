#include "app/runtime_internal.h"

#include <stdio.h>
#include <string.h>

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
    memcpy(expected.hdd_override, "/xbox_hdd.qcow2", sizeof "/xbox_hdd.qcow2");
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
        strcmp(runtime->snapshot.settings.hdd_override, "/xbox_hdd.qcow2") == 0
    );
    CHECK(
        strcmp(runtime->snapshot.settings.preservation_directory, "/preserved")
        == 0
    );
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
    gdox_mutex_destroy(&runtime.mutex);
    if (result == 0) {
        (void)puts("GDOX runtime request tests passed");
    }
    return result;
}
