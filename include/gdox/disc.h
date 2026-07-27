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
} gdox_random_disc_ops;

struct gdox_random_disc {
    void *context;
    const gdox_random_disc_ops *ops;
};

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
bool gdox_disc_physical_read_stats(
    const gdox_random_disc *disc,
    gdox_physical_read_stats *output
);
void gdox_disc_abort(gdox_random_disc *disc);
bool gdox_disc_close(gdox_random_disc *disc, gdox_error *error);
void gdox_disc_destroy(gdox_random_disc *disc);

/* Moves `source` on success and leaves it untouched on failure. */
bool gdox_disc_from_source(
    gdox_sector_source *source,
    gdox_random_disc *disc,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
