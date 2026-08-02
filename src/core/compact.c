#include "gdox/xdvdfs.h"

#include <stdlib.h>
#include <string.h>

#define COMPACT_MAX_DIRECTORY_BYTES (UINT64_C(64) * 1024U * 1024U)
#define COMPACT_MAX_METADATA_BYTES (UINT64_C(256) * 1024U * 1024U)
#define COMPACT_MAX_DIRECTORIES ((size_t)16U * 1024U)
#define COMPACT_MAX_ENTRIES ((size_t)256U * 1024U)
#define COMPACT_MAX_DEPTH 64U

typedef enum compact_segment_kind {
    COMPACT_SEGMENT_MEMORY = 0,
    COMPACT_SEGMENT_FILE,
    COMPACT_SEGMENT_ZERO
} compact_segment_kind;

typedef struct compact_segment {
    uint64_t output_sector;
    uint64_t sectors;
    uint64_t source_sector;
    uint64_t content_bytes;
    uint8_t *memory;
    compact_segment_kind kind;
} compact_segment;

typedef struct compact_directory {
    uint32_t source_sector;
    uint32_t size;
    uint32_t output_sector;
    size_t depth;
} compact_directory;

typedef struct compact_context {
    gdox_sector_source inner;
    compact_segment *segments;
    size_t segment_count;
    uint64_t sectors;
} compact_context;

typedef struct compact_builder {
    gdox_sector_source *source;
    compact_segment *segments;
    size_t segment_count;
    size_t segment_capacity;
    compact_directory *directories;
    size_t directory_count;
    size_t directory_capacity;
    uint64_t next_sector;
    uint64_t file_count;
    uint64_t metadata_bytes;
} compact_builder;

typedef struct offset_stack {
    size_t *items;
    size_t count;
    size_t capacity;
} offset_stack;

static const uint8_t compact_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

static uint32_t read_le_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8U
        | (uint32_t)bytes[2] << 16U
        | (uint32_t)bytes[3] << 24U;
}

static void put_le_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void put_le_u64(uint8_t *bytes, uint64_t value)
{
    put_le_u32(bytes, (uint32_t)(value & UINT64_C(0xffffffff)));
    put_le_u32(bytes + 4U, (uint32_t)(value >> 32U));
}

static uint64_t required_sectors(uint64_t bytes)
{
    if (bytes == 0U) {
        return 1U;
    }
    return (bytes + GDOX_LOGICAL_SECTOR_BYTES - 1U)
        / GDOX_LOGICAL_SECTOR_BYTES;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *output)
{
    if (left > UINT64_MAX - right) {
        return false;
    }
    *output = left + right;
    return true;
}

static void destroy_segments(compact_segment *segments, size_t count)
{
    size_t index;
    for (index = 0U; index < count; ++index) {
        free(segments[index].memory);
    }
    free(segments);
}

static bool push_segment(
    compact_builder *builder,
    compact_segment segment,
    gdox_error *error
)
{
    compact_segment *resized;
    size_t capacity;
    if (builder->segment_count >= COMPACT_MAX_ENTRIES + COMPACT_MAX_DIRECTORIES + 2U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "compact XISO segment limit exceeded");
        return false;
    }
    if (builder->segment_count == builder->segment_capacity) {
        capacity = builder->segment_capacity == 0U
            ? 64U
            : builder->segment_capacity * 2U;
        if (capacity > COMPACT_MAX_ENTRIES + COMPACT_MAX_DIRECTORIES + 2U) {
            capacity = COMPACT_MAX_ENTRIES + COMPACT_MAX_DIRECTORIES + 2U;
        }
        resized = realloc(builder->segments, capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate compact XISO segments");
            return false;
        }
        builder->segments = resized;
        builder->segment_capacity = capacity;
    }
    builder->segments[builder->segment_count++] = segment;
    return true;
}

static bool directory_exists(
    const compact_builder *builder,
    uint32_t source_sector,
    uint32_t size
)
{
    size_t index;
    for (index = 0U; index < builder->directory_count; ++index) {
        if (builder->directories[index].source_sector == source_sector
            && builder->directories[index].size == size) {
            return true;
        }
    }
    return false;
}

