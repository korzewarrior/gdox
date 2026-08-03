#include "core/file_readahead_source.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define FILE_READAHEAD_CACHE_BYTES (16U * 1024U * 1024U)

typedef struct file_cache_slot {
    uint64_t start_sector;
    uint64_t last_used;
    uint64_t epoch;
    size_t extent_index;
    uint32_t blocks;
    bool valid;
} file_cache_slot;

typedef struct file_readahead_context {
    gdox_sector_source inner;
    gdox_xdvdfs_file_extent *extents;
    uint8_t *touched_extents;
    size_t extent_count;
    uint8_t *cache;
    file_cache_slot *slots;
    size_t slot_count;
    size_t slot_bytes;
    uint64_t use_counter;
    uint32_t max_window_blocks;
    atomic_uint_fast64_t cache_epoch;
    atomic_bool aborted;
    atomic_bool closing;
    atomic_bool media_invalid;
    atomic_bool generation_known;
    atomic_uint_fast64_t observed_generation;
} file_readahead_context;

static void invalidate_cache(file_readahead_context *context)
{
    (void)atomic_fetch_add_explicit(
        &context->cache_epoch,
        1U,
        memory_order_acq_rel
    );
}

static bool request_cancelled(
    const file_readahead_context *context,
    gdox_error *error
)
{
    if (!atomic_load_explicit(&context->aborted, memory_order_acquire)
        && !atomic_load_explicit(&context->closing, memory_order_acquire)
        && !atomic_load_explicit(
            &context->media_invalid,
            memory_order_acquire
        )) {
        return false;
    }
    gdox_error_set(error, GDOX_ERROR_CANCELLED, "file read-ahead session is no longer readable");
    return true;
}

static void apply_media_observation(
    file_readahead_context *context,
    const gdox_media_observation *observation
)
{
    const bool invalid_session =
        observation->readiness == GDOX_MEDIA_READINESS_ABSENT
        || observation->event == GDOX_MEDIA_EVENT_EJECT_REQUEST
        || observation->event == GDOX_MEDIA_EVENT_NEW_MEDIA
        || observation->event == GDOX_MEDIA_EVENT_REMOVAL
        || observation->event == GDOX_MEDIA_EVENT_CHANGED;
    bool invalidate =
        observation->readiness != GDOX_MEDIA_READINESS_PRESENT
        || invalid_session;

    if (observation->readiness == GDOX_MEDIA_READINESS_PRESENT) {
        if (atomic_load_explicit(
                &context->generation_known,
                memory_order_acquire
            ) && atomic_load_explicit(
                &context->observed_generation,
                memory_order_acquire
            ) != observation->generation) {
            invalidate = true;
            atomic_store_explicit(
                &context->media_invalid,
                true,
                memory_order_release
            );
        }
        atomic_store_explicit(
            &context->observed_generation,
            observation->generation,
            memory_order_release
        );
        atomic_store_explicit(
            &context->generation_known,
            true,
            memory_order_release
        );
    }
    if (invalid_session) {
        atomic_store_explicit(
            &context->media_invalid,
            true,
            memory_order_release
        );
    }
    if (invalidate) {
        invalidate_cache(context);
    }
}

static void establish_media_baseline(file_readahead_context *context)
{
    gdox_media_observation observation;

    if (!gdox_source_observe_media(&context->inner, &observation)) {
        return;
    }
    if (observation.event == GDOX_MEDIA_EVENT_EJECT_REQUEST
        || observation.event == GDOX_MEDIA_EVENT_REMOVAL
        || observation.event == GDOX_MEDIA_EVENT_CHANGED) {
        atomic_store_explicit(
            &context->media_invalid,
            true,
            memory_order_release
        );
        return;
    }
    if (observation.readiness == GDOX_MEDIA_READINESS_PRESENT) {
        atomic_store_explicit(
            &context->observed_generation,
            observation.generation,
            memory_order_release
        );
        atomic_store_explicit(
            &context->generation_known,
            true,
            memory_order_release
        );
    } else if (observation.readiness == GDOX_MEDIA_READINESS_ABSENT) {
        atomic_store_explicit(
            &context->media_invalid,
            true,
            memory_order_release
        );
    }
}

static uint64_t readahead_sector_count(const void *raw_context)
{
    const file_readahead_context *context = raw_context;
    return gdox_source_sector_count(&context->inner);
}

