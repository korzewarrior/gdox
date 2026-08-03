#include "gdox/live.h"

#include "core/default_xbe_cache_source.h"
#include "core/file_readahead_source.h"
#include "core/xbe_patch_source.h"
#include "core/xdvdfs_directory_cache.h"
#include "gdox/xdvdfs.h"

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

static void move_source_if_valid(
    gdox_sector_source *source,
    gdox_sector_source *output
)
{
    if (gdox_source_is_valid(source) && !gdox_source_is_valid(output)) {
        *output = *source;
        source->context = NULL;
        source->ops = NULL;
    }
}

static bool require_playable_default_xbe(
    const gdox_xdvdfs_metadata *metadata,
    gdox_error *error
)
{
    if (metadata->default_xbe_index == GDOX_XDVDFS_NO_ENTRY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "Original Xbox media has no root default.xbe"
        );
        return false;
    }
    if (!metadata->title_id_present || metadata->title_id == 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "Original Xbox default.xbe has no valid executable identity"
        );
        return false;
    }
    return true;
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

bool gdox_live_disc_build_configured(
    gdox_sector_source *whole_source,
    const gdox_live_disc_options *options,
    gdox_random_disc *output,
    gdox_live_disc_info *info,
    gdox_error *error
)
{
    gdox_sector_source partition = {0};
    gdox_sector_source directory_cached = {0};
    gdox_sector_source prepared = {0};
    gdox_sector_source streamed = {0};
    gdox_sector_source compatible = {0};
    gdox_random_disc disc = {0};
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_metadata metadata;
    gdox_xdvdfs_directory_cache *directory_cache = NULL;
    uint64_t partition_sectors = 0U;
    bool success = false;
    bool cleanup_failed = false;
    gdox_error cleanup_error;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(whole_source) || options == NULL
        || output == NULL
        || gdox_disc_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open source, live-disc options, and empty disc output are required"
        );
        return false;
    }
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
    }

    memset(&metadata, 0, sizeof(metadata));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    if (!gdox_xdvdfs_find_volume(whole_source, &volume, error)
        || !gdox_xdvdfs_inspect_with_directory_cache(
            whole_source,
            &volume,
            &metadata,
            &directory_cache,
            error
        )
        || !require_playable_default_xbe(&metadata, error)
        || !gdox_source_make_partition(
            whole_source,
            volume.base_lba,
            &partition,
            error
        )
        || !gdox_source_make_xdvdfs_directory_cache(
            &partition,
            &directory_cache,
            &directory_cached,
            error
        )
        || !gdox_source_make_default_xbe_cache(
            &directory_cached,
            &metadata,
            &prepared,
            error
        )
        || !gdox_source_make_file_readahead(
            &prepared,
            &metadata,
            options->sequential_read_blocks,
            &streamed,
            error
        )
        || !gdox_source_make_xbe_patch_source(
            &streamed,
            &metadata,
            &compatible,
            error
        )) {
        goto cleanup;
    }
    partition_sectors = gdox_source_sector_count(&compatible);
    if (!gdox_disc_from_source(&compatible, &disc, error)) {
        goto cleanup;
    }

    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        copy_title(info->title, metadata.title);
        info->title_id_present = metadata.title_id_present;
        info->title_id = metadata.title_id;
        info->game_partition_lba = volume.base_lba;
        info->input_sectors = partition_sectors;
        info->output_sectors = partition_sectors;
    }
    *output = disc;
    disc.context = NULL;
    disc.ops = NULL;
    success = true;

cleanup:
    gdox_xdvdfs_metadata_destroy(&metadata);
    gdox_xdvdfs_directory_cache_destroy(&directory_cache);
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
        cleanup_failed = true;
        if (gdox_disc_is_valid(&disc)) {
            *output = disc;
            disc.context = NULL;
            disc.ops = NULL;
        }
    }
    if (!close_source(&compatible, &cleanup_error)
        || !close_source(&streamed, &cleanup_error)
        || !close_source(&prepared, &cleanup_error)
        || !close_source(&directory_cached, &cleanup_error)
        || !close_source(&partition, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
        cleanup_failed = true;
    }
    if (cleanup_failed && !gdox_disc_is_valid(output)) {
        move_source_if_valid(&compatible, whole_source);
        move_source_if_valid(&streamed, whole_source);
        move_source_if_valid(&prepared, whole_source);
        move_source_if_valid(&directory_cached, whole_source);
        move_source_if_valid(&partition, whole_source);
    }
    return success;
}

bool gdox_live_disc_build(
    gdox_sector_source *whole_source,
    gdox_random_disc *output,
    gdox_live_disc_info *info,
    gdox_error *error
)
{
    const gdox_live_disc_options options = {0};

    return gdox_live_disc_build_configured(
        whole_source,
        &options,
        output,
        info,
        error
    );
}
