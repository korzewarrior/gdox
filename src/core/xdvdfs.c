#include "gdox/xdvdfs.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_XDVDFS_MAX_DIRECTORY_BYTES (64U * 1024U * 1024U)
#define GDOX_XDVDFS_MAX_XBE_BYTES (64U * 1024U * 1024U)
#define GDOX_XDVDFS_MAX_DIRECTORY_DEPTH 64U
#define GDOX_XDVDFS_MAX_DIRECTORIES ((size_t)16U * 1024U)
#define GDOX_XDVDFS_MAX_ENTRIES ((size_t)256U * 1024U)
#define GDOX_XDVDFS_SCAN_BATCH 256U

static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};
static const uint8_t xbe_media_check[8] = {
    0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
};

typedef struct entry_vector {
    gdox_xdvdfs_entry *items;
    size_t count;
    size_t capacity;
} entry_vector;

typedef struct offset_vector {
    size_t *items;
    size_t count;
    size_t capacity;
} offset_vector;

typedef struct directory_job {
    uint32_t start_sector;
    uint32_t size;
    char *parent_path;
    size_t depth;
} directory_job;

typedef struct directory_vector {
    directory_job *items;
    size_t count;
    size_t capacity;
} directory_vector;

typedef struct directory_identity {
    uint32_t start_sector;
    uint32_t size;
} directory_identity;

static bool read_le_u32(const uint8_t *data, size_t bytes, size_t offset, uint32_t *value)
{
    if (offset > bytes || bytes - offset < 4U) {
        return false;
    }
    *value = (uint32_t)data[offset]
        | (uint32_t)data[offset + 1U] << 8U
        | (uint32_t)data[offset + 2U] << 16U
        | (uint32_t)data[offset + 3U] << 24U;
    return true;
}

static bool read_le_u64(const uint8_t *data, size_t bytes, size_t offset, uint64_t *value)
{
    uint32_t low;
    uint32_t high;

    if (!read_le_u32(data, bytes, offset, &low)
        || !read_le_u32(data, bytes, offset + 4U, &high)) {
        return false;
    }
    *value = (uint64_t)low | (uint64_t)high << 32U;
    return true;
}

static bool descriptor_magic_valid(const uint8_t *sector)
{
    return memcmp(sector, xdvdfs_magic, sizeof(xdvdfs_magic)) == 0
        && memcmp(
            sector + GDOX_LOGICAL_SECTOR_BYTES - sizeof(xdvdfs_magic),
            xdvdfs_magic,
            sizeof(xdvdfs_magic)
        ) == 0;
}

static bool parse_descriptor(
    const uint8_t *sector,
    uint64_t descriptor_lba,
    gdox_xdvdfs_volume *volume,
    gdox_error *error
)
{
    uint32_t root_sector;
    uint32_t root_size;
    uint64_t timestamp;

    if (descriptor_lba < GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "XDVDFS descriptor appears before virtual sector 32"
        );
        return false;
    }
    if (!read_le_u32(sector, GDOX_LOGICAL_SECTOR_BYTES, 20U, &root_sector)
        || !read_le_u32(sector, GDOX_LOGICAL_SECTOR_BYTES, 24U, &root_size)
        || !read_le_u64(sector, GDOX_LOGICAL_SECTOR_BYTES, 28U, &timestamp)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "XDVDFS descriptor is truncated");
        return false;
    }
    if (root_size == 0U || root_size > GDOX_XDVDFS_MAX_DIRECTORY_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "XDVDFS root directory has an implausible size"
        );
        return false;
    }
    volume->base_lba = descriptor_lba - GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR;
    volume->root_directory_sector = root_sector;
    volume->root_directory_size = root_size;
    volume->image_timestamp = timestamp;
    return true;
}

static bool probe_descriptor(
    gdox_sector_source *source,
    uint64_t lba,
    gdox_xdvdfs_volume *volume,
    bool *found,
    gdox_error *error
)
{
    uint8_t sector[GDOX_LOGICAL_SECTOR_BYTES];

    *found = false;
    if (!gdox_source_read(
            source,
            lba,
            1U,
            sector,
            sizeof(sector),
            error
        )) {
        return false;
    }
    if (!descriptor_magic_valid(sector)) {
        return true;
    }
    if (!parse_descriptor(sector, lba, volume, error)) {
        return false;
    }
    *found = true;
    return true;
}

