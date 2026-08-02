#ifndef GDOX_MMC_COMMANDS_H
#define GDOX_MMC_COMMANDS_H

#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_mmc_identity {
    char vendor[9];
    char model[17];
    char revision[5];
} gdox_mmc_identity;

bool gdox_mmc_inquiry(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_identity *identity,
    gdox_error *error
);
bool gdox_mmc_test_unit_ready(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_error *error
);
bool gdox_mmc_request_sense(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    uint8_t output[18],
    size_t *transferred,
    gdox_error *error
);
bool gdox_mmc_read_capacity_10(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    uint32_t *last_lba,
    uint32_t *block_size,
    gdox_error *error
);
bool gdox_mmc_read_dvd_structure(
    gdox_scsi_transport *transport,
    uint8_t format,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
);
bool gdox_mmc_read_10(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
);
bool gdox_mmc_read_12(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
);

#endif
