#ifndef GDOX_NBD_TELEMETRY_H
#define GDOX_NBD_TELEMETRY_H

#include "gdox/disc.h"
#include "gdox/nbd.h"

#include "platform/portable_sync.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct gdox_nbd_telemetry {
    gdox_nbd_read_stats reads;
    uint64_t previous_read_end;
    bool previous_read_valid;
} gdox_nbd_telemetry;

void gdox_nbd_telemetry_record(
    gdox_nbd_telemetry *telemetry,
    gdox_mutex *mutex,
    uint64_t offset,
    uint32_t length,
    bool sequence_valid,
    bool succeeded,
    const gdox_physical_read_stats *physical_before,
    const gdox_physical_read_stats *physical_after,
    uint64_t elapsed_ms
);
bool gdox_nbd_telemetry_snapshot(
    const gdox_nbd_telemetry *telemetry,
    gdox_mutex *mutex,
    gdox_nbd_read_stats *output
);

#endif