static bool inspect_descriptor_batch(
    const uint8_t *batch,
    uint32_t blocks,
    uint64_t lba,
    gdox_xdvdfs_volume *volume,
    bool *matched,
    gdox_error *error
)
{
    uint32_t index;

    *matched = false;
    for (index = 0U; index < blocks; ++index) {
        const uint8_t *sector =
            batch + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES;
        if (descriptor_magic_valid(sector)) {
            *matched = true;
            return parse_descriptor(sector, lba + index, volume, error);
        }
    }
    return true;
}

static bool probe_descriptor_batch(
    gdox_sector_source *source,
    uint64_t lba,
    uint32_t blocks,
    gdox_xdvdfs_volume *volume
)
{
    uint32_t index;

    for (index = 0U; index < blocks; ++index) {
        bool found = false;
        gdox_error probe_error;
        if (probe_descriptor(
            source,
            lba + index,
            volume,
            &found,
            &probe_error
        ) && found) {
            return true;
        }
    }
    return false;
}

static bool scan_range(
    gdox_sector_source *source,
    uint64_t start,
    uint64_t end,
    gdox_xdvdfs_volume *volume,
    bool *found,
    gdox_error *error
)
{
    uint8_t *batch;
    uint64_t lba = start;

    *found = false;
    batch = malloc((size_t)GDOX_XDVDFS_SCAN_BATCH * GDOX_LOGICAL_SECTOR_BYTES);
    if (batch == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate XDVDFS scan buffer");
        return false;
    }
    while (lba < end) {
        const uint64_t available = end - lba;
        const uint32_t blocks = available > GDOX_XDVDFS_SCAN_BATCH
            ? GDOX_XDVDFS_SCAN_BATCH
            : (uint32_t)available;
        const size_t bytes = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
        gdox_error read_error;

        if (gdox_source_read(source, lba, blocks, batch, bytes, &read_error)) {
            bool matched;
            const bool valid = inspect_descriptor_batch(
                batch,
                blocks,
                lba,
                volume,
                &matched,
                error
            );
            if (matched) {
                free(batch);
                *found = valid;
                return valid;
            }
        } else if (blocks > 1U
            && probe_descriptor_batch(source, lba, blocks, volume)) {
            free(batch);
            *found = true;
            return true;
        }
        lba += blocks;
    }
    free(batch);
    return true;
}

bool gdox_xdvdfs_find_volume(
    gdox_sector_source *source,
    gdox_xdvdfs_volume *volume,
    gdox_error *error
)
{
    const uint64_t quick_scan_sectors =
        UINT64_C(512) * 1024U * 1024U / GDOX_LOGICAL_SECTOR_BYTES;
    uint64_t sectors;
    uint64_t preferred[2];
    uint64_t quick_end;
    size_t index;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(source) || volume == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "source and volume are required");
        return false;
    }
    sectors = gdox_source_sector_count(source);
    preferred[0] = GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR;
    preferred[1] =
        GDOX_XDVDFS_COMMON_GAME_BASE_LBA + GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR;
    for (index = 0U; index < 2U; ++index) {
        bool found = false;
        gdox_error probe_error;
        if (preferred[index] < sectors
            && probe_descriptor(
                source,
                preferred[index],
                volume,
                &found,
                &probe_error
            )
            && found) {
            return true;
        }
    }

    quick_end = sectors < quick_scan_sectors ? sectors : quick_scan_sectors;
    if (quick_end != 0U) {
        bool found = false;
        if (!scan_range(source, 0U, quick_end, volume, &found, error)) {
            return false;
        }
        if (found) {
            return true;
        }
    }
    if (quick_end < sectors) {
        bool found = false;
        if (!scan_range(source, quick_end, sectors, volume, &found, error)) {
            return false;
        }
        if (found) {
            return true;
        }
    }
    gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "Xbox game partition was not found");
    return false;
}

static void entry_destroy(gdox_xdvdfs_entry *entry)
{
    free(entry->name);
    free(entry->path);
    memset(entry, 0, sizeof(*entry));
}

static void entry_vector_destroy(entry_vector *entries)
{
    size_t index;
    for (index = 0U; index < entries->count; ++index) {
        entry_destroy(&entries->items[index]);
    }
    free(entries->items);
    memset(entries, 0, sizeof(*entries));
}

