#include "core/xbe_patch_source.h"

#include <stdlib.h>
#include <string.h>

#define XBE_SIGNATURE_BYTES 8U
#define XBE_PATCH_OFFSET 7U
#define XBE_PATCH_VALUE 0xebU

enum {
    XBE_HEADER_UNKNOWN = 0,
    XBE_HEADER_VALID,
    XBE_HEADER_INVALID,
};

typedef struct xbe_patch_extent {
    uint64_t offset;
    uint64_t length;
    uint64_t prefix_max_end;
    unsigned char header_state;
} xbe_patch_extent;

typedef struct xbe_patch_context {
    gdox_sector_source inner;
    xbe_patch_extent *extents;
    size_t extent_count;
} xbe_patch_context;

typedef struct sector_cache {
    uint8_t bytes[GDOX_LOGICAL_SECTOR_BYTES];
    uint64_t lba;
    bool valid;
} sector_cache;

static const uint8_t media_check_signature[XBE_SIGNATURE_BYTES] = {
    0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
};

bool gdox_xbe_patch_complete_file(uint8_t *bytes, size_t length)
{
    size_t offset;

    if (bytes == NULL || length < XBE_SIGNATURE_BYTES
        || memcmp(bytes, "XBEH", 4U) != 0) {
        return false;
    }
    for (offset = 0U;
         offset <= length - XBE_SIGNATURE_BYTES;
         ++offset) {
        if (memcmp(
                bytes + offset,
                media_check_signature,
                XBE_SIGNATURE_BYTES
            ) == 0) {
            bytes[offset + XBE_PATCH_OFFSET] = XBE_PATCH_VALUE;
        }
    }
    return true;
}

static uint64_t extent_end(const xbe_patch_extent *extent)
{
    return extent->offset + extent->length;
}

static int compare_extents(const void *left_value, const void *right_value)
{
    const xbe_patch_extent *left = left_value;
    const xbe_patch_extent *right = right_value;

    if (left->offset < right->offset) {
        return -1;
    }
    if (left->offset > right->offset) {
        return 1;
    }
    if (left->length < right->length) {
        return -1;
    }
    if (left->length > right->length) {
        return 1;
    }
    return 0;
}

