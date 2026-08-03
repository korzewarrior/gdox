#ifndef GDOX_LIVE_H
#define GDOX_LIVE_H

#include "gdox/disc.h"
#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_LIVE_TITLE_CAPACITY 256U

typedef struct gdox_live_disc_info {
    char title[GDOX_LIVE_TITLE_CAPACITY];
    bool title_id_present;
    uint32_t title_id;
    uint64_t game_partition_lba;
    uint64_t input_sectors;
    uint64_t output_sectors;
} gdox_live_disc_info;

typedef struct gdox_live_disc_options {
    /* Zero keeps reads exact. Nonzero enables file-bounded sequential reads. */
    uint32_t sequential_read_blocks;
} gdox_live_disc_options;

/*
 * Reads the disc title and title ID without consuming `whole_source`.
 * The layout fields are zero because no emulator view is built.
 */
bool gdox_live_disc_identify(
    gdox_sector_source *whole_source,
    gdox_live_disc_info *info,
    gdox_error *error
);

/*
 * Builds the read-only game-partition view consumed by an emulator. The
 * default XBE is prepared once in a bounded session cache; compatibility
 * changes for other validated XBEs are applied only to requested bytes. File
 * contents remain backed by `whole_source`, and no game data is copied to
 * persistent storage.
 *
 * On success, `output` owns the resulting disc. On failure, cleanup is
 * complete unless `whole_source` or `output` remains valid; a valid handle is
 * retained solely so the caller can retry close.
 */
bool gdox_live_disc_build(
    gdox_sector_source *whole_source,
    gdox_random_disc *output,
    gdox_live_disc_info *info,
    gdox_error *error
);

bool gdox_live_disc_build_configured(
    gdox_sector_source *whole_source,
    const gdox_live_disc_options *options,
    gdox_random_disc *output,
    gdox_live_disc_info *info,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