static bool push_directory(
    compact_builder *builder,
    compact_directory directory,
    gdox_error *error
)
{
    compact_directory *resized;
    size_t capacity;
    if (builder->directory_count >= COMPACT_MAX_DIRECTORIES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "compact XISO directory limit exceeded");
        return false;
    }
    if (directory_exists(builder, directory.source_sector, directory.size)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "XDVDFS contains a directory cycle or duplicate extent");
        return false;
    }
    if (builder->directory_count == builder->directory_capacity) {
        capacity = builder->directory_capacity == 0U
            ? 32U
            : builder->directory_capacity * 2U;
        if (capacity > COMPACT_MAX_DIRECTORIES) {
            capacity = COMPACT_MAX_DIRECTORIES;
        }
        resized = realloc(builder->directories, capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate compact XISO directories");
            return false;
        }
        builder->directories = resized;
        builder->directory_capacity = capacity;
    }
    builder->directories[builder->directory_count++] = directory;
    return true;
}

static bool push_offset(
    offset_stack *stack,
    size_t offset,
    gdox_error *error
)
{
    size_t *resized;
    size_t capacity;
    if (stack->count == stack->capacity) {
        capacity = stack->capacity == 0U ? 32U : stack->capacity * 2U;
        if (capacity > COMPACT_MAX_ENTRIES) {
            capacity = COMPACT_MAX_ENTRIES;
        }
        if (stack->count == capacity) {
            gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "directory entry limit exceeded");
            return false;
        }
        resized = realloc(stack->items, capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate directory traversal");
            return false;
        }
        stack->items = resized;
        stack->capacity = capacity;
    }
    stack->items[stack->count++] = offset;
    return true;
}

static bool allocate_region(
    compact_builder *builder,
    uint64_t bytes,
    uint32_t *sector,
    uint64_t *sectors,
    gdox_error *error
)
{
    uint64_t end;
    *sectors = required_sectors(bytes);
    if (builder->next_sector > UINT32_MAX
        || !add_u64(builder->next_sector, *sectors, &end)
        || end > (uint64_t)UINT32_MAX + 1U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "compact XISO exceeds the XDVDFS address space");
        return false;
    }
    *sector = (uint32_t)builder->next_sector;
    builder->next_sector = end;
    return true;
}

static bool bytes_are_empty_directory(const uint8_t *bytes, size_t length)
{
    size_t index;
    if (length < 14U) {
        return false;
    }
    for (index = 0U; index < 14U; ++index) {
        if (bytes[index] != 0xffU) {
            return false;
        }
    }
    return true;
}

typedef struct compact_entry {
    uint16_t left;
    uint16_t right;
    uint32_t source_sector;
    uint32_t size;
    uint8_t attributes;
    uint32_t output_sector;
    uint64_t output_blocks;
} compact_entry;

typedef struct compact_directory_reader {
    compact_builder *builder;
    compact_directory directory;
    uint8_t *data;
    uint8_t *visited;
    offset_stack *pending;
} compact_directory_reader;

static uint16_t compact_read_u16(const uint8_t *data, size_t offset)
{
    return (uint16_t)(
        (uint16_t)data[offset]
            | (uint16_t)((uint16_t)data[offset + 1U] << 8U)
    );
}

static bool read_compact_entry(
    compact_directory_reader *reader,
    size_t offset,
    compact_entry *entry,
    bool *already_visited,
    gdox_error *error
)
{
    const size_t directory_size = reader->directory.size;
    size_t slot;
    uint8_t name_bytes;
    uint64_t source_end;

    *already_visited = false;
    if (offset % 4U != 0U || offset > directory_size
        || directory_size - offset < 14U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "compact directory entry is truncated"
        );
        return false;
    }
    slot = offset / 4U;
    if (reader->visited[slot] != 0U) {
        *already_visited = true;
        return true;
    }
    reader->visited[slot] = 1U;
    entry->left = compact_read_u16(reader->data, offset);
    entry->right = compact_read_u16(reader->data, offset + 2U);
    entry->source_sector = read_le_u32(reader->data + offset + 4U);
    entry->size = read_le_u32(reader->data + offset + 8U);
    entry->attributes = reader->data[offset + 12U];
    name_bytes = reader->data[offset + 13U];
    if (name_bytes == 0U
        || name_bytes > directory_size - offset - 14U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "compact directory name is invalid"
        );
        return false;
    }
    if (!allocate_region(
        reader->builder,
        entry->size,
        &entry->output_sector,
        &entry->output_blocks,
        error
    ) || !add_u64(
        entry->source_sector,
        entry->output_blocks,
        &source_end
    ) || source_end > gdox_source_sector_count(reader->builder->source)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_VOLUME,
                "XDVDFS file extent exceeds the partition"
            );
        }
        return false;
    }
    put_le_u32(reader->data + offset + 4U, entry->output_sector);
    return true;
}

