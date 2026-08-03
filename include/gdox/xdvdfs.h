#ifndef GDOX_XDVDFS_H
#define GDOX_XDVDFS_H

#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR UINT64_C(32)
#define GDOX_XDVDFS_COMMON_GAME_BASE_LBA UINT64_C(198144)
#define GDOX_XDVDFS_NO_ENTRY SIZE_MAX

typedef struct gdox_xdvdfs_volume {
    uint64_t base_lba;
    uint32_t root_directory_sector;
    uint32_t root_directory_size;
    uint64_t image_timestamp;
} gdox_xdvdfs_volume;

typedef struct gdox_xdvdfs_entry {
    char *name;
    char *path;
    uint32_t start_sector;
    uint32_t size;
    uint8_t attributes;
} gdox_xdvdfs_entry;

typedef struct gdox_xdvdfs_file_extent {
    uint32_t start_sector;
    uint32_t sector_count;
    uint64_t prefix_max_end;
} gdox_xdvdfs_file_extent;

typedef struct gdox_xdvdfs_metadata {
    gdox_xdvdfs_volume volume;
    char *title;
    bool title_id_present;
    uint32_t title_id;
    gdox_xdvdfs_entry *xbe_files;
    size_t xbe_file_count;
    size_t default_xbe_index;
    gdox_xdvdfs_file_extent *file_extents;
    size_t file_extent_count;
} gdox_xdvdfs_metadata;

bool gdox_xdvdfs_find_volume(
    gdox_sector_source *source,
    gdox_xdvdfs_volume *volume,
    gdox_error *error
);
bool gdox_xdvdfs_inspect(
    gdox_sector_source *source,
    const gdox_xdvdfs_volume *volume,
    gdox_xdvdfs_metadata *metadata,
    gdox_error *error
);
void gdox_xdvdfs_metadata_destroy(gdox_xdvdfs_metadata *metadata);

/*
 * Traverses every reachable directory and validates every referenced extent.
 * Returns the 32-sector-aligned prefix needed to retain the complete
 * filesystem while removing only unreferenced trailing mastering padding.
 */
bool gdox_xdvdfs_measure_trimmed_sectors(
    gdox_sector_source *partition,
    const gdox_xdvdfs_volume *volume,
    uint64_t *sectors,
    gdox_error *error
);

/*
 * Locate conventional XBE media-check branches. Returned offsets are relative
 * to the game partition, so they can be passed directly to a patched partition
 * source. The caller owns `*patches` and releases it with free().
 */
bool gdox_xdvdfs_collect_media_patches(
    gdox_sector_source *whole_disc,
    const gdox_xdvdfs_metadata *metadata,
    gdox_byte_patch **patches,
    size_t *patch_count,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
