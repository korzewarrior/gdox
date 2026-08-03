#ifndef GDOX_CORE_COMPACT_H
#define GDOX_CORE_COMPACT_H

#include "gdox/xdvdfs.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct gdox_xdvdfs_compact_stats {
    uint64_t input_sectors;
    uint64_t output_sectors;
    uint64_t file_count;
    uint64_t directory_count;
} gdox_xdvdfs_compact_stats;

/*
 * Moves `partition` into a seekable, compact virtual XISO.
 * Directory tables retain their original tree and names while file contents
 * remain streamed from the inner source. `output` must be empty; failures
 * leave `partition` untouched.
 */
bool gdox_source_make_compact_xiso(
    gdox_sector_source *partition,
    const gdox_xdvdfs_volume *volume,
    gdox_sector_source *output,
    gdox_xdvdfs_compact_stats *stats,
    gdox_error *error
);

#endif
