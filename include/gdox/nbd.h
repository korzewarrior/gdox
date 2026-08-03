#ifndef GDOX_NBD_H
#define GDOX_NBD_H

#include "gdox/disc.h"
#include "gdox/error.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_nbd_export gdox_nbd_export;
typedef enum gdox_nbd_client_access {
    GDOX_NBD_CLIENT_READ_ONLY = 0,
    GDOX_NBD_CLIENT_WRITE_OPEN,
} gdox_nbd_client_access;
typedef struct gdox_nbd_read_stats {
    uint64_t requests;
    uint64_t requested_bytes;
    uint64_t successful_requests;
    uint64_t successful_bytes;
    uint64_t failed_requests;
    uint64_t sequential_requests;
    uint64_t discontinuous_requests;
    uint64_t served_without_drive_io_requests;
    uint64_t served_without_drive_io_bytes;
    uint64_t requests_with_drive_io;
    uint64_t physical_commands;
    uint64_t physical_sectors;
    uint64_t physical_bytes;
    uint64_t service_milliseconds;
    uint64_t maximum_service_milliseconds;
} gdox_nbd_read_stats;
typedef bool (*gdox_nbd_disc_inspector)(
    gdox_random_disc *disc,
    void *context,
    gdox_error *error
);

/*
 * Start a one-client-at-a-time, read-only NBD export on IPv4 loopback.
 * READ_ONLY advertises protocol-level read-only access. WRITE_OPEN omits that
 * advertisement for clients that insist on opening optical media writable;
 * the server still rejects every write request in either mode.
 * `disc` is moved into the export on success and remains untouched on failure.
 */
bool gdox_nbd_start(
    gdox_random_disc *disc,
    gdox_nbd_client_access client_access,
    gdox_nbd_export **exported,
    gdox_error *error
);
const char *gdox_nbd_uri(const gdox_nbd_export *exported);
const char *gdox_nbd_display_uri(const gdox_nbd_export *exported);
uint64_t gdox_nbd_length(const gdox_nbd_export *exported);
bool gdox_nbd_media_present(const gdox_nbd_export *exported);
bool gdox_nbd_observe_media(
    const gdox_nbd_export *exported,
    gdox_media_observation *output
);
bool gdox_nbd_physical_read_stats(
    const gdox_nbd_export *exported,
    gdox_physical_read_stats *output
);
/*
 * Returns in-memory telemetry for guest NBD reads. Physical counters contain
 * only successful hardware work attributable to those requests; preparation
 * performed before the client reads is excluded. A successful request is
 * counted as served without drive I/O only when physical statistics were
 * available before and after it and no new optical command was observed.
 */
bool gdox_nbd_get_read_stats(
    const gdox_nbd_export *exported,
    gdox_nbd_read_stats *output
);
bool gdox_nbd_runtime_error(const gdox_nbd_export *exported, gdox_error *error);

/*
 * Runs a serialized read-only inspection against the owned disc while no NBD
 * client is active. The export retains ownership throughout the callback.
 */
bool gdox_nbd_inspect_disc(
    gdox_nbd_export *exported,
    gdox_nbd_disc_inspector inspector,
    void *context,
    gdox_error *error
);
/*
 * Stops the server before closing the owned disc. On failure, the export is
 * retained and may be passed to this function again. Runtime read failures are
 * reported separately by gdox_nbd_runtime_error.
 */
bool gdox_nbd_close(gdox_nbd_export *exported, gdox_error *error);

#ifdef __cplusplus
}
#endif

#endif