static bool record_compact_entry(
    compact_directory_reader *reader,
    const compact_entry *entry,
    gdox_error *error
)
{
    if ((entry->attributes & 0x10U) != 0U) {
        const compact_directory child = {
            entry->source_sector,
            entry->size,
            entry->output_sector,
            reader->directory.depth + 1U,
        };
        if (reader->directory.depth < COMPACT_MAX_DEPTH
            && push_directory(reader->builder, child, error)) {
            return true;
        }
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_VOLUME,
                "XDVDFS directory nesting limit exceeded"
            );
        }
        return false;
    }
    if (!push_segment(
        reader->builder,
        (compact_segment){
            entry->output_sector,
            entry->output_blocks,
            entry->source_sector,
            entry->size,
            NULL,
            entry->size == 0U ? COMPACT_SEGMENT_ZERO : COMPACT_SEGMENT_FILE,
        },
        error
    )) {
        return false;
    }
    ++reader->builder->file_count;
    return true;
}

static bool push_compact_entry_children(
    offset_stack *pending,
    const compact_entry *entry,
    gdox_error *error
)
{
    if (entry->left != 0U
        && !push_offset(pending, (size_t)entry->left * 4U, error)) {
        return false;
    }
    return entry->right == 0U
        || push_offset(pending, (size_t)entry->right * 4U, error);
}

static bool traverse_compact_directory(
    compact_directory_reader *reader,
    gdox_error *error
)
{
    const size_t visited_bytes =
        ((size_t)reader->directory.size + 3U) / 4U;

    reader->visited = calloc(visited_bytes, 1U);
    if (reader->visited == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate compact directory index"
        );
        return false;
    }
    if (!push_offset(reader->pending, 0U, error)) {
        return false;
    }
    while (reader->pending->count != 0U) {
        compact_entry entry;
        bool already_visited;
        size_t offset;

        --reader->pending->count;
        offset = reader->pending->items[reader->pending->count];
        if (!read_compact_entry(
            reader,
            offset,
            &entry,
            &already_visited,
            error
        )) {
            return false;
        }
        if (already_visited) {
            continue;
        }
        if (!record_compact_entry(reader, &entry, error)
            || !push_compact_entry_children(reader->pending, &entry, error)) {
            return false;
        }
    }
    return true;
}

static bool append_compact_directory_table(
    compact_builder *builder,
    compact_directory directory,
    uint64_t blocks,
    uint8_t *data,
    gdox_error *error
)
{
    return push_segment(
        builder,
        (compact_segment){
            directory.output_sector,
            blocks,
            0U,
            directory.size,
            data,
            COMPACT_SEGMENT_MEMORY,
        },
        error
    );
}

static bool process_directory(
    compact_builder *builder,
    size_t directory_index,
    gdox_error *error
)
{
    const compact_directory directory = builder->directories[directory_index];
    const uint64_t blocks = required_sectors(directory.size);
    const uint64_t allocated_bytes_u64 = blocks * GDOX_LOGICAL_SECTOR_BYTES;
    const size_t allocated_bytes = (size_t)allocated_bytes_u64;
    uint8_t *data;
    uint8_t *visited = NULL;
    offset_stack pending = {0};
    compact_directory_reader reader;
    bool success = false;

    if (directory.size == 0U || directory.size > COMPACT_MAX_DIRECTORY_BYTES
        || blocks > SIZE_MAX / GDOX_LOGICAL_SECTOR_BYTES
        || builder->metadata_bytes > COMPACT_MAX_METADATA_BYTES - allocated_bytes_u64) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "XDVDFS directory metadata exceeds compacting limits");
        return false;
    }
    data = malloc(allocated_bytes);
    if (data == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate compact directory table");
        return false;
    }
    if (!gdox_source_read(
            builder->source,
            directory.source_sector,
            (uint32_t)blocks,
            data,
            allocated_bytes,
            error
        )) {
        free(data);
        return false;
    }
    builder->metadata_bytes += allocated_bytes_u64;
    if (!bytes_are_empty_directory(data, directory.size)) {
        reader = (compact_directory_reader){
            builder,
            directory,
            data,
            NULL,
            &pending,
        };
        if (!traverse_compact_directory(&reader, error)) {
            visited = reader.visited;
            goto cleanup;
        }
        visited = reader.visited;
    }
    if (!append_compact_directory_table(
        builder,
        directory,
        blocks,
        data,
        error
    )) {
        goto cleanup;
    }
    data = NULL;
    success = true;

