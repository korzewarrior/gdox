#include "ui/playback_labels.h"

gdox_playback_labels gdox_playback_labels_for_backend(
    gdox_media_backend backend
)
{
    switch (backend) {
        case GDOX_MEDIA_BACKEND_XEMU:
            return (gdox_playback_labels){
                "Start xemu",
                "Restart xemu",
                "Close xemu",
            };
        case GDOX_MEDIA_BACKEND_XENIA:
            return (gdox_playback_labels){
                "Start Xenia",
                "Restart Xenia",
                "Close Xenia",
            };
        case GDOX_MEDIA_BACKEND_NONE:
            break;
    }
    return (gdox_playback_labels){
        "Start emulator",
        "Restart emulator",
        "Close emulator",
    };
}

gdox_playback_setup_notice gdox_playback_setup_for_media(
    gdox_media_platform platform,
    bool xemu_ready,
    const char *xemu_setup
)
{
    if (platform == GDOX_MEDIA_PLATFORM_XBOX && !xemu_ready
        && xemu_setup != NULL && xemu_setup[0] != '\0') {
        return (gdox_playback_setup_notice){
            xemu_setup,
            GDOX_PLAYBACK_SETUP_OPEN_SOURCES,
        };
    }
    return (gdox_playback_setup_notice){
        "",
        GDOX_PLAYBACK_SETUP_NONE,
    };
}

const char *gdox_playback_attention_notice(
    bool attention,
    const char *notice
)
{
    return attention && notice != NULL ? notice : "";
}
