#ifndef GDOX_GP63_SG_LINUX_H
#define GDOX_GP63_SG_LINUX_H

#include "gdox/error.h"
#include "gdox/optical.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Opens the SCSI generic character device bound to the uniquely identified
 * Linux optical block device for an
 * HL-DT-ST DVDRAM GP63EX70 RF02 behind USB 0e8d:1887.
 * The returned SG_IO transport admits only the audited GameCube and Wii data-in
 * packets, one START packet, and four L259 volatile admission fields;
 * data-out commands, other RAM writes, and resets are rejected.
 */
bool gdox_gp63_sg_linux_open(
    gdox_scsi_transport *transport,
    gdox_error *error
);
bool gdox_gp63_sg_linux_open_device(
    const gdox_optical_device *device,
    gdox_scsi_transport *transport,
    gdox_error *error
);

/* Internal policy predicate shared with the deterministic transport tests. */
bool gdox_gp63_sg_linux_command_allowed(
    const uint8_t *cdb,
    size_t cdb_bytes,
    bool data_in,
    size_t data_bytes,
    uint32_t timeout_ms
);

bool gdox_gp63_sg_linux_parse_deferred_sense(
    const uint8_t *sense,
    size_t sense_bytes,
    uint8_t *key,
    uint8_t *asc,
    uint8_t *ascq
);

#endif
