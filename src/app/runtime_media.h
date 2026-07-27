#ifndef GDOX_APP_RUNTIME_MEDIA_H
#define GDOX_APP_RUNTIME_MEDIA_H

#include "gdox/error.h"
#include "gdox/media.h"
#include "gdox/nbd.h"
#include "gdox/optical.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_runtime_media_info {
    gdox_media_source source;
    gdox_media_image_layout image_layout;
    uint64_t source_sectors;
    uint64_t game_partition_lba;
    char title[GDOX_LIVE_TITLE_CAPACITY];
} gdox_runtime_media_info;

bool gdox_runtime_media_open_physical(
    gdox_optical_drive drive,
    gdox_nbd_export **exported,
    gdox_runtime_media_info *info,
    gdox_error *error
);

bool gdox_runtime_media_open_image(
    const char *path,
    gdox_nbd_export **exported,
    gdox_runtime_media_info *info,
    gdox_error *error
);

const char *gdox_runtime_media_image_layout_name(
    gdox_media_image_layout layout
);

#endif
