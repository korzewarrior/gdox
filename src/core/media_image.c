#include "gdox/media.h"

#include "gdox/source.h"
#include "gdox/x360.h"

#include <string.h>

bool gdox_media_open_image(
    const char *path,
    gdox_random_disc *output,
    gdox_media_image_info *info,
    gdox_error *error
)
{
    gdox_sector_source source = {0};
    gdox_live_disc_info disc_info = {0};
    gdox_x360_disc_info x360_info = {0};
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
    if (gdox_live_disc_build(&source, output, &disc_info, error)) {
        image_info.platform = GDOX_MEDIA_PLATFORM_XBOX;
        image_info.backend = GDOX_MEDIA_BACKEND_XEMU;
        image_info.game_partition_lba = disc_info.game_partition_lba;
        image_info.layout = disc_info.game_partition_lba == 0U
            ? GDOX_MEDIA_IMAGE_PLAYABLE_XISO
            : GDOX_MEDIA_IMAGE_WHOLE_DISC;
        image_info.disc = disc_info;
    } else {
        if (error == NULL || error->code != GDOX_ERROR_INVALID_VOLUME) {
            if (gdox_source_is_valid(&source)
                && !gdox_source_close(&source, &cleanup_error)) {
                if (error != NULL) {
                    *error = cleanup_error;
                }
            }
            return false;
        }
        gdox_error_clear(error);
        if (!gdox_x360_live_disc_build(
            &source, output, &x360_info, error
            )) {
            if (gdox_source_is_valid(&source)
                && !gdox_source_close(&source, &cleanup_error)) {
                if (error != NULL) {
                    *error = cleanup_error;
                }
            }
            return false;
        }
        image_info.platform = GDOX_MEDIA_PLATFORM_XBOX_360;
        image_info.backend = GDOX_MEDIA_BACKEND_XENIA;
        image_info.x360 = x360_info;
    }
    *info = image_info;
    return true;
}
