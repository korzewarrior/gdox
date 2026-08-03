#ifndef GDOX_GP08_SOURCE_H
#define GDOX_GP08_SOURCE_H

#include "gdox/error.h"
#include "gdox/source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*gdox_gp08_transport_opener)(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
);

/*
 * Opens one validated GP08 source using a caller-supplied transport opener.
 * The opener is responsible for enforcing USB identity. The resulting source
 * owns the opened transport until close. If initialization fails and transport
 * cleanup cannot complete, the function returns false with `source` valid so
 * the caller can retry gdox_source_close.
 */
bool gdox_gp08_source_open(
    gdox_gp08_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);

#endif