cleanup:
    free(pending.items);
    free(visited);
    free(data);
    return success;
}

static int compare_segments(const void *left_value, const void *right_value)
{
    const compact_segment *left = left_value;
    const compact_segment *right = right_value;
    if (left->output_sector < right->output_sector) {
        return -1;
    }
    if (left->output_sector > right->output_sector) {
        return 1;
    }
    return 0;
}

static bool build_compact_layout(
    compact_builder *builder,
    const gdox_xdvdfs_volume *volume,
    uint64_t *output_sectors,
    gdox_error *error
)
{
    const uint64_t header_sectors = 33U;
    const size_t header_bytes =
        (size_t)header_sectors * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *header = calloc(header_bytes, 1U);
    uint32_t root_output_sector;
    uint64_t root_blocks;
    uint64_t aligned;
    size_t index;

    if (header == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate compact XISO header");
        return false;
    }
    builder->next_sector = header_sectors;
    if (!allocate_region(
            builder,
            volume->root_directory_size,
            &root_output_sector,
            &root_blocks,
            error
        )) {
        free(header);
        return false;
    }
    memcpy(
        header + (size_t)32U * GDOX_LOGICAL_SECTOR_BYTES,
        compact_magic,
        sizeof(compact_magic)
    );
    put_le_u32(
        header + (size_t)32U * GDOX_LOGICAL_SECTOR_BYTES + 20U,
        root_output_sector
    );
    put_le_u32(
        header + (size_t)32U * GDOX_LOGICAL_SECTOR_BYTES + 24U,
        volume->root_directory_size
    );
    put_le_u64(
        header + (size_t)32U * GDOX_LOGICAL_SECTOR_BYTES + 28U,
        volume->image_timestamp
    );
    memcpy(
        header + (size_t)33U * GDOX_LOGICAL_SECTOR_BYTES
            - sizeof(compact_magic),
        compact_magic,
        sizeof(compact_magic)
    );
    if (!push_segment(
            builder,
            (compact_segment){
                0U,
                header_sectors,
                0U,
                header_bytes,
                header,
                COMPACT_SEGMENT_MEMORY,
            },
            error
        )) {
        free(header);
        return false;
    }
    header = NULL;
    if (!push_directory(
            builder,
            (compact_directory){
                volume->root_directory_sector,
                volume->root_directory_size,
                root_output_sector,
                0U,
            },
            error
        )) {
        return false;
    }
    for (index = 0U; index < builder->directory_count; ++index) {
        if (!process_directory(builder, index, error)) {
            return false;
        }
    }
    if (builder->next_sector > UINT64_MAX - 31U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "compact XISO aligned length overflows");
        return false;
    }
    aligned = (builder->next_sector + 31U) / 32U * 32U;
    if (aligned > builder->next_sector
        && !push_segment(
            builder,
            (compact_segment){
                builder->next_sector,
                aligned - builder->next_sector,
                0U,
                0U,
                NULL,
                COMPACT_SEGMENT_ZERO,
            },
            error
        )) {
        return false;
    }
    qsort(
        builder->segments,
        builder->segment_count,
        sizeof(*builder->segments),
        compare_segments
    );
    {
        uint64_t expected = 0U;
        for (index = 0U; index < builder->segment_count; ++index) {
            if (builder->segments[index].output_sector != expected
                || !add_u64(
                    expected,
                    builder->segments[index].sectors,
                    &expected
                )) {
                gdox_error_set(error, GDOX_ERROR_INTERNAL, "compact XISO layout is not contiguous");
                return false;
            }
        }
        if (expected != aligned) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "compact XISO layout has the wrong length");
            return false;
        }
    }
    *output_sectors = aligned;
    return true;
}

static uint64_t compact_sector_count(const void *raw_context)
{
    return ((const compact_context *)raw_context)->sectors;
}

