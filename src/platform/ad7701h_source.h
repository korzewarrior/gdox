#ifndef GDOX_AD7701H_SOURCE_H
#define GDOX_AD7701H_SOURCE_H

#include "gdox/source.h"
#include "platform/scsi_transport.h"

/* The opener enforces USB identity. A failed open can retain a valid source
 * when restoration fails; the caller must retry close before releasing it. */
bool gdox_ad7701h_source_open(
    bool (*opener)(void *, gdox_scsi_transport *, gdox_error *),
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);

#endif
