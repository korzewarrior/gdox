#include "app/runtime_media.h"

#include "gdox/disc.h"
#include "gdox/live.h"
#include "gdox/optical.h"
#include "gdox/source.h"

#include <stdio.h>

static bool start_export(
    gdox_random_disc *disc,
    gdox_nbd_export **exported,
    gdox_error *error
)
{
    if (!gdox_nbd_start(disc, exported, error)) {
        return false;
    }
    return true;
}

bool gdox_runtime_media_open_physical(
    gdox_optical_drive drive,
    gdox_nbd_export **exported,
    gdox_runtime_media_info *info,
    gdox_error *error
)
{
    gdox_sector_source whole = {0};
    gdox_random_disc disc = {0};
    gdox_live_disc_info live_info;
    gdox_runtime_media_info media_info = {0};
    gdox_error cleanup_error;
    bool success = false;

    gdox_error_clear(error);
    if (exported == NULL || *exported != NULL || info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty export and media information are required"
        );
        return false;
    }
    if (!gdox_optical_open(drive, 3U, 20000U, &whole, error)
        || !gdox_live_disc_build(&whole, &disc, &live_info, error)
        || !start_export(&disc, exported, error)) {
        goto cleanup;
    }
    media_info.source = GDOX_MEDIA_PHYSICAL_DISC;
    media_info.source_sectors = live_info.input_sectors;
    (void)snprintf(
        media_info.title,
        sizeof(media_info.title),
        "%s",
        live_info.title
    );
    *info = media_info;
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
    }
    if (gdox_source_is_valid(&whole)
        && !gdox_source_close(&whole, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
    }
    return success;
}

bool gdox_runtime_media_open_image(
    const char *path,
    gdox_nbd_export **exported,
    gdox_runtime_media_info *info,
    gdox_error *error
)
{
    gdox_random_disc disc = {0};
    gdox_media_image_info image_info;
    gdox_runtime_media_info media_info = {0};
    gdox_error cleanup_error;
    bool success = false;

    gdox_error_clear(error);
    if (exported == NULL || *exported != NULL || info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty export and media information are required"
        );
        return false;
    }
    if (!gdox_media_open_image(path, &disc, &image_info, error)
        || !start_export(&disc, exported, error)) {
        goto cleanup;
    }
    media_info.source = GDOX_MEDIA_DISC_IMAGE;
    media_info.image_layout = image_info.layout;
    media_info.source_sectors = image_info.source_sectors;
    media_info.game_partition_lba = image_info.game_partition_lba;
    (void)snprintf(
        media_info.title,
        sizeof(media_info.title),
        "%s",
        image_info.disc.title
    );
    *info = media_info;
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
    }
    return success;
}

const char *gdox_runtime_media_image_layout_name(
    gdox_media_image_layout layout
)
{
    switch (layout) {
        case GDOX_MEDIA_IMAGE_PLAYABLE_XISO:
            return "Playable XISO";
        case GDOX_MEDIA_IMAGE_WHOLE_DISC:
            return "Full-disc image";
        case GDOX_MEDIA_IMAGE_NONE:
            break;
    }
    return "Disc image";
}
