#ifndef GDOX_NBD_H
#define GDOX_NBD_H

#include "gdox/disc.h"
#include "gdox/error.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_nbd_export gdox_nbd_export;

/*
 * Start a one-client-at-a-time, read-only NBD export on IPv4 loopback.
 * `disc` is moved into the export on success and remains untouched on failure.
 */
bool gdox_nbd_start(
    gdox_random_disc *disc,
    gdox_nbd_export **exported,
    gdox_error *error
);
const char *gdox_nbd_uri(const gdox_nbd_export *exported);
const char *gdox_nbd_display_uri(const gdox_nbd_export *exported);
bool gdox_nbd_media_present(const gdox_nbd_export *exported);
bool gdox_nbd_physical_read_stats(
    const gdox_nbd_export *exported,
    gdox_physical_read_stats *output
);
bool gdox_nbd_runtime_error(const gdox_nbd_export *exported, gdox_error *error);
bool gdox_nbd_close(gdox_nbd_export *exported, gdox_error *error);

#ifdef __cplusplus
}
#endif

#endif
