#ifndef GDOX_UI_PLAYBACK_LABELS_H
#define GDOX_UI_PLAYBACK_LABELS_H

#include "gdox/media.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_playback_labels {
    const char *start;
    const char *restart;
    const char *close;
} gdox_playback_labels;

typedef enum gdox_playback_setup_action {
    GDOX_PLAYBACK_SETUP_NONE = 0,
    GDOX_PLAYBACK_SETUP_OPEN_SOURCES
} gdox_playback_setup_action;

typedef struct gdox_playback_setup_notice {
    const char *message;
    gdox_playback_setup_action action;
} gdox_playback_setup_notice;

gdox_playback_labels gdox_playback_labels_for_backend(
    gdox_media_backend backend
);
gdox_playback_setup_notice gdox_playback_setup_for_media(
    gdox_media_platform platform,
    bool xemu_ready,
    const char *xemu_setup
);
const char *gdox_playback_attention_notice(
    bool attention,
    const char *notice
);

#ifdef __cplusplus
}
#endif

#endif
