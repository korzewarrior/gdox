#include "core/hdd_cache.h"

#include "core/ports/random_access_file.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QCOW2_HEADER_MIN_BYTES 104U
#define QCOW2_MAGIC UINT32_C(0x514649fb)
#define QCOW2_COMPRESSED (UINT64_C(1) << 62U)
#define QCOW2_ZERO UINT64_C(1)
#define QCOW2_OFFSET_MASK UINT64_C(0x00fffffffffffe00)

#define XBOX_CACHE_METADATA_BYTES ((size_t)128U * 1024U)
#define XBOX_CACHE_PARTITION_BYTES UINT64_C(0x2ee00000)

typedef struct xbox_cache_partition {
    uint64_t offset;
    uint32_t volume_id;
} xbox_cache_partition;

typedef struct qcow2_layout {
    gdox_random_access_file *file;
    uint64_t file_bytes;
    uint64_t guest_bytes;
    uint64_t cluster_bytes;
    uint64_t l1_offset;
    uint32_t l1_entries;
} qcow2_layout;

typedef enum qcow2_cluster_kind {
    QCOW2_CLUSTER_ZERO = 0,
    QCOW2_CLUSTER_COMPRESSED,
    QCOW2_CLUSTER_NORMAL,
} qcow2_cluster_kind;

static const xbox_cache_partition cache_partitions[] = {
    {UINT64_C(0x00080000), UINT32_C(0x000d0137)},
    {UINT64_C(0x2ee80000), UINT32_C(0x000d01d4)},
    {UINT64_C(0x5dc80000), UINT32_C(0x000d02a4)},
};

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static uint64_t read_be_u64(const uint8_t *input)
{
    return (uint64_t)read_be_u32(input) << 32U
        | (uint64_t)read_be_u32(input + 4U);
}

static void put_le_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void build_empty_fatx_metadata(uint32_t volume_id, uint8_t *output)
{
    memset(output, 0, XBOX_CACHE_METADATA_BYTES);
    memset(output, 0xff, 4096U);
    memcpy(output, "FATX", 4U);
    put_le_u32(output + 4U, volume_id);
    put_le_u32(output + 8U, UINT32_C(32));
    put_le_u32(output + 12U, UINT32_C(1));
    output[16] = 0U;
    output[17] = 0U;
    output[4096U] = 0xf8U;
    output[4097U] = 0xffU;
    output[4098U] = 0xffU;
    output[4099U] = 0xffU;
}

static bool range_inside(uint64_t offset, uint64_t bytes, uint64_t length)
{
    return offset <= length && bytes <= length - offset;
}

static bool read_layout(
    qcow2_layout *layout,
    const char *path,
    gdox_error *error
)
{
    uint8_t header[QCOW2_HEADER_MIN_BYTES];
    uint32_t version;
    uint32_t cluster_bits;
    uint32_t encryption;
    uint64_t backing_offset;
    uint32_t backing_bytes;
    uint32_t snapshot_count;

    if (!gdox_random_access_file_open_update(
            path,
            &layout->file,
            &layout->file_bytes,
            error
        ) || !gdox_random_access_file_read(
            layout->file,
            0U,
            header,
            sizeof(header),
            error
        )) {
        return false;
    }
    version = read_be_u32(header + 4U);
    backing_offset = read_be_u64(header + 8U);
    backing_bytes = read_be_u32(header + 16U);
    cluster_bits = read_be_u32(header + 20U);
    layout->guest_bytes = read_be_u64(header + 24U);
    encryption = read_be_u32(header + 32U);
    layout->l1_entries = read_be_u32(header + 36U);
    layout->l1_offset = read_be_u64(header + 40U);
    snapshot_count = read_be_u32(header + 60U);
    if (read_be_u32(header) != QCOW2_MAGIC
        || (version != 2U && version != 3U)
        || backing_offset != 0U
        || backing_bytes != 0U
        || encryption != 0U
        || cluster_bits < 9U
        || cluster_bits > 21U
        || layout->l1_entries == 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "managed Xbox hard disk is not a supported standalone QCOW2 image"
        );
        return false;
    }
    if (snapshot_count != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "managed Xbox hard disk has internal snapshots that share data clusters"
        );
        return false;
    }
    if (version == 3U && read_be_u64(header + 72U) != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "managed Xbox hard disk has unsupported QCOW2 features"
        );
        return false;
    }
    layout->cluster_bytes = UINT64_C(1) << cluster_bits;
    if (!range_inside(
            layout->l1_offset,
            (uint64_t)layout->l1_entries * 8U,
            layout->file_bytes
        ) || layout->guest_bytes
            < cache_partitions[2].offset + XBOX_CACHE_PARTITION_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "managed Xbox hard disk has invalid QCOW2 geometry"
        );
        return false;
    }
    return true;
}

