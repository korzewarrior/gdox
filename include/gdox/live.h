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
    uint64_t input_sectors;
    uint64_t output_sectors;
} gdox_live_disc_info;

/*
 * Reads the disc title and title ID without consuming `whole_source`.
 * The sector counts are zero because no compact emulator view is built.
 */
bool gdox_live_disc_identify(
    gdox_sector_source *whole_source,
    gdox_live_disc_info *info,
    gdox_error *error
);

/*
 * Builds the read-only XISO view consumed by an emulator. File contents remain
 * backed by `whole_source`; no game data is copied to persistent storage.
 *
 * This function consumes `whole_source` on both success and failure. On
 * success, `output` owns the resulting disc and must be closed by the caller.
 */
bool gdox_live_disc_build(
    gdox_sector_source *whole_source,
    gdox_random_disc *output,
    gdox_live_disc_info *info,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
