#include "gdox/live.h"

#include "gdox/xdvdfs.h"

#include <stdlib.h>
#include <string.h>

static void copy_title(char output[GDOX_LIVE_TITLE_CAPACITY], const char *title)
{
    const char *source = title != NULL ? title : "Original Xbox disc";
    size_t length = strlen(source);

    if (length >= GDOX_LIVE_TITLE_CAPACITY) {
        length = GDOX_LIVE_TITLE_CAPACITY - 1U;
    }
    memcpy(output, source, length);
    output[length] = '\0';
}

static bool close_source(gdox_sector_source *source, gdox_error *error)
{
    return !gdox_source_is_valid(source) || gdox_source_close(source, error);
}

bool gdox_live_disc_identify(
    gdox_sector_source *whole_source,
    gdox_live_disc_info *info,
    gdox_error *error
)
{
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_metadata metadata;
    bool success;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(whole_source) || info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open source and disc information are required"
        );
        return false;
    }

    memset(info, 0, sizeof(*info));
    memset(&metadata, 0, sizeof(metadata));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    success = gdox_xdvdfs_find_volume(whole_source, &volume, error)
        && gdox_xdvdfs_inspect(whole_source, &volume, &metadata, error);
    if (success) {
        copy_title(info->title, metadata.title);
        info->title_id_present = metadata.title_id_present;
        info->title_id = metadata.title_id;
    }
    gdox_xdvdfs_metadata_destroy(&metadata);
    return success;
}

bool gdox_live_disc_build(
    gdox_sector_source *whole_source,
    gdox_random_disc *output,
    gdox_live_disc_info *info,
    gdox_error *error
)
{
    gdox_sector_source whole = {0};
    gdox_sector_source partition = {0};
    gdox_sector_source patched = {0};
    gdox_sector_source compact = {0};
    gdox_random_disc disc = {0};
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_volume partition_volume;
    gdox_xdvdfs_metadata metadata;
    gdox_xdvdfs_compact_stats compact_stats;
    gdox_byte_patch *patches = NULL;
    size_t patch_count = 0U;
    bool success = false;
    gdox_error cleanup_error;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(whole_source) || output == NULL
        || gdox_disc_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open source and empty disc output are required"
        );
        return false;
    }

    memset(&metadata, 0, sizeof(metadata));
    memset(&compact_stats, 0, sizeof(compact_stats));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    whole = *whole_source;
    whole_source->context = NULL;
    whole_source->ops = NULL;

    if (!gdox_xdvdfs_find_volume(&whole, &volume, error)
        || !gdox_xdvdfs_inspect(&whole, &volume, &metadata, error)
        || !gdox_xdvdfs_collect_media_patches(
            &whole,
            &metadata,
            &patches,
            &patch_count,
            error
        )
        || !gdox_source_make_partition(
            &whole,
            volume.base_lba,
            &partition,
            error
        )
        || !gdox_source_make_patched(
            &partition,
            patches,
            patch_count,
            &patched,
            error
        )) {
        goto cleanup;
    }

    partition_volume = volume;
    partition_volume.base_lba = 0U;
    if (!gdox_source_make_compact_xiso(
            &patched,
            &partition_volume,
            &compact,
            &compact_stats,
            error
        )
        || !gdox_disc_from_source(&compact, &disc, error)) {
        goto cleanup;
    }

    if (info != NULL) {
        copy_title(info->title, metadata.title);
        info->title_id_present = metadata.title_id_present;
        info->title_id = metadata.title_id;
        info->input_sectors = compact_stats.input_sectors;
        info->output_sectors = compact_stats.output_sectors;
    }
    *output = disc;
    disc.context = NULL;
    disc.ops = NULL;
    success = true;

cleanup:
    free(patches);
    gdox_xdvdfs_metadata_destroy(&metadata);
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
    }
    if (!close_source(&compact, &cleanup_error)
        || !close_source(&patched, &cleanup_error)
        || !close_source(&partition, &cleanup_error)
        || !close_source(&whole, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
    }
    return success;
}