static bool find_containing_extent(
    const file_readahead_context *context,
    uint64_t lba,
    uint32_t blocks,
    size_t *extent_index,
    uint64_t *extent_end
)
{
    const uint64_t request_end = lba + blocks;
    size_t low = 0U;
    size_t high = context->extent_count;
    size_t containing_limit;

    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if ((uint64_t)context->extents[middle].start_sector <= lba) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) {
        return false;
    }
    containing_limit = low - 1U;
    if (context->extents[containing_limit].prefix_max_end < request_end) {
        return false;
    }

    low = 0U;
    high = containing_limit + 1U;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if (context->extents[middle].prefix_max_end < request_end) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    *extent_end =
        (uint64_t)context->extents[low].start_sector
        + context->extents[low].sector_count;
    if (extent_index != NULL) {
        *extent_index = low;
    }
    return *extent_end >= request_end;
}

static bool mark_extent_touched(
    file_readahead_context *context,
    size_t extent_index
)
{
    const size_t byte_index = extent_index / 8U;
    const uint8_t mask = (uint8_t)(1U << (extent_index % 8U));
    const bool first_touch =
        (context->touched_extents[byte_index] & mask) == 0U;

    context->touched_extents[byte_index] |= mask;
    return first_touch;
}

static uint8_t *slot_data(
    const file_readahead_context *context,
    size_t slot_index
)
{
    return context->cache + slot_index * context->slot_bytes;
}

static uint64_t next_use_stamp(file_readahead_context *context)
{
    size_t index;

    ++context->use_counter;
    if (context->use_counter != 0U) {
        return context->use_counter;
    }
    for (index = 0U; index < context->slot_count; ++index) {
        context->slots[index].last_used = 0U;
    }
    context->use_counter = 1U;
    return context->use_counter;
}

static size_t find_cached_request(
    const file_readahead_context *context,
    uint64_t epoch,
    uint64_t lba,
    uint32_t blocks
)
{
    const uint64_t request_end = lba + blocks;
    size_t index;

    for (index = 0U; index < context->slot_count; ++index) {
        const file_cache_slot *slot = &context->slots[index];

        if (slot->valid && slot->epoch == epoch
            && lba >= slot->start_sector
            && request_end <= slot->start_sector + slot->blocks) {
            return index;
        }
    }
    return SIZE_MAX;
}

