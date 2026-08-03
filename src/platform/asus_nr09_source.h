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

typedef enum gdox_asus_nr09_media_kind {
    GDOX_ASUS_NR09_MEDIA_UNKNOWN = 0,
    GDOX_ASUS_NR09_MEDIA_XGD1,
    GDOX_ASUS_NR09_MEDIA_XGD2,
} gdox_asus_nr09_media_kind;

/*
 * Opens the exact ASUS SDRW-08D1S-U A202 / NR09 source through a
 * caller-supplied, USB-identity-gated transport. The source owns the
 * transport until close. If initialization fails and transport cleanup cannot
 * complete, the function returns false with `source` valid so the caller can
 * retry gdox_source_close.
 */
bool gdox_asus_nr09_source_open(
    gdox_asus_nr09_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);

/*
 * Selects a validated XGD profile and activates it without releasing or
 * reopening the transport.
 */
bool gdox_asus_nr09_detected_source_open(
    gdox_asus_nr09_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_asus_nr09_media_kind *selected_media,
    gdox_error *error
);
#endif