static size_t first_possible_extent(
    const xbe_patch_context *context,
    uint64_t offset
)
{
    size_t low = 0U;
    size_t high = context->extent_count;

    while (low < high) {
        const size_t middle = low + (high - low) / 2U;

        if (context->extents[middle].prefix_max_end <= offset) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

static bool ranges_intersect(
    uint64_t left_start,
    uint64_t left_end,
    uint64_t right_start,
    uint64_t right_end
)
{
    return left_start < right_end && right_start < left_end;
}

static bool read_cached_sector(
    xbe_patch_context *context,
    uint64_t lba,
    sector_cache *cache,
    gdox_error *error
)
{
    if (cache->valid && cache->lba == lba) {
        return true;
    }
    if (!gdox_source_read(
            &context->inner,
            lba,
            1U,
            cache->bytes,
            sizeof(cache->bytes),
            error
        )) {
        return false;
    }
    cache->lba = lba;
    cache->valid = true;
    return true;
}

static bool validate_header(
    xbe_patch_context *context,
    xbe_patch_extent *extent,
    uint64_t request_start,
    uint64_t request_end,
    const uint8_t *output,
    sector_cache *cache,
    gdox_error *error
)
{
    const uint8_t *header;
    static const uint8_t magic[4] = {'X', 'B', 'E', 'H'};

    if (extent->header_state != XBE_HEADER_UNKNOWN) {
        return true;
    }
    if (request_start <= extent->offset
        && extent->offset + sizeof(magic) <= request_end) {
        header = output + (size_t)(extent->offset - request_start);
    } else {
        const uint64_t header_lba =
            extent->offset / GDOX_LOGICAL_SECTOR_BYTES;

        if (!read_cached_sector(context, header_lba, cache, error)) {
            return false;
        }
        header = cache->bytes;
    }
    extent->header_state = memcmp(header, magic, sizeof(magic)) == 0
        ? XBE_HEADER_VALID
        : XBE_HEADER_INVALID;
    return true;
}

static uint8_t patch_window_byte(
    uint64_t position,
    uint64_t request_start,
    const uint8_t *output,
    uint64_t prefix_start,
    const uint8_t *prefix
)
{
    return position < request_start
        ? prefix[(size_t)(position - prefix_start)]
        : output[(size_t)(position - request_start)];
}

static bool crossing_signature_possible(
    const xbe_patch_extent *extent,
    uint64_t request_start,
    uint64_t request_end,
    const uint8_t *output
)
{
    const uint64_t request_bytes = request_end - request_start;
    const size_t maximum = request_bytes < XBE_PATCH_OFFSET
        ? (size_t)request_bytes
        : XBE_PATCH_OFFSET;
    size_t returned;

    for (returned = 1U; returned <= maximum; ++returned) {
        const uint64_t patch_position = request_start + returned - 1U;

        if (patch_position >= extent->offset + XBE_PATCH_OFFSET
            && memcmp(
                output,
                media_check_signature + XBE_SIGNATURE_BYTES - returned,
                returned
            ) == 0) {
            return true;
        }
    }
    return false;
}

static void patch_extent_bytes(
    const xbe_patch_extent *extent,
    uint64_t request_start,
    uint64_t request_end,
    uint8_t *output,
    uint64_t prefix_start,
    const uint8_t *prefix
)
{
    uint64_t position = extent->offset > prefix_start
        ? extent->offset
        : prefix_start;
    uint64_t final_start = request_end - XBE_SIGNATURE_BYTES;
    const uint64_t extent_final = extent_end(extent) - XBE_SIGNATURE_BYTES;

    if (extent_final < final_start) {
        final_start = extent_final;
    }
    for (; position <= final_start; ++position) {
        size_t index;
        bool matched = true;

        for (index = 0U; index < XBE_SIGNATURE_BYTES; ++index) {
            if (patch_window_byte(
                    position + index,
                    request_start,
                    output,
                    prefix_start,
                    prefix
                ) != media_check_signature[index]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            const uint64_t patch_position = position + XBE_PATCH_OFFSET;

            if (patch_position >= request_start
                && patch_position < request_end) {
                output[(size_t)(patch_position - request_start)] =
                    XBE_PATCH_VALUE;
            }
        }
    }
}

static uint64_t patch_sector_count(const void *raw_context)
{
    const xbe_patch_context *context = raw_context;
    return gdox_source_sector_count(&context->inner);
}

static bool patch_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    xbe_patch_context *context = raw_context;
    const uint64_t request_start = lba * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t request_end = request_start + output_bytes;
    sector_cache auxiliary = {0};
    uint64_t prefix_start = request_start;
    const uint8_t *prefix = output;
    size_t first;
    size_t index;
    bool prefix_needed = false;

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
    first = first_possible_extent(context, request_start);
    for (index = first;
         index < context->extent_count
            && context->extents[index].offset < request_end;
         ++index) {
        xbe_patch_extent *extent = &context->extents[index];

        if (!ranges_intersect(
                request_start,
                request_end,
                extent->offset,
                extent_end(extent)
            )) {
            continue;
        }
        if (!validate_header(
                context,
                extent,
                request_start,
                request_end,
                output,
                &auxiliary,
                error
            )) {
            return false;
        }
        if (extent->header_state == XBE_HEADER_VALID
            && extent->offset < request_start
            && crossing_signature_possible(
                extent,
                request_start,
                request_end,
                output
            )) {
            prefix_needed = true;
        }
    }
    if (prefix_needed) {
        const uint64_t prefix_lba = lba - 1U;

        if (!read_cached_sector(context, prefix_lba, &auxiliary, error)) {
            return false;
        }
        prefix_start = request_start - XBE_PATCH_OFFSET;
        prefix = auxiliary.bytes
            + GDOX_LOGICAL_SECTOR_BYTES - XBE_PATCH_OFFSET;
    }
    for (index = first;
         index < context->extent_count
            && context->extents[index].offset < request_end;
         ++index) {
        const xbe_patch_extent *extent = &context->extents[index];

        if (extent->header_state == XBE_HEADER_VALID
            && ranges_intersect(
                request_start,
                request_end,
                extent->offset,
                extent_end(extent)
            )) {
            patch_extent_bytes(
                extent,
                request_start,
                request_end,
                output,
                prefix_start,
                prefix
            );
        }
    }
    return true;
}

static bool patch_media_present(const void *raw_context)
{
    const xbe_patch_context *context = raw_context;
    return gdox_source_media_present(&context->inner);
}

static void patch_observe_media(
    const void *raw_context,
    gdox_media_observation *output
)
{
    const xbe_patch_context *context = raw_context;
    (void)gdox_source_observe_media(&context->inner, output);
}

static bool patch_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const xbe_patch_context *context = raw_context;
    return gdox_source_evidence(&context->inner, output);
}

static bool patch_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const xbe_patch_context *context = raw_context;
    return gdox_source_physical_read_stats(&context->inner, output);
}

static void patch_abort(void *raw_context)
{
    xbe_patch_context *context = raw_context;
    gdox_source_abort(&context->inner);
}