static bool copy_cached_request(
    file_readahead_context *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    bool *found,
    gdox_error *error
)
{
    const uint64_t epoch = atomic_load_explicit(
        &context->cache_epoch,
        memory_order_acquire
    );
    const size_t slot_index = find_cached_request(
        context,
        epoch,
        lba,
        blocks
    );
    file_cache_slot *slot;

    *found = false;
    if (slot_index == SIZE_MAX) {
        return true;
    }
    slot = &context->slots[slot_index];
    memcpy(
        output,
        slot_data(context, slot_index)
            + (size_t)(lba - slot->start_sector)
                * GDOX_LOGICAL_SECTOR_BYTES,
        (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES
    );
    slot->last_used = next_use_stamp(context);
    if (request_cancelled(context, error)) {
        return false;
    }
    if (epoch != atomic_load_explicit(
            &context->cache_epoch,
            memory_order_acquire
        )) {
        return true;
    }
    *found = true;
    return true;
}

static bool has_adjacent_slot(
    const file_readahead_context *context,
    uint64_t epoch,
    size_t extent_index,
    uint64_t lba
)
{
    size_t index;

    for (index = 0U; index < context->slot_count; ++index) {
        const file_cache_slot *slot = &context->slots[index];

        if (slot->valid && slot->epoch == epoch
            && slot->extent_index == extent_index
            && slot->start_sector + slot->blocks == lba) {
            return true;
        }
    }
    return false;
}

static size_t reserve_cache_slot(
    file_readahead_context *context,
    uint64_t epoch
)
{
    size_t victim = SIZE_MAX;
    uint64_t oldest_use = UINT64_MAX;
    size_t index;

    for (index = 0U; index < context->slot_count; ++index) {
        const file_cache_slot *slot = &context->slots[index];

        if (!slot->valid || slot->epoch != epoch) {
            victim = index;
            break;
        }
        if (slot->last_used < oldest_use) {
            oldest_use = slot->last_used;
            victim = index;
        }
    }
    context->slots[victim].valid = false;
    return victim;
}

static bool session_is_readable(const file_readahead_context *context)
{
    return !atomic_load_explicit(&context->aborted, memory_order_acquire)
        && !atomic_load_explicit(&context->closing, memory_order_acquire)
        && !atomic_load_explicit(
            &context->media_invalid,
            memory_order_acquire
        );
}

static void publish_cache_slot(
    file_readahead_context *context,
    size_t slot_index,
    uint64_t epoch,
    size_t extent_index,
    uint64_t lba,
    uint32_t blocks
)
{
    file_cache_slot *slot = &context->slots[slot_index];

    if (epoch != atomic_load_explicit(
            &context->cache_epoch,
            memory_order_acquire
        ) || !session_is_readable(context)) {
        return;
    }
    slot->start_sector = lba;
    slot->blocks = blocks;
    slot->extent_index = extent_index;
    slot->epoch = epoch;
    slot->last_used = next_use_stamp(context);
    slot->valid = true;
}

static void retain_exact_read(
    file_readahead_context *context,
    uint64_t epoch,
    size_t extent_index,
    uint64_t lba,
    uint32_t blocks,
    const uint8_t *output
)
{
    size_t slot_index;

    if (blocks > context->max_window_blocks
        || epoch != atomic_load_explicit(
            &context->cache_epoch,
            memory_order_acquire
        ) || !session_is_readable(context)) {
        return;
    }
    slot_index = reserve_cache_slot(context, epoch);
    memcpy(
        slot_data(context, slot_index),
        output,
        (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES
    );
    publish_cache_slot(
        context,
        slot_index,
        epoch,
        extent_index,
        lba,
        blocks
    );
}

static bool media_was_removed(file_readahead_context *context)
{
    gdox_media_observation observation;

    if (!gdox_source_observe_media(&context->inner, &observation)) {
        return false;
    }
    apply_media_observation(context, &observation);
    return observation.readiness == GDOX_MEDIA_READINESS_ABSENT
        || observation.event == GDOX_MEDIA_EVENT_EJECT_REQUEST
        || observation.event == GDOX_MEDIA_EVENT_NEW_MEDIA
        || observation.event == GDOX_MEDIA_EVENT_REMOVAL
        || observation.event == GDOX_MEDIA_EVENT_CHANGED;
}

static bool exact_read(
    file_readahead_context *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    bool cacheable,
    size_t extent_index,
    gdox_error *error
)
{
    const uint64_t epoch = atomic_load_explicit(
        &context->cache_epoch,
        memory_order_acquire
    );

    if (!gdox_source_read(
            &context->inner,
            lba,
            blocks,
            output,
            output_bytes,
            error
        )) {
        return false;
    }
    if (request_cancelled(context, error)) {
        return false;
    }
    if (cacheable) {
        retain_exact_read(
            context,
            epoch,
            extent_index,
            lba,
            blocks,
            output
        );
    }
    return true;
}

static bool speculative_read(
    file_readahead_context *context,
    uint64_t lba,
    uint32_t blocks,
    uint32_t window_blocks,
    size_t extent_index,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_error speculative_error;
    const uint64_t epoch = atomic_load_explicit(
        &context->cache_epoch,
        memory_order_acquire
    );
    const size_t slot_index = reserve_cache_slot(context, epoch);
    uint8_t *const data = slot_data(context, slot_index);

    gdox_error_clear(&speculative_error);
    if (gdox_source_read(
            &context->inner,
            lba,
            window_blocks,
            data,
            (size_t)window_blocks * GDOX_LOGICAL_SECTOR_BYTES,
            &speculative_error
        )) {
        if (request_cancelled(context, error)) {
            return false;
        }
        memcpy(output, data, output_bytes);
        publish_cache_slot(
            context,
            slot_index,
            epoch,
            extent_index,
            lba,
            window_blocks
        );
        return true;
    }
    if (speculative_error.code == GDOX_ERROR_CANCELLED
        || media_was_removed(context)) {
        if (error != NULL) {
            *error = speculative_error;
        }
        return false;
    }
    return exact_read(
        context,
        lba,
        blocks,
        output,
        output_bytes,
        true,
        extent_index,
        error
    );
}

static bool bounded_window_read(
    file_readahead_context *context,
    uint64_t lba,
    uint32_t blocks,
    uint64_t extent_end,
    uint64_t source_end,
    size_t extent_index,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    uint64_t available = extent_end - lba;
    uint32_t window_blocks;

    if (available > source_end - lba) {
        available = source_end - lba;
    }
    window_blocks = available < context->max_window_blocks
        ? (uint32_t)available
        : context->max_window_blocks;
    return window_blocks > blocks
        ? speculative_read(
            context,
            lba,
            blocks,
            window_blocks,
            extent_index,
            output,
            output_bytes,
            error
        )
        : exact_read(
            context,
            lba,
            blocks,
            output,
            output_bytes,
            true,
            extent_index,
            error
        );
}

static bool readahead_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    file_readahead_context *context = raw_context;
    const uint64_t source_end = gdox_source_sector_count(&context->inner);
    size_t extent_index = 0U;
    uint64_t extent_end;
    bool contained;
    bool cache_hit;
    bool first_touch;
    bool adjacent;
    bool success;

    if (request_cancelled(context, error)) {
        return false;
    }
    contained = find_containing_extent(
        context,
        lba,
        blocks,
        &extent_index,
        &extent_end
    );
    if (!copy_cached_request(
            context,
            lba,
            blocks,
            output,
            &cache_hit,
            error
        )) {
        return false;
    }
    if (cache_hit) {
        return !request_cancelled(context, error);
    }
    first_touch = contained && mark_extent_touched(context, extent_index);
    adjacent = contained && has_adjacent_slot(
        context,
        atomic_load_explicit(
            &context->cache_epoch,
            memory_order_acquire
        ),
        extent_index,
        lba
    );
    if ((first_touch || adjacent)
        && blocks < context->max_window_blocks) {
        success = bounded_window_read(
            context,
            lba,
            blocks,
            extent_end,
            source_end,
            extent_index,
            output,
            output_bytes,
            error
        );
    } else {
        success = exact_read(
            context,
            lba,
            blocks,
            output,
            output_bytes,
            contained,
            extent_index,
            error
        );
    }
    if (success && request_cancelled(context, error)) {
        return false;
    }
    return success;
}

static bool readahead_media_present(const void *raw_context)
{
    file_readahead_context *context = (file_readahead_context *)raw_context;
    gdox_media_observation observation;

    if (!gdox_source_observe_media(&context->inner, &observation)) {
        return false;
    }
    apply_media_observation(context, &observation);
    return observation.readiness == GDOX_MEDIA_READINESS_PRESENT;
}

static void readahead_observe_media(
    const void *raw_context,
    gdox_media_observation *output
)
{
    file_readahead_context *context = (file_readahead_context *)raw_context;
    if (gdox_source_observe_media(&context->inner, output)) {
        apply_media_observation(context, output);
    }
}

static bool readahead_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const file_readahead_context *context = raw_context;
    return gdox_source_evidence(&context->inner, output);
}

