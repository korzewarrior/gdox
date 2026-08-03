#include "core/default_xbe_cache_source.h"

#include "core/xbe_patch_source.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_XBE_CACHE_MAX_BYTES (UINT64_C(64) * 1024U * 1024U)
#define DEFAULT_XBE_CACHE_READ_BLOCKS UINT32_C(512)

typedef struct default_xbe_cache_context {
    gdox_sector_source inner;
    uint8_t *bytes;
    uint64_t start_sector;
    uint64_t sectors;
    atomic_bool aborted;
} default_xbe_cache_context;

static uint64_t cache_sector_count(const void *raw_context)
{
    const default_xbe_cache_context *context = raw_context;
    return gdox_source_sector_count(&context->inner);
}

static bool read_inner(
    default_xbe_cache_context *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    gdox_error *error
)
{
    return gdox_source_read(
        &context->inner,
        lba,
        blocks,
        output,
        (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES,
        error
    );
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
    default_xbe_cache_context *context = raw_context;
    const uint64_t cache_end = context->start_sector + context->sectors;
    uint64_t current = lba;
    uint32_t remaining = blocks;
    size_t output_offset = 0U;

    (void)output_bytes;
    if (atomic_load_explicit(&context->aborted, memory_order_acquire)) {
        gdox_error_set(
            error,
            GDOX_ERROR_CANCELLED,
            "default XBE cache source was cancelled"
        );
        return false;
    }
    while (remaining != 0U) {
        uint32_t chunk;

        if (context->bytes != NULL
            && current >= context->start_sector
            && current < cache_end) {
            const uint64_t available = cache_end - current;

            chunk = available < remaining
                ? (uint32_t)available
                : remaining;
            memcpy(
                output + output_offset,
                context->bytes
                    + (size_t)(current - context->start_sector)
                        * GDOX_LOGICAL_SECTOR_BYTES,
                (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES
            );
        } else {
            uint64_t available = remaining;

            if (context->bytes != NULL && current < context->start_sector) {
                available = context->start_sector - current;
                if (available > remaining) {
                    available = remaining;
                }
            }
            chunk = (uint32_t)available;
            if (!read_inner(
                    context,
                    current,
                    chunk,
                    output + output_offset,
                    error
                )) {
                return false;
            }
        }
        current += chunk;
        remaining -= chunk;
        output_offset += (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES;
    }
    return true;
}

static bool cache_media_present(const void *raw_context)
{
    const default_xbe_cache_context *context = raw_context;
    return gdox_source_media_present(&context->inner);
}

static void cache_observe_media(
    const void *raw_context,
    gdox_media_observation *output
)
{
    const default_xbe_cache_context *context = raw_context;
    (void)gdox_source_observe_media(&context->inner, output);
}

static bool cache_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const default_xbe_cache_context *context = raw_context;
    return gdox_source_evidence(&context->inner, output);
}

static bool cache_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const default_xbe_cache_context *context = raw_context;
    return gdox_source_physical_read_stats(&context->inner, output);
}

static void cache_abort(void *raw_context)
{
    default_xbe_cache_context *context = raw_context;
    atomic_store_explicit(&context->aborted, true, memory_order_release);
    gdox_source_abort(&context->inner);
}

static bool cache_prepare_close(void *raw_context, gdox_error *error)
{
    default_xbe_cache_context *context = raw_context;
    return gdox_source_prepare_close(&context->inner, error);
}

static bool cache_close(void *raw_context, gdox_error *error)
{
    default_xbe_cache_context *context = raw_context;
    const bool closed = gdox_source_close(&context->inner, error);

    free(context->bytes);
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

static bool prepare_default_xbe(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    uint8_t **bytes,
    uint64_t *start_sector,
    uint64_t *sectors,
    gdox_error *error
)
{
    const gdox_xdvdfs_entry *entry;
    uint64_t partition_sectors;
    uint64_t cache_sectors;
    size_t cache_bytes;
    uint8_t *cache;
    uint64_t completed = 0U;

    *bytes = NULL;
    *start_sector = 0U;
    *sectors = 0U;
    if (metadata->default_xbe_index == GDOX_XDVDFS_NO_ENTRY) {
        return true;
    }
    if (metadata->xbe_files == NULL
        || metadata->default_xbe_index >= metadata->xbe_file_count) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "default XBE cache metadata is invalid"
        );
        return false;
    }
    entry = &metadata->xbe_files[metadata->default_xbe_index];
    if (entry->size == 0U || entry->size > DEFAULT_XBE_CACHE_MAX_BYTES) {
        return true;
    }
    cache_sectors =
        ((uint64_t)entry->size + GDOX_LOGICAL_SECTOR_BYTES - 1U)
        / GDOX_LOGICAL_SECTOR_BYTES;
    partition_sectors = gdox_source_sector_count(partition);
    if ((uint64_t)entry->start_sector > partition_sectors
        || cache_sectors
            > partition_sectors - (uint64_t)entry->start_sector
        || cache_sectors > SIZE_MAX / GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_OUT_OF_BOUNDS,
            "default XBE cache extent is outside the game partition"
        );
        return false;
    }
    cache_bytes = (size_t)cache_sectors * GDOX_LOGICAL_SECTOR_BYTES;
    cache = malloc(cache_bytes);
    if (cache == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate the default XBE cache"
        );
        return false;
    }
    while (completed < cache_sectors) {
        const uint64_t remaining = cache_sectors - completed;
        const uint32_t blocks = remaining < DEFAULT_XBE_CACHE_READ_BLOCKS
            ? (uint32_t)remaining
            : DEFAULT_XBE_CACHE_READ_BLOCKS;

        if (!gdox_source_read(
                partition,
                (uint64_t)entry->start_sector + completed,
                blocks,
                cache
                    + (size_t)completed * GDOX_LOGICAL_SECTOR_BYTES,
                (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES,
                error
            )) {
            free(cache);
            return false;
        }
        completed += blocks;
    }
    (void)gdox_xbe_patch_complete_file(cache, entry->size);
    *bytes = cache;
    *start_sector = entry->start_sector;
    *sectors = cache_sectors;
    return true;
}

bool gdox_source_make_default_xbe_cache(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    gdox_sector_source *output,
    gdox_error *error
)
{
    default_xbe_cache_context *context;
    uint8_t *bytes = NULL;
    uint64_t start_sector = 0U;
    uint64_t sectors = 0U;

    gdox_error_clear(error);
    if (partition == NULL || metadata == NULL || output == NULL
        || partition == output || !gdox_source_is_valid(partition)
        || gdox_source_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "default XBE cache requires a valid partition, metadata, and empty output"
        );
        return false;
    }
    if (!prepare_default_xbe(
            partition,
            metadata,
            &bytes,
            &start_sector,
            &sectors,
            error
        )) {
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        free(bytes);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate the default XBE cache source"
        );
        return false;
    }
    context->inner = *partition;
    context->bytes = bytes;
    context->start_sector = start_sector;
    context->sectors = sectors;
    atomic_init(&context->aborted, false);
    partition->context = NULL;
    partition->ops = NULL;
    output->context = context;
    output->ops = &cache_ops;
    return true;
}