static bool entry_vector_push(
    entry_vector *entries,
    gdox_xdvdfs_entry entry,
    gdox_error *error
)
{
    gdox_xdvdfs_entry *resized;
    size_t capacity;

    if (entries->count >= GDOX_XDVDFS_MAX_ENTRIES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "XDVDFS entry limit exceeded");
        return false;
    }
    if (entries->count == entries->capacity) {
        capacity = entries->capacity == 0U ? 16U : entries->capacity * 2U;
        if (capacity > GDOX_XDVDFS_MAX_ENTRIES) {
            capacity = GDOX_XDVDFS_MAX_ENTRIES;
        }
        resized = realloc(entries->items, capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate XDVDFS entries");
            return false;
        }
        entries->items = resized;
        entries->capacity = capacity;
    }
    entries->items[entries->count] = entry;
    ++entries->count;
    return true;
}

static bool offset_vector_push(offset_vector *offsets, size_t value, gdox_error *error)
{
    size_t *resized;
    size_t capacity;

    if (offsets->count == offsets->capacity) {
        capacity = offsets->capacity == 0U ? 16U : offsets->capacity * 2U;
        if (capacity > GDOX_XDVDFS_MAX_ENTRIES) {
            capacity = GDOX_XDVDFS_MAX_ENTRIES;
        }
        if (offsets->count == capacity) {
            gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "directory tree is too large");
            return false;
        }
        resized = realloc(offsets->items, capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate directory traversal");
            return false;
        }
        offsets->items = resized;
        offsets->capacity = capacity;
    }
    offsets->items[offsets->count] = value;
    ++offsets->count;
    return true;
}

static char *display_name(const uint8_t *bytes, size_t count)
{
    char *name = malloc(count + 1U);
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t value = bytes[index];
        name[index] = value >= 0x20U && value <= 0x7eU ? (char)value : '?';
    }
    name[count] = '\0';
    return name;
}

static char *join_path(const char *parent, const char *name)
{
    const size_t parent_bytes = strlen(parent);
    const size_t name_bytes = strlen(name);
    char *path;

    if (parent_bytes > SIZE_MAX - name_bytes - 2U) {
        return NULL;
    }
    path = malloc(parent_bytes + name_bytes + 2U);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, parent, parent_bytes);
    path[parent_bytes] = '/';
    memcpy(path + parent_bytes + 1U, name, name_bytes + 1U);
    return path;
}

typedef struct directory_reader {
    const uint8_t *data;
    uint32_t size;
    const char *parent;
    uint8_t *visited;
    offset_vector *pending;
    entry_vector *entries;
} directory_reader;

static bool directory_data_is_empty(const uint8_t *data, uint32_t size)
{
    size_t index;

    if (size < 14U) {
        return false;
    }
    for (index = 0U; index < 14U; ++index) {
        if (data[index] != 0xffU) {
            return false;
        }
    }
    return true;
}

static uint16_t directory_u16(const uint8_t *data, size_t offset)
{
    return (uint16_t)(
        (uint16_t)data[offset]
            | (uint16_t)((uint16_t)data[offset + 1U] << 8U)
    );
}

static bool process_directory_entry(
    directory_reader *reader,
    size_t offset,
    gdox_error *error
)
{
    gdox_xdvdfs_entry entry = {0};
    uint32_t entry_sector;
    uint32_t entry_size;
    uint16_t left;
    uint16_t right;
    size_t name_bytes;
    size_t slot;

    if (offset % 4U != 0U || offset > (size_t)reader->size
        || (size_t)reader->size - offset < 14U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "directory entry is truncated"
        );
        return false;
    }
    slot = offset / 4U;
    if (reader->visited[slot] != 0U) {
        return true;
    }
    reader->visited[slot] = 1U;
    left = directory_u16(reader->data, offset);
    right = directory_u16(reader->data, offset + 2U);
    if (!read_le_u32(
        reader->data,
        reader->size,
        offset + 4U,
        &entry_sector
    ) || !read_le_u32(
        reader->data,
        reader->size,
        offset + 8U,
        &entry_size
    )) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "directory entry is truncated"
        );
        return false;
    }
    entry.attributes = reader->data[offset + 12U];
    name_bytes = reader->data[offset + 13U];
    if (name_bytes == 0U
        || name_bytes > (size_t)reader->size - offset - 14U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "directory name is invalid"
        );
        return false;
    }
    entry.name = display_name(reader->data + offset + 14U, name_bytes);
    if (entry.name == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate directory name"
        );
        return false;
    }
    entry.path = join_path(reader->parent, entry.name);
    if (entry.path == NULL) {
        entry_destroy(&entry);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate directory path"
        );
        return false;
    }
    entry.start_sector = entry_sector;
    entry.size = entry_size;
    if (!entry_vector_push(reader->entries, entry, error)) {
        entry_destroy(&entry);
        return false;
    }
    if (left != 0U
        && !offset_vector_push(reader->pending, (size_t)left * 4U, error)) {
        return false;
    }
    return right == 0U
        || offset_vector_push(reader->pending, (size_t)right * 4U, error);
}