static bool readahead_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const file_readahead_context *context = raw_context;
    return gdox_source_physical_read_stats(&context->inner, output);
}

static void readahead_abort(void *raw_context)
{
    file_readahead_context *context = raw_context;
    atomic_store_explicit(&context->aborted, true, memory_order_release);
    invalidate_cache(context);
    gdox_source_abort(&context->inner);
}

static bool readahead_prepare_close(void *raw_context, gdox_error *error)
{
    file_readahead_context *context = raw_context;
    if (!gdox_source_prepare_close(&context->inner, error)) {
        return false;
    }
    atomic_store_explicit(&context->closing, true, memory_order_release);
    invalidate_cache(context);
    return true;
}

static bool readahead_close(void *raw_context, gdox_error *error)
{
    file_readahead_context *context = raw_context;
    const bool closed = gdox_source_close(&context->inner, error);

    free(context->slots);
    free(context->cache);
    free(context->touched_extents);
    free(context->extents);
    free(context);
    return closed;
}

static const gdox_sector_source_ops readahead_ops = {
    readahead_sector_count,
    readahead_read,
    readahead_media_present,
    readahead_close,
    readahead_evidence,
    readahead_physical_read_stats,
    readahead_abort,
    readahead_prepare_close,
    readahead_observe_media,
};