static size_t find_segment(
    const compact_context *context,
    uint64_t sector
)
{
    size_t first = 0U;
    size_t count = context->segment_count;
    while (count != 0U) {
        const size_t step = count / 2U;
        const size_t middle = first + step;
        const compact_segment *segment = &context->segments[middle];
        if (segment->output_sector + segment->sectors <= sector) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

static bool compact_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    compact_context *context = raw_context;
    uint64_t current = lba;
    uint32_t remaining = blocks;
    size_t output_offset = 0U;
    (void)output_bytes;

    while (remaining != 0U) {
        const size_t index = find_segment(context, current);
        compact_segment *segment;
        uint64_t within;
        uint64_t available;
        uint32_t chunk;
        size_t chunk_bytes;
        if (index >= context->segment_count) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "compact XISO read reached a layout gap");
            return false;
        }
        segment = &context->segments[index];
        if (current < segment->output_sector) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "compact XISO read found a layout gap");
            return false;
        }
        within = current - segment->output_sector;
        available = segment->sectors - within;
        chunk = available < remaining ? (uint32_t)available : remaining;
        chunk_bytes = (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES;
        if (segment->kind == COMPACT_SEGMENT_MEMORY) {
            memcpy(
                output + output_offset,
                segment->memory
                    + (size_t)within * GDOX_LOGICAL_SECTOR_BYTES,
                chunk_bytes
            );
        } else if (segment->kind == COMPACT_SEGMENT_ZERO) {
            memset(output + output_offset, 0, chunk_bytes);
        } else {
            const uint64_t content_offset =
                within * GDOX_LOGICAL_SECTOR_BYTES;
            uint64_t valid_bytes = segment->content_bytes > content_offset
                ? segment->content_bytes - content_offset
                : 0U;
            if (!gdox_source_read(
                    &context->inner,
                    segment->source_sector + within,
                    chunk,
                    output + output_offset,
                    chunk_bytes,
                    error
                )) {
                return false;
            }
            if (valid_bytes < chunk_bytes) {
                memset(
                    output + output_offset + (size_t)valid_bytes,
                    0,
                    chunk_bytes - (size_t)valid_bytes
                );
            }
        }
        current += chunk;
        remaining -= chunk;
        output_offset += chunk_bytes;
    }
    return true;
}

static bool compact_media_present(const void *raw_context)
{
    const compact_context *context = raw_context;
    return gdox_source_media_present(&context->inner);
}

static bool compact_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const compact_context *context = raw_context;
    return gdox_source_evidence(&context->inner, output);
}

static bool compact_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const compact_context *context = raw_context;
    return gdox_source_physical_read_stats(&context->inner, output);
}

static bool compact_close(void *raw_context, gdox_error *error)
{
    compact_context *context = raw_context;
    const bool closed = gdox_source_close(&context->inner, error);
    destroy_segments(context->segments, context->segment_count);
    free(context);
    return closed;
}

static void compact_abort(void *raw_context)
{
    compact_context *context = raw_context;
    gdox_source_abort(&context->inner);
}

static const gdox_sector_source_ops compact_ops = {
    compact_sector_count,
    compact_read,
    compact_media_present,
    compact_close,
    compact_evidence,
    compact_physical_read_stats,
    compact_abort,
};

bool gdox_source_make_compact_xiso(
    gdox_sector_source *partition,
    const gdox_xdvdfs_volume *volume,
    gdox_sector_source *output,
    gdox_xdvdfs_compact_stats *stats,
    gdox_error *error
)
{
    compact_builder builder = {0};
    compact_context *context = NULL;
    uint64_t output_sectors = 0U;
    bool success = false;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(partition) || volume == NULL || output == NULL
        || output == partition || volume->base_lba != 0U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "compact XISO requires a partition-relative source and volume");
        return false;
    }
    builder.source = partition;
    if (!build_compact_layout(&builder, volume, &output_sectors, error)) {
        goto cleanup;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate compact XISO source");
        goto cleanup;
    }
    context->inner = *partition;
    context->segments = builder.segments;
    context->segment_count = builder.segment_count;
    context->sectors = output_sectors;
    partition->context = NULL;
    partition->ops = NULL;
    output->context = context;
    output->ops = &compact_ops;
    if (stats != NULL) {
        stats->input_sectors = gdox_source_sector_count(&context->inner);
        stats->output_sectors = output_sectors;
        stats->file_count = builder.file_count;
        stats->directory_count = builder.directory_count;
    }
    context = NULL;
    builder.segments = NULL;
    builder.segment_count = 0U;
    success = true;

cleanup:
    free(builder.directories);
    destroy_segments(builder.segments, builder.segment_count);
    free(context);
    return success;
}