static bool read_directory(
    gdox_sector_source *source,
    const gdox_xdvdfs_volume *volume,
    uint32_t start_sector,
    uint32_t size,
    const char *parent,
    entry_vector *entries,
    gdox_error *error
)
{
    const uint32_t blocks =
        (size + GDOX_LOGICAL_SECTOR_BYTES - 1U) / GDOX_LOGICAL_SECTOR_BYTES;
    const size_t allocated = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *data;
    uint8_t *visited;
    size_t visited_bytes;
    offset_vector pending = {0};
    directory_reader reader;
    bool success = false;

    if (size == 0U || size > GDOX_XDVDFS_MAX_DIRECTORY_BYTES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "directory has an implausible size");
        return false;
    }
    data = malloc(allocated);
    if (data == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate directory data");
        return false;
    }
    if (!gdox_source_read(
            source,
            volume->base_lba + start_sector,
            blocks,
            data,
            allocated,
            error
        )) {
        free(data);
        return false;
    }
    if (directory_data_is_empty(data, size)) {
        free(data);
        return true;
    }

    visited_bytes = ((size_t)size + 3U) / 4U;
    visited = calloc(visited_bytes, 1U);
    if (visited == NULL) {
        free(data);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate directory index");
        return false;
    }
    if (!offset_vector_push(&pending, 0U, error)) {
        goto cleanup;
    }
    reader = (directory_reader){
        data,
        size,
        parent,
        visited,
        &pending,
        entries,
    };
    while (pending.count != 0U) {
        --pending.count;
        if (!process_directory_entry(
            &reader,
            pending.items[pending.count],
            error
        )) {
            goto cleanup;
        }
    }
    success = true;

cleanup:
    free(pending.items);
    free(visited);
    free(data);
    return success;
}

static bool is_directory(const gdox_xdvdfs_entry *entry)
{
    return (entry->attributes & 0x10U) != 0U;
}

static int ascii_case_compare(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        const unsigned char left_value = (unsigned char)*left;
        const unsigned char right_value = (unsigned char)*right;
        const int lower_left = tolower(left_value);
        const int lower_right = tolower(right_value);
        if (lower_left != lower_right) {
            return lower_left < lower_right ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == '\0' ? -1 : 1;
}

static bool has_xbe_extension(const char *name)
{
    const size_t length = strlen(name);
    return length >= 4U && ascii_case_compare(name + length - 4U, ".xbe") == 0;
}

static int compare_entries(const void *left_value, const void *right_value)
{
    const gdox_xdvdfs_entry *left = left_value;
    const gdox_xdvdfs_entry *right = right_value;
    return ascii_case_compare(left->path, right->path);
}

static bool directory_vector_push(
    directory_vector *directories,
    directory_job job,
    gdox_error *error
)
{
    directory_job *resized;
    size_t capacity;

    if (directories->count >= GDOX_XDVDFS_MAX_DIRECTORIES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "XDVDFS directory limit exceeded");
        return false;
    }
    if (directories->count == directories->capacity) {
        capacity = directories->capacity == 0U ? 16U : directories->capacity * 2U;
        if (capacity > GDOX_XDVDFS_MAX_DIRECTORIES) {
            capacity = GDOX_XDVDFS_MAX_DIRECTORIES;
        }
        resized = realloc(directories->items, capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate directory queue");
            return false;
        }
        directories->items = resized;
        directories->capacity = capacity;
    }
    directories->items[directories->count] = job;
    ++directories->count;
    return true;
}

