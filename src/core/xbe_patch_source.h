#ifndef GDOX_CORE_XBE_PATCH_SOURCE_H
#define GDOX_CORE_XBE_PATCH_SOURCE_H

#include "gdox/xdvdfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Applies the complete compatibility transform to one in-memory XBE. */
bool gdox_xbe_patch_complete_file(uint8_t *bytes, size_t length);

/*
 * Moves a game-partition source into a compatibility adapter. Each read is
 * patched from only its returned bytes plus at most seven preceding bytes.
 * XBE headers are validated lazily and cached; file contents and patch lists
 * are never cached.
 */
bool gdox_source_make_xbe_patch_source(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    gdox_sector_source *output,
    gdox_error *error
);

#endif