static bool patch_prepare_close(void *raw_context, gdox_error *error)
{
    xbe_patch_context *context = raw_context;
    return gdox_source_prepare_close(&context->inner, error);
}

static bool patch_close(void *raw_context, gdox_error *error)
{
    xbe_patch_context *context = raw_context;
    const bool closed = gdox_source_close(&context->inner, error);

    free(context->extents);
    free(context);
    return closed;
}

static const gdox_sector_source_ops patch_ops = {
    patch_sector_count,
    patch_read,
    patch_media_present,
    patch_close,
    patch_evidence,
    patch_physical_read_stats,
    patch_abort,
    patch_prepare_close,
    patch_observe_media,
};

static bool copy_extents(
    const gdox_xdvdfs_metadata *metadata,
    uint64_t source_bytes,
    xbe_patch_extent **output,
    size_t *output_count,
    gdox_error *error
)
{
    xbe_patch_extent *extents;
    uint64_t maximum_end = 0U;
    size_t count = 0U;
    size_t index;

    *output = NULL;
    *output_count = 0U;
    if (metadata->xbe_file_count == 0U) {
        return true;
    }
    if (metadata->xbe_files == NULL
        || metadata->xbe_file_count > SIZE_MAX / sizeof(*extents)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "XBE patch metadata is invalid"
        );
        return false;
    }
    extents = calloc(metadata->xbe_file_count, sizeof(*extents));
    if (extents == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate XBE patch extents"
        );
        return false;
    }
    for (index = 0U; index < metadata->xbe_file_count; ++index) {
        const gdox_xdvdfs_entry *entry = &metadata->xbe_files[index];
        const uint64_t offset =
            (uint64_t)entry->start_sector * GDOX_LOGICAL_SECTOR_BYTES;
        const uint64_t length = entry->size;

        if (offset > source_bytes || length > source_bytes - offset) {
            free(extents);
            gdox_error_set(
                error,
                GDOX_ERROR_OUT_OF_BOUNDS,
                "XBE patch extent is outside the game partition"
            );
            return false;
        }
        if (length < XBE_SIGNATURE_BYTES) {
            continue;
        }
        extents[count].offset = offset;
        extents[count].length = length;
        ++count;
    }
    if (count > 1U) {
        qsort(extents, count, sizeof(*extents), compare_extents);
    }
    *output_count = 0U;
    for (index = 0U; index < count; ++index) {
        if (*output_count != 0U) {
            const xbe_patch_extent *previous =
                &extents[*output_count - 1U];

            if (extents[index].offset == previous->offset
                && extents[index].length == previous->length) {
                continue;
            }
            if (extents[index].offset < extent_end(previous)) {
                free(extents);
                gdox_error_set(
                    error,
                    GDOX_ERROR_INVALID_VOLUME,
                    "XBE compatibility extents overlap"
                );
                *output_count = 0U;
                return false;
            }
        }
        extents[*output_count] = extents[index];
        ++*output_count;
    }
    count = *output_count;
    for (index = 0U; index < count; ++index) {
        const uint64_t end = extent_end(&extents[index]);

        if (end > maximum_end) {
            maximum_end = end;
        }
        extents[index].prefix_max_end = maximum_end;
        extents[index].header_state = XBE_HEADER_UNKNOWN;
    }
    *output = extents;
    return true;
}

bool gdox_source_make_xbe_patch_source(
    gdox_sector_source *partition,
    const gdox_xdvdfs_metadata *metadata,
    gdox_sector_source *output,
    gdox_error *error
)
{
    xbe_patch_context *context;
    xbe_patch_extent *extents = NULL;
    size_t extent_count = 0U;
    uint64_t sectors;

    gdox_error_clear(error);
    if (partition == NULL || metadata == NULL || output == NULL
        || partition == output || !gdox_source_is_valid(partition)
        || gdox_source_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "XBE patch source requires a valid partition, metadata, and empty output"
        );
        return false;
    }
    sectors = gdox_source_sector_count(partition);
    if (sectors > UINT64_MAX / GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "XBE patch source length overflows"
        );
        return false;
    }
    if (!copy_extents(
            metadata,
            sectors * GDOX_LOGICAL_SECTOR_BYTES,
            &extents,
            &extent_count,
            error
        )) {
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        free(extents);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate XBE patch source"
        );
        return false;
    }
    context->inner = *partition;
    context->extents = extents;
    context->extent_count = extent_count;
    partition->context = NULL;
    partition->ops = NULL;
    output->context = context;
    output->ops = &patch_ops;
    return true;
}