static bool directory_seen(
    const directory_identity *visited,
    size_t count,
    uint32_t start_sector,
    uint32_t size
)
{
    size_t index;
    for (index = 0U; index < count; ++index) {
        if (visited[index].start_sector == start_sector && visited[index].size == size) {
            return true;
        }
    }
    return false;
}

static void directory_vector_destroy(directory_vector *directories, size_t first_owned)
{
    size_t index;
    for (index = first_owned; index < directories->count; ++index) {
        free(directories->items[index].parent_path);
    }
    free(directories->items);
    memset(directories, 0, sizeof(*directories));
}

typedef struct xbe_collection {
    gdox_sector_source *source;
    const gdox_xdvdfs_volume *volume;
    directory_vector directories;
    directory_identity *visited;
    size_t visited_count;
    entry_vector *xbes;
    uint64_t highest;
} xbe_collection;

static bool initialize_xbe_collection(
    xbe_collection *collection,
    gdox_error *error
)
{
    const uint64_t root_blocks =
        ((uint64_t)collection->volume->root_directory_size
            + GDOX_LOGICAL_SECTOR_BYTES - 1U)
        / GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t root_end =
        (uint64_t)collection->volume->root_directory_sector + root_blocks;
    directory_job root = {
        collection->volume->root_directory_sector,
        collection->volume->root_directory_size,
        NULL,
        0U,
    };

    if (root_end > collection->highest) {
        collection->highest = root_end;
    }
    root.parent_path = malloc(1U);
    if (root.parent_path == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate root path"
        );
        return false;
    }
    root.parent_path[0] = '\0';
    if (!directory_vector_push(&collection->directories, root, error)) {
        free(root.parent_path);
        return false;
    }
    collection->visited = malloc(
        GDOX_XDVDFS_MAX_DIRECTORIES * sizeof(*collection->visited)
    );
    if (collection->visited == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate directory identities"
        );
        return false;
    }
    return true;
}

static bool update_highest_entry(
    xbe_collection *collection,
    const gdox_xdvdfs_entry *entry,
    gdox_error *error
)
{
    const uint64_t entry_blocks =
        ((uint64_t)entry->size + GDOX_LOGICAL_SECTOR_BYTES - 1U)
        / GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t entry_end =
        (uint64_t)entry->start_sector + entry_blocks;

    if (entry_end
        > gdox_source_sector_count(collection->source)
            - collection->volume->base_lba) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "XDVDFS entry extent exceeds the source"
        );
        return false;
    }
    if (entry_end > collection->highest) {
        collection->highest = entry_end;
    }
    return true;
}

static bool enqueue_child_directory(
    xbe_collection *collection,
    gdox_xdvdfs_entry *entry,
    size_t parent_depth,
    gdox_error *error
)
{
    directory_job child;

    if (parent_depth >= GDOX_XDVDFS_MAX_DIRECTORY_DEPTH) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "XDVDFS directory nesting limit exceeded"
        );
        return false;
    }
    if (directory_seen(
        collection->visited,
        collection->visited_count,
        entry->start_sector,
        entry->size
    )) {
        return true;
    }
    collection->visited[collection->visited_count] = (directory_identity){
        entry->start_sector,
        entry->size,
    };
    ++collection->visited_count;
    child = (directory_job){
        entry->start_sector,
        entry->size,
        entry->path,
        parent_depth + 1U,
    };
    entry->path = NULL;
    if (!directory_vector_push(&collection->directories, child, error)) {
        free(child.parent_path);
        return false;
    }
    return true;
}

static bool collect_xbe_entry(
    xbe_collection *collection,
    gdox_xdvdfs_entry *entry,
    size_t parent_depth,
    gdox_error *error
)
{
    gdox_xdvdfs_entry moved;

    if (!update_highest_entry(collection, entry, error)) {
        return false;
    }
    if (is_directory(entry)) {
        return enqueue_child_directory(
            collection,
            entry,
            parent_depth,
            error
        );
    }
    if (!has_xbe_extension(entry->name)) {
        return true;
    }
    moved = *entry;
    memset(entry, 0, sizeof(*entry));
    if (!entry_vector_push(collection->xbes, moved, error)) {
        entry_destroy(&moved);
        return false;
    }
    return true;
}

