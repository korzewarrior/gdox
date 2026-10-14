#ifndef GDOX_GP63_GAMECUBE_SOURCE_H
#define GDOX_GP63_GAMECUBE_SOURCE_H

#include "gdox/optical.h"

#include "gdox/error.h"
#include "gdox/source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*gdox_gp63_gamecube_transport_opener)(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
);

/*
 * Opens the read-only raw-cache path of an exact GP63 mechanism. The opener
 * must enforce the GP63 USB identity. This decoder-only entry does not write
 * volatile drive memory. The selected-device streaming entry is used for playback.
 */
bool gdox_gp63_gamecube_source_open(
    gdox_gp63_gamecube_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);

/* RF02 L259 path: temporarily opens and restores four volatile RAM
 * admission fields per cache acquisition, then authenticates every sector.
 * No firmware or nonvolatile drive state is modified. */
bool gdox_gp63_gamecube_ready_source_open(
    gdox_gp63_gamecube_transport_opener opener,
    void *opener_context,
    gdox_sector_source *source,
    gdox_error *error
);

/* Stock SEEK10 plus DVD quality scan; sector bytes still require PSN/EDC
 * validation from volatile drive RAM. No disc image or firmware patch. */
bool gdox_gp63_gamecube_scan_source_open(
    gdox_gp63_gamecube_transport_opener opener,
    void *opener_context,
    gdox_sector_source *source,
    gdox_error *error
);

/* Bounded RAM read-ahead for up to four observed sequential streams.
 * Requested reads take priority; speculation never uses slow recovery. */
bool gdox_gp63_gamecube_stream_source_open(
    gdox_gp63_gamecube_transport_opener opener,
    void *opener_context,
    gdox_sector_source *source,
    gdox_error *error
);

/* The decoded header selects GameCube or a validated single-layer Wii layout. */
gdox_optical_media_profile gdox_gp63_gamecube_source_profile(const gdox_sector_source *source);

#endif
