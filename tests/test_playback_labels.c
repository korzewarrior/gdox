#include "test.h"

#include "ui/playback_labels.h"

#include <string.h>

static void check_labels(
    gdox_media_backend backend,
    const char *start,
    const char *restart,
    const char *close
)
{
    const gdox_playback_labels labels =
        gdox_playback_labels_for_backend(backend);

    GDOX_TEST_CHECK(strcmp(labels.start, start) == 0);
    GDOX_TEST_CHECK(strcmp(labels.restart, restart) == 0);
    GDOX_TEST_CHECK(strcmp(labels.close, close) == 0);
}

void gdox_test_playback_labels(void)
{
    static const char setup_message[] = "xemu setup is incomplete";
    gdox_playback_setup_notice setup;

    check_labels(
        GDOX_MEDIA_BACKEND_XEMU,
        "Start xemu",
        "Restart xemu",
        "Close xemu"
    );
    check_labels(
        GDOX_MEDIA_BACKEND_XENIA,
        "Start Xenia",
        "Restart Xenia",
        "Close Xenia"
    );
    check_labels(
        GDOX_MEDIA_BACKEND_NONE,
        "Start emulator",
        "Restart emulator",
        "Close emulator"
    );
    check_labels(
        (gdox_media_backend)99,
        "Start emulator",
        "Restart emulator",
        "Close emulator"
    );

    setup = gdox_playback_setup_for_media(
        GDOX_MEDIA_PLATFORM_XBOX,
        false,
        setup_message
    );
    GDOX_TEST_CHECK(
        setup.action == GDOX_PLAYBACK_SETUP_OPEN_SOURCES
    );
    GDOX_TEST_CHECK(setup.message == setup_message);
    setup = gdox_playback_setup_for_media(
        GDOX_MEDIA_PLATFORM_XBOX,
        true,
        setup_message
    );
    GDOX_TEST_CHECK(setup.action == GDOX_PLAYBACK_SETUP_NONE);
    setup = gdox_playback_setup_for_media(
        GDOX_MEDIA_PLATFORM_XBOX_360,
        false,
        setup_message
    );
    GDOX_TEST_CHECK(setup.action == GDOX_PLAYBACK_SETUP_NONE);
    setup = gdox_playback_setup_for_media(
        GDOX_MEDIA_PLATFORM_XBOX,
        false,
        ""
    );
    GDOX_TEST_CHECK(setup.action == GDOX_PLAYBACK_SETUP_NONE);

    GDOX_TEST_CHECK(
        strcmp(
            gdox_playback_attention_notice(true, "Launch failed"),
            "Launch failed"
        ) == 0
    );
    GDOX_TEST_CHECK(
        strcmp(
            gdox_playback_attention_notice(false, "Launch failed"),
            ""
        ) == 0
    );
    GDOX_TEST_CHECK(
        strcmp(gdox_playback_attention_notice(true, NULL), "") == 0
    );
}