static bool process_xbe_directory(
    xbe_collection *collection,
    size_t index,
    gdox_error *error
)
{
    directory_job *stored = &collection->directories.items[index];
    const size_t depth = stored->depth;
    entry_vector entries = {0};
    size_t entry_index;

    if (!read_directory(
        collection->source,
        collection->volume,
        stored->start_sector,
        stored->size,
        stored->parent_path,
        &entries,
        error
    )) {
        entry_vector_destroy(&entries);
        return false;
    }
    free(stored->parent_path);
    stored->parent_path = NULL;
    for (entry_index = 0U; entry_index < entries.count; ++entry_index) {
        if (!collect_xbe_entry(
            collection,
            &entries.items[entry_index],
            depth,
            error
        )) {
            entry_vector_destroy(&entries);
            return false;
        }
    }
    entry_vector_destroy(&entries);
    return true;
}

static bool collect_xbe_entries(
    gdox_sector_source *source,
    const gdox_xdvdfs_volume *volume,
    entry_vector *xbes,
    uint64_t *highest_used_sector,
    gdox_error *error
)
{
    xbe_collection collection = {
        .source = source,
        .volume = volume,
        .xbes = xbes,
        .highest = GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR + 1U,
    };
    size_t next = 0U;
    bool success = false;

    if (!initialize_xbe_collection(&collection, error)) {
        directory_vector_destroy(&collection.directories, 0U);
        return false;
    }
    while (next < collection.directories.count) {
        if (!process_xbe_directory(&collection, next, error)) {
            goto cleanup;
        }
        ++next;
    }
    if (xbes->count > 1U) {
        qsort(xbes->items, xbes->count, sizeof(*xbes->items), compare_entries);
    }
    if (highest_used_sector != NULL) {
        *highest_used_sector = collection.highest;
    }
    success = true;

cleanup:
    free(collection.visited);
    directory_vector_destroy(&collection.directories, next);
    return success;
}

