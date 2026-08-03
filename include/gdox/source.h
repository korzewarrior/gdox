#ifndef GDOX_SOURCE_H
#define GDOX_SOURCE_H

#include "gdox/error.h"
#include "gdox/evidence.h"
#include "gdox/sector.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_sector_source gdox_sector_source;

typedef struct gdox_physical_read_stats {
    uint64_t commands;
    uint64_t sectors;
    uint64_t bytes;
    uint64_t last_lba;
} gdox_physical_read_stats;

typedef enum gdox_media_readiness {
    GDOX_MEDIA_READINESS_UNKNOWN = 0,
    GDOX_MEDIA_READINESS_ABSENT,
    GDOX_MEDIA_READINESS_PRESENT,
} gdox_media_readiness;

typedef enum gdox_media_event {
    GDOX_MEDIA_EVENT_NONE = 0,
    GDOX_MEDIA_EVENT_EJECT_REQUEST,
    GDOX_MEDIA_EVENT_NEW_MEDIA,
    GDOX_MEDIA_EVENT_REMOVAL,
    GDOX_MEDIA_EVENT_CHANGED,
} gdox_media_event;

/*
 * `generation` changes when a removable source observes that the medium may
 * have changed. It is stable for file-backed and other immutable sources.
 */
typedef struct gdox_media_observation {
    gdox_media_readiness readiness;
    uint64_t generation;
    gdox_media_event event;
} gdox_media_observation;

typedef enum gdox_removable_session_status {
    GDOX_REMOVABLE_SESSION_PRESENT = 0,
    GDOX_REMOVABLE_SESSION_UNAVAILABLE,
    GDOX_REMOVABLE_SESSION_EJECT_REQUESTED,
    GDOX_REMOVABLE_SESSION_CHANGED,
} gdox_removable_session_status;

gdox_removable_session_status gdox_removable_session_classify(
    const gdox_media_observation *observation,
    bool generation_known,
    uint64_t expected_generation
);

typedef struct gdox_sector_source_ops {
    uint64_t (*sector_count)(const void *context);
    bool (*read)(
        void *context,
        uint64_t lba,
        uint32_t blocks,
        uint8_t *output,
        size_t output_bytes,
        gdox_error *error
    );
    bool (*media_present)(const void *context);
    bool (*close)(void *context, gdox_error *error);
    bool (*evidence)(const void *context, gdox_disc_evidence *output);
    bool (*physical_read_stats)(
        const void *context,
        gdox_physical_read_stats *output
    );
    /*
     * Optional. Requests that in-flight and future reads fail with
     * GDOX_ERROR_CANCELLED instead of entering recovery. Safe from any
     * thread while the source remains open.
     */
    void (*abort)(void *context);
    /*
     * Optional. Completes safety-critical shutdown without consuming the
     * context. The operation must be idempotent; after it succeeds, later
     * calls must also succeed. On failure, the context must remain valid so
     * shutdown can be retried.
     */
    bool (*prepare_close)(void *context, gdox_error *error);
    /*
     * Optional. Reports readiness without collapsing transient transport
     * failures into absence. Appended to preserve existing member order.
     */
    void (*observe_media)(
        const void *context,
        gdox_media_observation *output
    );
} gdox_sector_source_ops;

struct gdox_sector_source {
    void *context;
    const gdox_sector_source_ops *ops;
};

/*
 * Callers serialize reads against other reads. Media queries may run while a
 * read is active, and abort is explicitly safe from another thread. Prepare
 * and close require all in-flight operations to be drained first.
 */

typedef struct gdox_byte_patch {
    uint64_t offset;
    uint8_t value;
} gdox_byte_patch;

bool gdox_source_is_valid(const gdox_sector_source *source);
uint64_t gdox_source_sector_count(const gdox_sector_source *source);
bool gdox_source_read(
    gdox_sector_source *source,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
);
bool gdox_source_media_present(const gdox_sector_source *source);
bool gdox_source_observe_media(
    const gdox_sector_source *source,
    gdox_media_observation *output
);
bool gdox_source_evidence(
    const gdox_sector_source *source,
    gdox_disc_evidence *output
);
bool gdox_source_physical_read_stats(
    const gdox_sector_source *source,
    gdox_physical_read_stats *output
);
void gdox_source_abort(gdox_sector_source *source);
bool gdox_source_prepare_close(gdox_sector_source *source, gdox_error *error);
/* A failed prepare_close leaves the source open and eligible for retry. */
bool gdox_source_close(gdox_sector_source *source, gdox_error *error);
void gdox_source_destroy(gdox_sector_source *source);

bool gdox_source_validate_read(
    uint64_t sectors,
    uint64_t lba,
    uint32_t blocks,
    size_t output_bytes,
    gdox_error *error
);

/*
 * Constructors initialize `output`; it must not already own a source.
 * Adapter constructors move `inner` into the new source on success and leave
 * it untouched on failure.
 */
bool gdox_source_open_file(
    const char *path,
    gdox_sector_source *output,
    gdox_error *error
);
bool gdox_source_make_partition(
    gdox_sector_source *inner,
    uint64_t base_lba,
    gdox_sector_source *output,
    gdox_error *error
);
bool gdox_source_make_patched(
    gdox_sector_source *inner,
    const gdox_byte_patch *patches,
    size_t patch_count,
    gdox_sector_source *output,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
