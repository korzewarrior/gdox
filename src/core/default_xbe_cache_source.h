#ifndef GDOX_CORE_DEFAULT_XBE_CACHE_SOURCE_H
#define GDOX_CORE_DEFAULT_XBE_CACHE_SOURCE_H

#include "gdox/xdvdfs.h"

#include <stdbool.h>

/*
 * Moves a game-partition source into a session-local adapter. A bounded
 * default.xbe is read and prepared before the source is published; all reads
 * outside that boot extent continue to use the inner source.
 */
bool gdox_source_make_default_xbe_cache(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    gdox_sector_source *output,
    gdox_error *error
);

#endif
