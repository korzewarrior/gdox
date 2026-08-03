#ifndef GDOX_DISC_H
#define GDOX_DISC_H

#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_random_disc gdox_random_disc;

typedef struct gdox_random_disc_ops {
    uint64_t (*length)(const void *context);
    bool (*read_at)(
        void *context,
        uint64_t offset,
        uint8_t *output,
        size_t output_bytes,
        size_t *read_bytes,
        gdox_error *error
    );
    bool (*media_present)(const void *context);
    bool (*close)(void *context, gdox_error *error);
    bool (*physical_read_stats)(
        const void *context,
        gdox_physical_read_stats *output
    );
    /* Optional. Forwards gdox_source_abort to the underlying source. */
    void (*abort)(void *context);
    /*
     * Optional. Completes safety-critical shutdown without consuming the
     * context. The operation must be idempotent; after it succeeds, later
     * calls must also succeed. On failure, the context must remain valid so
     * shutdown can be retried.
     */
    bool (*prepare_close)(void *context, gdox_error *error);
    /* Optional. Forwards typed removable-media state when available. */
    void (*observe_media)(
        const void *context,
        gdox_media_observation *output
    );
} gdox_random_disc_ops;

struct gdox_random_disc {
    void *context;
    const gdox_random_disc_ops *ops;
};

/*
 * Callers serialize reads against other reads. Media queries may run while a
 * read is active, and abort is explicitly safe from another thread. Prepare
 * and close require all in-flight operations to be drained first.
 */

bool gdox_disc_is_valid(const gdox_random_disc *disc);
uint64_t gdox_disc_length(const gdox_random_disc *disc);
bool gdox_disc_read_at(
    gdox_random_disc *disc,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
);
bool gdox_disc_media_present(const gdox_random_disc *disc);
bool gdox_disc_observe_media(
    const gdox_random_disc *disc,
    gdox_media_observation *output
);
bool gdox_disc_physical_read_stats(
    const gdox_random_disc *disc,
    gdox_physical_read_stats *output
);
void gdox_disc_abort(gdox_random_disc *disc);
bool gdox_disc_prepare_close(gdox_random_disc *disc, gdox_error *error);
/* A failed prepare_close leaves the disc open and eligible for retry. */
bool gdox_disc_close(gdox_random_disc *disc, gdox_error *error);

/* Requires an empty `disc`; moves `source` only after all fallible setup. */
bool gdox_disc_from_source(
    gdox_sector_source *source,
    gdox_random_disc *disc,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
