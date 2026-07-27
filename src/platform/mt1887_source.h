#ifndef GDOX_MT1887_SOURCE_H
#define GDOX_MT1887_SOURCE_H

#include "gdox/error.h"
#include "gdox/source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*gdox_mt1887_transport_opener)(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
);

/*
 * Opens and validates one MT1887-backed GP63 source using a transport owned by
 * the caller's platform adapter. The resulting sector source owns the opened
 * transport until it is closed.
 */
bool gdox_mt1887_source_open(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);

#endif