static bool append_utf8(char *output, size_t capacity, size_t *length, uint32_t codepoint)
{
    size_t needed;

    if (codepoint <= 0x7fU) {
        needed = 1U;
    } else if (codepoint <= 0x7ffU) {
        needed = 2U;
    } else if (codepoint <= 0xffffU) {
        needed = 3U;
    } else {
        needed = 4U;
    }
    if (*length > capacity || capacity - *length <= needed) {
        return false;
    }
    if (needed == 1U) {
        output[(*length)++] = (char)codepoint;
    } else if (needed == 2U) {
        output[(*length)++] = (char)(0xc0U | (codepoint >> 6U));
        output[(*length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else if (needed == 3U) {
        output[(*length)++] = (char)(0xe0U | (codepoint >> 12U));
        output[(*length)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[(*length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else {
        output[(*length)++] = (char)(0xf0U | (codepoint >> 18U));
        output[(*length)++] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
        output[(*length)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[(*length)++] = (char)(0x80U | (codepoint & 0x3fU));
    }
    output[*length] = '\0';
    return true;
}

static char *decode_title(const uint8_t *title_bytes)
{
    char *title = calloc(161U, 1U);
    size_t output_length = 0U;
    size_t unit = 0U;
    size_t first;

    if (title == NULL) {
        return NULL;
    }
    while (unit < 40U) {
        uint32_t codepoint =
            (uint32_t)title_bytes[unit * 2U]
            | (uint32_t)title_bytes[unit * 2U + 1U] << 8U;
        ++unit;
        if (codepoint == 0U) {
            break;
        }
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU && unit < 40U) {
            const uint32_t low =
                (uint32_t)title_bytes[unit * 2U]
                | (uint32_t)title_bytes[unit * 2U + 1U] << 8U;
            if (low >= 0xdc00U && low <= 0xdfffU) {
                codepoint =
                    0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
                ++unit;
            } else {
                codepoint = 0xfffdU;
            }
        } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
            codepoint = 0xfffdU;
        }
        if (!append_utf8(title, 161U, &output_length, codepoint)) {
            free(title);
            return NULL;
        }
    }

    first = 0U;
    while (first < output_length
        && (unsigned char)title[first] < 0x80U
        && isspace((unsigned char)title[first]) != 0) {
        ++first;
    }
    while (output_length > first
        && (unsigned char)title[output_length - 1U] < 0x80U
        && isspace((unsigned char)title[output_length - 1U]) != 0) {
        --output_length;
    }
    if (first != 0U) {
        memmove(title, title + first, output_length - first);
        output_length -= first;
    }
    title[output_length] = '\0';
    if (output_length == 0U) {
        free(title);
        return NULL;
    }
    return title;
}

static bool read_xbe_title(
    gdox_sector_source *source,
    uint64_t volume_base,
    const gdox_xdvdfs_entry *entry,
    char **title,
    bool *title_id_present,
    uint32_t *title_id,
    gdox_error *error
)
{
    uint8_t header[GDOX_LOGICAL_SECTOR_BYTES];
    uint32_t image_base;
    uint32_t certificate_address;
    uint64_t certificate_offset;
    uint64_t certificate_lba;
    size_t within;
    uint32_t blocks;
    uint8_t certificate[2U * GDOX_LOGICAL_SECTOR_BYTES];

    *title = NULL;
    *title_id_present = false;
    if (entry->size < GDOX_LOGICAL_SECTOR_BYTES
        || !gdox_source_read(
            source,
            volume_base + entry->start_sector,
            1U,
            header,
            sizeof(header),
            error
        )) {
        return entry->size < GDOX_LOGICAL_SECTOR_BYTES;
    }
    if (memcmp(header, "XBEH", 4U) != 0
        || !read_le_u32(header, sizeof(header), 0x104U, &image_base)
        || !read_le_u32(header, sizeof(header), 0x118U, &certificate_address)
        || certificate_address < image_base) {
        return true;
    }
    certificate_offset = (uint64_t)(certificate_address - image_base);
    if (certificate_offset > entry->size || entry->size - certificate_offset < 92U) {
        return true;
    }
    certificate_lba =
        volume_base + entry->start_sector
        + certificate_offset / GDOX_LOGICAL_SECTOR_BYTES;
    within = (size_t)(certificate_offset % GDOX_LOGICAL_SECTOR_BYTES);
    blocks = within + 92U > GDOX_LOGICAL_SECTOR_BYTES ? 2U : 1U;
    if (!gdox_source_read(
            source,
            certificate_lba,
            blocks,
            certificate,
            (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES,
            error
        )) {
        return false;
    }
    if (!read_le_u32(certificate, sizeof(certificate), within + 8U, title_id)) {
        return true;
    }
    *title_id_present = true;
    *title = decode_title(certificate + within + 12U);
    return true;
}

bool gdox_xdvdfs_inspect(
    gdox_sector_source *source,
    const gdox_xdvdfs_volume *volume,
    gdox_xdvdfs_metadata *metadata,
    gdox_error *error
)
{
    entry_vector xbes = {0};
    size_t index;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(source) || volume == NULL || metadata == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "source, volume, and metadata are required");
        return false;
    }
    memset(metadata, 0, sizeof(*metadata));
    metadata->default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    if (!collect_xbe_entries(source, volume, &xbes, NULL, error)) {
        entry_vector_destroy(&xbes);
        return false;
    }
    metadata->volume = *volume;
    metadata->xbe_files = xbes.items;
    metadata->xbe_file_count = xbes.count;
    for (index = 0U; index < metadata->xbe_file_count; ++index) {
        if (ascii_case_compare(metadata->xbe_files[index].path, "/default.xbe") == 0) {
            metadata->default_xbe_index = index;
            break;
        }
    }
    if (metadata->default_xbe_index != GDOX_XDVDFS_NO_ENTRY
        && !read_xbe_title(
            source,
            volume->base_lba,
            &metadata->xbe_files[metadata->default_xbe_index],
            &metadata->title,
            &metadata->title_id_present,
            &metadata->title_id,
            error
        )) {
        gdox_xdvdfs_metadata_destroy(metadata);
        return false;
    }
    return true;
}

bool gdox_xdvdfs_measure_trimmed_sectors(
    gdox_sector_source *partition,
    const gdox_xdvdfs_volume *volume,
    uint64_t *sectors,
    gdox_error *error
)
{
    entry_vector xbes = {0};
    uint64_t highest = 0U;
    uint64_t aligned;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(partition) || volume == NULL || sectors == NULL
        || volume->base_lba >= gdox_source_sector_count(partition)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "partition, volume, and output are required");
        return false;
    }
    if (!collect_xbe_entries(
            partition,
            volume,
            &xbes,
            &highest,
            error
        )) {
        entry_vector_destroy(&xbes);
        return false;
    }
    entry_vector_destroy(&xbes);
    if (highest > UINT64_MAX - 31U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "trimmed XISO length overflows");
        return false;
    }
    aligned = (highest + 31U) / 32U * 32U;
    if (aligned == 0U
        || aligned > gdox_source_sector_count(partition) - volume->base_lba) {
        gdox_error_set(error, GDOX_ERROR_INVALID_VOLUME, "trimmed XISO length exceeds the source");
        return false;
    }
    *sectors = aligned;
    return true;
}

void gdox_xdvdfs_metadata_destroy(gdox_xdvdfs_metadata *metadata)
{
    size_t index;

    if (metadata == NULL) {
        return;
    }
    free(metadata->title);
    for (index = 0U; index < metadata->xbe_file_count; ++index) {
        entry_destroy(&metadata->xbe_files[index]);
    }
    free(metadata->xbe_files);
    memset(metadata, 0, sizeof(*metadata));
    metadata->default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
}

static bool patch_vector_push(
    gdox_byte_patch **patches,
    size_t *count,
    size_t *capacity,
    gdox_byte_patch patch,
    gdox_error *error
)
{
    gdox_byte_patch *resized;
    size_t next_capacity;

    if (*count == *capacity) {
        next_capacity = *capacity == 0U ? 8U : *capacity * 2U;
        if (next_capacity < *capacity
            || next_capacity > SIZE_MAX / sizeof(**patches)) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "media patch list is too large");
            return false;
        }
        resized = realloc(*patches, next_capacity * sizeof(*resized));
        if (resized == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate media patches");
            return false;
        }
        *patches = resized;
        *capacity = next_capacity;
    }
    (*patches)[*count] = patch;
    ++*count;
    return true;
}

