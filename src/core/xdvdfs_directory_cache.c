#include "core/xdvdfs_directory_cache.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define DIRECTORY_CACHE_MAX_BYTES (UINT64_C(64) * 1024U * 1024U)
#define DIRECTORY_CACHE_MAX_ENTRIES ((size_t)16U * 1024U)

typedef struct cached_directory_extent {
    uint64_t start_sector;
    uint64_t prefix_max_end;
    uint32_t blocks;
    uint8_t *bytes;
} cached_directory_extent;

struct gdox_xdvdfs_directory_cache {
    cached_directory_extent *extents;
    size_t count;
    size_t capacity;
    uint64_t retained_bytes;
    bool finalized;
};

typedef struct directory_cache_context {
    gdox_sector_source inner;
    gdox_xdvdfs_directory_cache *cache;
    atomic_bool aborted;
    atomic_bool closing;
} directory_cache_context;

static uint64_t extent_end(const cached_directory_extent *extent)
{
    return extent->start_sector + extent->blocks;
}

static int compare_extents(const void *left_value, const void *right_value)
{
    const cached_directory_extent *left = left_value;
    const cached_directory_extent *right = right_value;

    if (left->start_sector < right->start_sector) {
        return -1;
    }
    if (left->start_sector > right->start_sector) {
        return 1;
    }
    if (left->blocks < right->blocks) {
        return -1;
    }
    if (left->blocks > right->blocks) {
        return 1;
    }
    return 0;
}

bool gdox_xdvdfs_directory_cache_create(
    gdox_xdvdfs_directory_cache **cache,
    gdox_error *error
)
{
    gdox_xdvdfs_directory_cache *created;

    gdox_error_clear(error);
    if (cache == NULL || *cache != NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty XDVDFS directory cache output is required"
        );
        return false;
    }
    created = calloc(1U, sizeof(*created));
    if (created == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate the XDVDFS directory cache"
        );
        return false;
    }
    *cache = created;
    return true;
}

void gdox_xdvdfs_directory_cache_destroy(
    gdox_xdvdfs_directory_cache **cache
)
{
    size_t index;

    if (cache == NULL || *cache == NULL) {
        return;
    }
    for (index = 0U; index < (*cache)->count; ++index) {
        free((*cache)->extents[index].bytes);
    }
    free((*cache)->extents);
    free(*cache);
    *cache = NULL;
}

bool gdox_xdvdfs_directory_cache_retain(
    gdox_xdvdfs_directory_cache *cache,
    uint32_t start_sector,
    uint32_t blocks,
    uint8_t **bytes,
    gdox_error *error
)
{
    const uint64_t retained =
        (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
    cached_directory_extent *resized;
    size_t capacity;

    if (cache == NULL || cache->finalized || blocks == 0U
        || bytes == NULL || *bytes == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "directory cache retention requires a live cache and buffer"
        );
        return false;
    }
    if (retained > DIRECTORY_CACHE_MAX_BYTES - cache->retained_bytes
        || cache->count >= DIRECTORY_CACHE_MAX_ENTRIES) {
        return true;
    }
    if (cache->count == cache->capacity) {
        capacity = cache->capacity == 0U ? 8U : cache->capacity * 2U;
        if (capacity > DIRECTORY_CACHE_MAX_ENTRIES) {
            capacity = DIRECTORY_CACHE_MAX_ENTRIES;
        }
        resized = realloc(
            cache->extents,
            capacity * sizeof(*resized)
        );
        if (resized == NULL) {
            return true;
        }
        cache->extents = resized;
        cache->capacity = capacity;
    }
    cache->extents[cache->count] = (cached_directory_extent){
        start_sector,
        0U,
        blocks,
        *bytes,
    };
    ++cache->count;
    cache->retained_bytes += retained;
    *bytes = NULL;
    return true;
}

void gdox_xdvdfs_directory_cache_finalize(
    gdox_xdvdfs_directory_cache *cache
)
{
    uint64_t prefix_max_end = 0U;
    size_t index;

    if (cache == NULL || cache->finalized) {
        return;
    }
    if (cache->count > 1U) {
        qsort(
            cache->extents,
            cache->count,
            sizeof(*cache->extents),
            compare_extents
        );
    }
    for (index = 0U; index < cache->count; ++index) {
        const uint64_t end = extent_end(&cache->extents[index]);

        if (end > prefix_max_end) {
            prefix_max_end = end;
        }
        cache->extents[index].prefix_max_end = prefix_max_end;
    }
    cache->finalized = true;
}

static size_t first_containing_extent(
    const gdox_xdvdfs_directory_cache *cache,
    uint64_t lba
)
{
    size_t low = 0U;
    size_t high = cache->count;
    size_t containing_limit;

    while (low < high) {
        const size_t middle = low + (high - low) / 2U;

        if (cache->extents[middle].start_sector <= lba) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) {
        return cache->count;
    }
    containing_limit = low - 1U;
    if (cache->extents[containing_limit].prefix_max_end <= lba) {
        return cache->count;
    }
    low = 0U;
    high = containing_limit + 1U;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;

        if (cache->extents[middle].prefix_max_end <= lba) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return extent_end(&cache->extents[low]) > lba
        ? low
        : cache->count;
}

