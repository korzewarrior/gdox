#ifndef GDOX_ASUS_NR09_SOURCE_H
#define GDOX_ASUS_NR09_SOURCE_H

#include "gdox/error.h"
#include "gdox/source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*gdox_asus_nr09_transport_opener)(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
);

/*
 * Opens the exact ASUS SDRW-08D1S-U A202 / NR09 source through a
 * caller-supplied, USB-identity-gated transport. The source owns the
 * transport until close.
 */
bool gdox_asus_nr09_source_open(
    gdox_asus_nr09_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
#endif
