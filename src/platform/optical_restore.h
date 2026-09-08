#ifndef GDOX_OPTICAL_RESTORE_H
#define GDOX_OPTICAL_RESTORE_H

#include "gdox/sector.h"
#include "platform/mmc_commands.h"

#include <string.h>

/* Only an explicit capacity-command NO MEDIUM response permits restoration
 * from a complete volatile-memory snapshot without standard media geometry. */
static inline bool gdox_optical_restore_no_medium(const gdox_scsi_transport *transport)
{
    uint8_t sense[32] = {0};
    size_t bytes = 0U;
    uint8_t key;
    uint8_t asc;
    if (!gdox_scsi_transport_last_sense(transport, sense, sizeof(sense), &bytes)) {
        return false;
    }
    switch (sense[0] & 0x7fU) {
        case 0x70U:
        case 0x71U:
            if (bytes < 13U) return false;
            key = (uint8_t)(sense[2] & 0x0fU);
            asc = sense[12];
            break;
        case 0x72U:
        case 0x73U:
            if (bytes < 3U) return false;
            key = (uint8_t)(sense[1] & 0x0fU);
            asc = sense[2];
            break;
        default:
            return false;
    }
    return key == 0x02U && asc == 0x3aU;
}

static inline bool gdox_optical_restore_read_capacity(
    gdox_scsi_transport *transport, uint32_t timeout_ms, uint32_t stock_last_lba,
    uint32_t *last_lba, uint32_t *block_size, bool *no_medium, gdox_error *error)
{
    *no_medium = false;
    if (!gdox_mmc_read_capacity_10(transport, timeout_ms,
            last_lba, block_size, error)) {
        if (!gdox_optical_restore_no_medium(transport)) return false;
        /* These values are only classification placeholders. The caller must
         * have read every volatile field successfully before reaching here. */
        *last_lba = stock_last_lba;
        *block_size = GDOX_LOGICAL_SECTOR_BYTES;
        *no_medium = true;
        gdox_error_clear(error);
        return true;
    }
    if (*block_size != GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE,
            "refusing optical restoration with an unknown logical block size");
        return false;
    }
    return true;
}

static inline bool gdox_optical_restore_check_geometry(
    gdox_scsi_transport *transport, const uint8_t expected[3],
    bool no_medium, gdox_error *error)
{
    uint8_t pfi[2052];
    size_t transferred = 0U;
    if (no_medium) return true;
    if (!gdox_mmc_read_dvd_structure(transport, 0U, pfi, sizeof(pfi),
            UINT32_C(10000), &transferred, error)) return false;
    if (transferred != sizeof(pfi) || memcmp(pfi + 17U, expected, 3U) != 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE,
            "refusing optical restoration onto different or unknown media");
        return false;
    }
    return true;
}

#endif
