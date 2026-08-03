#ifndef GDOX_MEDIA_H
#define GDOX_MEDIA_H

#include "gdox/disc.h"
#include "gdox/error.h"
#include "gdox/live.h"
#include "gdox/x360.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_media_source {
    GDOX_MEDIA_PHYSICAL_DISC = 0,
    GDOX_MEDIA_DISC_IMAGE
} gdox_media_source;

typedef enum gdox_media_platform {
    GDOX_MEDIA_PLATFORM_NONE = 0,
    GDOX_MEDIA_PLATFORM_XBOX,
    GDOX_MEDIA_PLATFORM_XBOX_360
} gdox_media_platform;

typedef enum gdox_media_backend {
    GDOX_MEDIA_BACKEND_NONE = 0,
    GDOX_MEDIA_BACKEND_XEMU,
    GDOX_MEDIA_BACKEND_XENIA
} gdox_media_backend;

typedef enum gdox_media_image_layout {
    GDOX_MEDIA_IMAGE_NONE = 0,
    GDOX_MEDIA_IMAGE_PLAYABLE_XISO,
    GDOX_MEDIA_IMAGE_WHOLE_DISC
} gdox_media_image_layout;

typedef struct gdox_media_image_info {
    gdox_media_platform platform;
    gdox_media_backend backend;
    gdox_media_image_layout layout;
    uint64_t source_sectors;
    uint64_t game_partition_lba;
    gdox_live_disc_info disc;
    gdox_x360_disc_info x360;
} gdox_media_image_info;

/*
 * Opens and validates a read-only Xbox-family disc image. Original Xbox media
 * exposes its validated game partition directly; Xbox 360 media remains
 * byte-exact for Xenia.
 */
bool gdox_media_open_image(
    const char *path,
    gdox_random_disc *output,
    gdox_media_image_info *info,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