static bool resolve_cluster(
    const qcow2_layout *layout,
    uint64_t guest_offset,
    qcow2_cluster_kind *kind,
    uint64_t *physical_offset,
    gdox_error *error
)
{
    const uint64_t guest_cluster = guest_offset / layout->cluster_bytes;
    const uint64_t l2_entries = layout->cluster_bytes / 8U;
    const uint64_t l1_index = guest_cluster / l2_entries;
    const uint64_t l2_index = guest_cluster % l2_entries;
    uint8_t encoded[8];
    uint64_t l1_entry;
    uint64_t l2_entry;
    uint64_t l2_offset;
    uint64_t data_offset;

    if (l1_index >= layout->l1_entries
        || !gdox_random_access_file_read(
            layout->file,
            layout->l1_offset + l1_index * 8U,
            encoded,
            sizeof(encoded),
            error
        )) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "Xbox cache partition is outside the QCOW2 mapping"
            );
        }
        return false;
    }
    l1_entry = read_be_u64(encoded);
    l2_offset = l1_entry & QCOW2_OFFSET_MASK;
    if (l2_offset == 0U
        || !range_inside(l2_offset, layout->cluster_bytes, layout->file_bytes)
        || !gdox_random_access_file_read(
            layout->file,
            l2_offset + l2_index * 8U,
            encoded,
            sizeof(encoded),
            error
        )) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "Xbox cache partition has an invalid QCOW2 mapping"
            );
        }
        return false;
    }
    l2_entry = read_be_u64(encoded);
    if ((l2_entry & QCOW2_COMPRESSED) != 0U) {
        *kind = QCOW2_CLUSTER_COMPRESSED;
        *physical_offset = 0U;
        return true;
    }
    data_offset = l2_entry & QCOW2_OFFSET_MASK;
    if (data_offset == 0U || (l2_entry & QCOW2_ZERO) != 0U) {
        *kind = QCOW2_CLUSTER_ZERO;
        *physical_offset = 0U;
        return true;
    }
    if (!range_inside(data_offset, layout->cluster_bytes, layout->file_bytes)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "Xbox cache partition points outside the QCOW2 file"
        );
        return false;
    }
    *kind = QCOW2_CLUSTER_NORMAL;
    *physical_offset = data_offset;
    return true;
}

static bool reset_partition(
    const qcow2_layout *layout,
    const xbox_cache_partition *partition,
    uint8_t *metadata,
    bool *changed,
    gdox_error *error
)
{
    uint64_t completed = 0U;

    build_empty_fatx_metadata(partition->volume_id, metadata);
    while (completed < XBOX_CACHE_METADATA_BYTES) {
        const uint64_t guest_offset = partition->offset + completed;
        const uint64_t within = guest_offset % layout->cluster_bytes;
        const uint64_t available = layout->cluster_bytes - within;
        const size_t remaining = XBOX_CACHE_METADATA_BYTES - (size_t)completed;
        const size_t chunk =
            available < remaining ? (size_t)available : remaining;
        qcow2_cluster_kind kind;
        uint64_t physical_offset;

        if (!resolve_cluster(
                layout,
                guest_offset,
                &kind,
                &physical_offset,
                error
            )) {
            return false;
        }
        if (kind == QCOW2_CLUSTER_NORMAL) {
            if (!gdox_random_access_file_write(
                    layout->file,
                    physical_offset + within,
                    metadata + completed,
                    chunk,
                    error
                )) {
                return false;
            }
            *changed = true;
        } else if (kind == QCOW2_CLUSTER_ZERO && completed < 4096U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "Xbox cache partition is missing its FATX superblock"
            );
            return false;
        }
        completed += chunk;
    }
    return true;
}

bool gdox_hdd_reset_cache_partitions(
    const char *path,
    bool *changed,
    gdox_error *error
)
{
    qcow2_layout layout = {0};
    uint8_t *metadata = NULL;
    bool modified = false;
    bool success = false;
    size_t index;
    gdox_error close_error;

    gdox_error_clear(error);
    if (path == NULL || path[0] == '\0' || changed == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "managed Xbox hard-disk path and reset result are required"
        );
        return false;
    }
    *changed = false;
    metadata = malloc(XBOX_CACHE_METADATA_BYTES);
    if (metadata == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Xbox cache metadata"
        );
        return false;
    }
    if (!read_layout(&layout, path, error)) {
        goto cleanup;
    }
    for (index = 0U;
         index < sizeof(cache_partitions) / sizeof(cache_partitions[0]);
         ++index) {
        if (!reset_partition(
                &layout,
                &cache_partitions[index],
                metadata,
                &modified,
                error
            )) {
            goto cleanup;
        }
    }
    if (modified) {
        if (!gdox_random_access_file_sync_close(layout.file, error)) {
            layout.file = NULL;
            goto cleanup;
        }
        layout.file = NULL;
    } else if (!gdox_random_access_file_close(layout.file, error)) {
        layout.file = NULL;
        goto cleanup;
    } else {
        layout.file = NULL;
    }
    *changed = modified;
    success = true;

cleanup:
    free(metadata);
    if (layout.file != NULL
        && !gdox_random_access_file_close(layout.file, &close_error)
        && success) {
        *error = close_error;
        success = false;
    }
    return success;
}