static bool copy_and_validate_extents(
    file_readahead_context *context,
    const gdox_xdvdfs_metadata *metadata,
    gdox_error *error
)
{
    const uint64_t source_sectors = gdox_source_sector_count(&context->inner);
    uint64_t prefix_max_end = 0U;
    size_t index;

    if (metadata->file_extent_count == 0U) {
        return true;
    }
    if (metadata->file_extents == NULL
        || metadata->file_extent_count
            > SIZE_MAX / sizeof(*context->extents)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "file extent metadata is invalid");
        return false;
    }
    context->extents = malloc(
        metadata->file_extent_count * sizeof(*context->extents)
    );
    if (context->extents == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate the file extent index");
        return false;
    }
    memcpy(
        context->extents,
        metadata->file_extents,
        metadata->file_extent_count * sizeof(*context->extents)
    );
    context->extent_count = metadata->file_extent_count;
    for (index = 0U; index < context->extent_count; ++index) {
        gdox_xdvdfs_file_extent *extent = &context->extents[index];
        const uint64_t end =
            (uint64_t)extent->start_sector + extent->sector_count;

        if ((index != 0U
                && extent->start_sector
                    < context->extents[index - 1U].start_sector)
            || end > source_sectors) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "file extent metadata is outside the source or unsorted");
            return false;
        }
        if (end > prefix_max_end) {
            prefix_max_end = end;
        }
        extent->prefix_max_end = prefix_max_end;
    }
    return true;
}

bool gdox_source_make_file_readahead(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    uint32_t max_window_blocks,
    gdox_sector_source *output,
    gdox_error *error
)
{
    file_readahead_context *context;
    size_t cache_bytes;

    gdox_error_clear(error);
    if (partition == NULL || output == NULL
        || partition == output || !gdox_source_is_valid(partition)
        || gdox_source_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "file read-ahead requires a valid partition, metadata, window, and empty output"
        );
        return false;
    }
    if (max_window_blocks == 0U) {
        *output = *partition;
        partition->context = NULL;
        partition->ops = NULL;
        return true;
    }
    if (metadata == NULL
        || max_window_blocks > GDOX_FILE_READAHEAD_MAX_WINDOW_BLOCKS
        || (uint64_t)max_window_blocks * GDOX_LOGICAL_SECTOR_BYTES
            > SIZE_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "file read-ahead requires metadata and a representable window"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate the file read-ahead source");
        return false;
    }
    context->inner = *partition;
    context->max_window_blocks = max_window_blocks;
    context->slot_bytes =
        (size_t)max_window_blocks * GDOX_LOGICAL_SECTOR_BYTES;
    context->slot_count =
        (size_t)FILE_READAHEAD_CACHE_BYTES / context->slot_bytes;
    atomic_init(&context->cache_epoch, 1U);
    atomic_init(&context->aborted, false);
    atomic_init(&context->closing, false);
    atomic_init(&context->media_invalid, false);
    atomic_init(&context->generation_known, false);
    atomic_init(&context->observed_generation, 0U);
    establish_media_baseline(context);
    if (!copy_and_validate_extents(context, metadata, error)) {
        free(context->extents);
        free(context);
        return false;
    }
    if (context->extent_count != 0U) {
        const size_t touched_bytes = context->extent_count / 8U
            + (context->extent_count % 8U != 0U ? 1U : 0U);

        context->touched_extents = calloc(touched_bytes, 1U);
        if (context->touched_extents == NULL) {
            free(context->extents);
            free(context);
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "could not allocate the file read-ahead touch index"
            );
            return false;
        }
    }
    while (context->slot_count != 0U) {
        cache_bytes = context->slot_count * context->slot_bytes;
        context->cache = malloc(cache_bytes);
        context->slots = calloc(
            context->slot_count,
            sizeof(*context->slots)
        );
        if (context->cache != NULL && context->slots != NULL) {
            break;
        }
        free(context->slots);
        free(context->cache);
        context->slots = NULL;
        context->cache = NULL;
        context->slot_count /= 2U;
    }
    if (context->slot_count == 0U) {
        free(context->touched_extents);
        free(context->extents);
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate the file read-ahead cache");
        return false;
    }
    partition->context = NULL;
    partition->ops = NULL;
    output->context = context;
    output->ops = &readahead_ops;
    return true;
}