bool gdox_xdvdfs_collect_media_patches(
    gdox_sector_source *whole_disc,
    const gdox_xdvdfs_metadata *metadata,
    gdox_byte_patch **patches,
    size_t *patch_count,
    gdox_error *error
)
{
    size_t capacity = 0U;
    size_t entry_index;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(whole_disc) || metadata == NULL
        || patches == NULL || patch_count == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "source, metadata, and outputs are required");
        return false;
    }
    *patches = NULL;
    *patch_count = 0U;
    for (entry_index = 0U; entry_index < metadata->xbe_file_count; ++entry_index) {
        const gdox_xdvdfs_entry *entry = &metadata->xbe_files[entry_index];
        uint32_t blocks;
        size_t allocated;
        uint8_t *data;
        size_t offset;

        if (entry->size < sizeof(xbe_media_check)
            || entry->size > GDOX_XDVDFS_MAX_XBE_BYTES) {
            continue;
        }
        blocks =
            (entry->size + GDOX_LOGICAL_SECTOR_BYTES - 1U)
            / GDOX_LOGICAL_SECTOR_BYTES;
        allocated = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
        data = malloc(allocated);
        if (data == NULL) {
            free(*patches);
            *patches = NULL;
            *patch_count = 0U;
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate XBE scan data");
            return false;
        }
        if (!gdox_source_read(
                whole_disc,
                metadata->volume.base_lba + entry->start_sector,
                blocks,
                data,
                allocated,
                error
            )) {
            free(data);
            free(*patches);
            *patches = NULL;
            *patch_count = 0U;
            return false;
        }
        if (memcmp(data, "XBEH", 4U) == 0) {
            for (offset = 0U; offset + sizeof(xbe_media_check) <= entry->size; ++offset) {
                if (memcmp(data + offset, xbe_media_check, sizeof(xbe_media_check)) == 0) {
                    gdox_byte_patch patch;
                    patch.offset =
                        (uint64_t)entry->start_sector * GDOX_LOGICAL_SECTOR_BYTES
                        + offset + 7U;
                    patch.value = 0xebU;
                    if (!patch_vector_push(
                            patches,
                            patch_count,
                            &capacity,
                            patch,
                            error
                        )) {
                        free(data);
                        free(*patches);
                        *patches = NULL;
                        *patch_count = 0U;
                        return false;
                    }
                }
            }
        }
        free(data);
    }
    return true;
}
