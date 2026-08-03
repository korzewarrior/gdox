#ifndef GDOX_MMC_COMMANDS_H
#define GDOX_MMC_COMMANDS_H

#include "gdox/source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_mmc_identity {
    char vendor[9];
    char model[17];
    char revision[5];
} gdox_mmc_identity;

typedef struct gdox_mmc_media_tracker {
    uint64_t generation;
    gdox_media_event pending_event;
    bool absence_latched;
    bool change_latched;
} gdox_mmc_media_tracker;

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
bool gdox_mmc_parse_media_event(
    const uint8_t *response,
    size_t response_bytes,
    gdox_media_event *event
);
bool gdox_mmc_poll_media_event(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker
);
void gdox_mmc_media_tracker_begin_session(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker
);
bool gdox_mmc_media_tracker_transitioned(
    const gdox_mmc_media_tracker *tracker,
    uint64_t expected_generation
);
void gdox_mmc_media_tracker_note_sense(
    gdox_mmc_media_tracker *tracker,
    const uint8_t *sense,
    size_t sense_bytes
);
bool gdox_mmc_media_tracker_capture_transport_sense(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker
);
void gdox_mmc_observe_media(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker,
    gdox_media_observation *output
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
