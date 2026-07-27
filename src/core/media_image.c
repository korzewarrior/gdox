#include "gdox/media.h"

#include "gdox/source.h"
#include "gdox/xdvdfs.h"

#include <string.h>

bool gdox_media_open_image(
    const char *path,
    gdox_random_disc *output,
    gdox_media_image_info *info,
    gdox_error *error
)
{
    gdox_sector_source source = {0};
    gdox_xdvdfs_volume volume;
    gdox_live_disc_info disc_info;
    gdox_media_image_info image_info;
    gdox_error cleanup_error;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || output == NULL
        || gdox_disc_is_valid(output) || info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an image path, empty disc output, and image information are required"
        );
        return false;
    }
    memset(&image_info, 0, sizeof(image_info));
    if (!gdox_source_open_file(path, &source, error)) {
        return false;
    }
    image_info.source_sectors = gdox_source_sector_count(&source);
    if (!gdox_xdvdfs_find_volume(&source, &volume, error)) {
        (void)gdox_source_close(&source, &cleanup_error);
        return false;
    }
    image_info.game_partition_lba = volume.base_lba;
    image_info.layout = volume.base_lba == 0U
        ? GDOX_MEDIA_IMAGE_PLAYABLE_XISO
        : GDOX_MEDIA_IMAGE_WHOLE_DISC;
    if (!gdox_live_disc_build(&source, output, &disc_info, error)) {
        return false;
    }
    image_info.disc = disc_info;
    *info = image_info;
    return true;
}
