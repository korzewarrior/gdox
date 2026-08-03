#ifndef GDOX_CORE_XDVDFS_DIRECTORY_CACHE_H
#define GDOX_CORE_XDVDFS_DIRECTORY_CACHE_H

#include "gdox/xdvdfs.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct gdox_xdvdfs_directory_cache gdox_xdvdfs_directory_cache;

bool gdox_xdvdfs_directory_cache_create(
    gdox_xdvdfs_directory_cache **cache,
    gdox_error *error
);

/*
 * Inspects XDVDFS while retaining, without rereading, a bounded set of the
 * sector-aligned directory buffers used by the traversal. The cache is
 * internal playback state and is independent of public XDVDFS metadata.
 */
bool gdox_xdvdfs_inspect_with_directory_cache(
    gdox_sector_source *source,
    const gdox_xdvdfs_volume *volume,
    gdox_xdvdfs_metadata *metadata,
    gdox_xdvdfs_directory_cache **cache,
    gdox_error *error
);

void gdox_xdvdfs_directory_cache_destroy(
    gdox_xdvdfs_directory_cache **cache
);

/* Used only by the XDVDFS traversal after a directory read succeeds. */
bool gdox_xdvdfs_directory_cache_retain(
    gdox_xdvdfs_directory_cache *cache,
    uint32_t start_sector,
    uint32_t blocks,
    uint8_t **bytes,
    gdox_error *error
);

void gdox_xdvdfs_directory_cache_finalize(
    gdox_xdvdfs_directory_cache *cache
);

/*
 * Moves both `partition` and `*cache` into a read-through cache adapter on
 * success. A failure leaves both inputs untouched.
 */
bool gdox_source_make_xdvdfs_directory_cache(
    gdox_sector_source *partition,
    gdox_xdvdfs_directory_cache **cache,
    gdox_sector_source *output,
    gdox_error *error
);

#endif
