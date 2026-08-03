#ifndef GDOX_CORE_FILE_READAHEAD_SOURCE_H
#define GDOX_CORE_FILE_READAHEAD_SOURCE_H

#include "gdox/xdvdfs.h"

#include <stdbool.h>
#include <stdint.h>

#define GDOX_FILE_READAHEAD_MAX_WINDOW_BLOCKS UINT32_C(512)

/*
 * Moves a game-partition source into a session-local read-ahead adapter.
 * Speculative reads are limited to validated non-directory file extents.
 */
bool gdox_source_make_file_readahead(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    uint32_t max_window_blocks,
    gdox_sector_source *output,
    gdox_error *error
);

#endif
