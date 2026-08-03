#ifndef GDOX_APP_RUNTIME_MEDIA_H
#define GDOX_APP_RUNTIME_MEDIA_H

#include "gdox/error.h"
#include "gdox/emulator.h"
#include "gdox/media.h"
#include "gdox/nbd.h"
#include "gdox/optical.h"
#include "gdox/xenia.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_runtime_media_info {
    gdox_media_source source;
    gdox_media_platform platform;
    gdox_media_backend backend;
    gdox_media_image_layout image_layout;
    uint64_t source_sectors;
    uint64_t game_partition_lba;
    gdox_x360_disc_info x360;
    const gdox_xenia_launch_policy *xenia_policy;
    gdox_x360_executable_kind xenia_module_kind;
    gdox_x360_execution_info xenia_module_execution;
    char title[GDOX_LIVE_TITLE_CAPACITY];
} gdox_runtime_media_info;

typedef struct gdox_runtime_media_session {
    bool open;
    gdox_nbd_export *exported;
    gdox_random_disc validated_disc;
    gdox_sector_source retained_source;
    gdox_runtime_media_info info;
    char image_path[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_runtime_media_session;

typedef enum gdox_runtime_media_open_state {
    GDOX_RUNTIME_MEDIA_UNIDENTIFIED = 0,
    GDOX_RUNTIME_MEDIA_IDENTIFIED,
    GDOX_RUNTIME_MEDIA_READY,
} gdox_runtime_media_open_state;

/*
 * Recognition is reported separately from session readiness. A failed open
 * can identify media while the session retains cleanup ownership.
 */
typedef struct gdox_runtime_media_open_result {
    gdox_runtime_media_open_state state;
    gdox_runtime_media_info info;
} gdox_runtime_media_open_result;

bool gdox_runtime_media_open_physical(
    gdox_optical_drive drive,
    gdox_runtime_media_session *session,
    gdox_runtime_media_open_result *result,
    gdox_error *error
);

bool gdox_runtime_media_open_image(
    const char *path,
    gdox_runtime_media_session *session,
    gdox_runtime_media_open_result *result,
    gdox_error *error
);

bool gdox_runtime_media_close(
    gdox_runtime_media_session *session,
    gdox_error *error
);

bool gdox_runtime_media_is_owned(
    const gdox_runtime_media_session *session
);

bool gdox_runtime_media_retain_cleanup_source(
    gdox_runtime_media_session *session,
    gdox_sector_source *source,
    gdox_error *error
);

bool gdox_runtime_media_prepare_xenia_target(
    gdox_runtime_media_session *session,
    gdox_xenia_target *target,
    gdox_error *error
);

const char *gdox_runtime_media_layout_name(
    const gdox_runtime_media_info *info
);

#endif