static uint64_t next_extent_start(
    const gdox_xdvdfs_directory_cache *cache,
    uint64_t lba
)
{
    size_t low = 0U;
    size_t high = cache->count;

    while (low < high) {
        const size_t middle = low + (high - low) / 2U;

        if (cache->extents[middle].start_sector <= lba) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low < cache->count
        ? cache->extents[low].start_sector
        : UINT64_MAX;
}

static bool request_cancelled(
    const directory_cache_context *context,
    gdox_error *error
)
{
    if (!atomic_load_explicit(&context->aborted, memory_order_acquire)
        && !atomic_load_explicit(&context->closing, memory_order_acquire)) {
        return false;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_CANCELLED,
        "XDVDFS directory cache source is no longer readable"
    );
    return true;
}

static uint64_t cache_sector_count(const void *raw_context)
{
    const directory_cache_context *context = raw_context;
    return gdox_source_sector_count(&context->inner);
}

static bool cache_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    directory_cache_context *context = raw_context;
    uint64_t current = lba;
    uint32_t remaining = blocks;
    size_t output_offset = 0U;

    (void)output_bytes;
    if (request_cancelled(context, error)) {
        return false;
    }
    while (remaining != 0U) {
        const size_t cached = first_containing_extent(
            context->cache,
            current
        );
        uint32_t chunk;

        if (request_cancelled(context, error)) {
            return false;
        }

        if (cached < context->cache->count) {
            const cached_directory_extent *extent =
                &context->cache->extents[cached];
            const uint64_t available = extent_end(extent) - current;

            chunk = available < remaining
                ? (uint32_t)available
                : remaining;
            memcpy(
                output + output_offset,
                extent->bytes
                    + (size_t)(current - extent->start_sector)
                        * GDOX_LOGICAL_SECTOR_BYTES,
                (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES
            );
            if (request_cancelled(context, error)) {
                return false;
            }
        } else {
            const uint64_t next = next_extent_start(
                context->cache,
                current
            );
            uint64_t available = remaining;

            if (next != UINT64_MAX && next - current < available) {
                available = next - current;
            }
            chunk = (uint32_t)available;
            if (!gdox_source_read(
                    &context->inner,
                    current,
                    chunk,
                    output + output_offset,
                    (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES,
                    error
                )) {
                return false;
            }
        }
        current += chunk;
        remaining -= chunk;
        output_offset += (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES;
    }
    return !request_cancelled(context, error);
}

static bool cache_media_present(const void *raw_context)
{
    const directory_cache_context *context = raw_context;
    return gdox_source_media_present(&context->inner);
}

static void cache_observe_media(
    const void *raw_context,
    gdox_media_observation *output
)
{
    const directory_cache_context *context = raw_context;
    (void)gdox_source_observe_media(&context->inner, output);
}

static bool cache_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const directory_cache_context *context = raw_context;
    return gdox_source_evidence(&context->inner, output);
}

static bool cache_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const directory_cache_context *context = raw_context;
    return gdox_source_physical_read_stats(&context->inner, output);
}

static void cache_abort(void *raw_context)
{
    directory_cache_context *context = raw_context;
    atomic_store_explicit(&context->aborted, true, memory_order_release);
    gdox_source_abort(&context->inner);
}

static bool cache_prepare_close(void *raw_context, gdox_error *error)
{
    directory_cache_context *context = raw_context;

    if (!gdox_source_prepare_close(&context->inner, error)) {
        return false;
    }
    atomic_store_explicit(&context->closing, true, memory_order_release);
    return true;
}

static bool cache_close(void *raw_context, gdox_error *error)
{
    directory_cache_context *context = raw_context;
    const bool closed = gdox_source_close(&context->inner, error);

    gdox_xdvdfs_directory_cache_destroy(&context->cache);
    free(context);
    return closed;
}

static const gdox_sector_source_ops cache_ops = {
    cache_sector_count,
    cache_read,
    cache_media_present,
    cache_close,
    cache_evidence,
    cache_physical_read_stats,
    cache_abort,
    cache_prepare_close,
    cache_observe_media,
};

bool gdox_source_make_xdvdfs_directory_cache(
    gdox_sector_source *partition,
    gdox_xdvdfs_directory_cache **cache,
    gdox_sector_source *output,
    gdox_error *error
)
{
    directory_cache_context *context;
    uint64_t source_sectors;
    size_t index;

    gdox_error_clear(error);
    if (partition == NULL || cache == NULL || *cache == NULL
        || output == NULL || partition == output
        || !gdox_source_is_valid(partition) || gdox_source_is_valid(output)
        || !(*cache)->finalized) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "directory cache source requires a partition, finalized cache, and empty output"
        );
        return false;
    }
    source_sectors = gdox_source_sector_count(partition);
    for (index = 0U; index < (*cache)->count; ++index) {
        const cached_directory_extent *extent = &(*cache)->extents[index];

        if (extent->bytes == NULL || extent->start_sector > source_sectors
            || extent->blocks > source_sectors - extent->start_sector) {
            gdox_error_set(
                error,
                GDOX_ERROR_OUT_OF_BOUNDS,
                "cached XDVDFS directory is outside the game partition"
            );
            return false;
        }
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate the XDVDFS directory cache source"
        );
        return false;
    }
    context->inner = *partition;
    context->cache = *cache;
    atomic_init(&context->aborted, false);
    atomic_init(&context->closing, false);
    partition->context = NULL;
    partition->ops = NULL;
    *cache = NULL;
    output->context = context;
    output->ops = &cache_ops;
    return true;
}
