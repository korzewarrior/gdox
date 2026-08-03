#include "gdox/x360.h"

#include "gdox/sector.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_X360_DESCRIPTOR_SECTOR UINT64_C(32)
#define GDOX_X360_DIRECTORY_ATTRIBUTE 0x10U
#define GDOX_X360_DIRECTORY_HEADER_BYTES 14U
#define GDOX_X360_MAX_ROOT_BYTES (32U * 1024U * 1024U)
#define GDOX_X360_MAX_ROOT_NODES 4096U
#define GDOX_X360_MAX_OPTIONAL_HEADERS 4096U
#define GDOX_X360_XEX_HEADER_BYTES 24U
#define GDOX_X360_EXECUTION_INFO_BYTES 24U

typedef struct layout_candidate {
    uint64_t offset;
    gdox_x360_image_layout layout;
} layout_candidate;

typedef struct directory_walk {
    uint8_t *visited;
    uint32_t *pending;
    size_t pending_count;
    size_t visited_nodes;
} directory_walk;

static const layout_candidate layout_candidates[] = {
    {UINT64_C(0x00000000), GDOX_X360_IMAGE_LAYOUT_PARTITION},
    {UINT64_C(0x0000fb20), GDOX_X360_IMAGE_LAYOUT_FB20},
    {UINT64_C(0x00020600), GDOX_X360_IMAGE_LAYOUT_20600},
    {UINT64_C(0x02080000), GDOX_X360_IMAGE_LAYOUT_02080000},
    {UINT64_C(0x0fd90000), GDOX_X360_IMAGE_LAYOUT_0FD90000},
};

static const uint8_t gdfx_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

bool gdox_x360_execution_info_equal(
    const gdox_x360_execution_info *left,
    const gdox_x360_execution_info *right
)
{
    return left != NULL && right != NULL
        && left->valid == right->valid
        && left->media_id == right->media_id
        && left->title_id == right->title_id
        && left->platform == right->platform
        && left->executable_type == right->executable_type
        && left->disc_number == right->disc_number
        && left->disc_count == right->disc_count;
}

bool gdox_x360_disc_info_equal(
    const gdox_x360_disc_info *left,
    const gdox_x360_disc_info *right
)
{
    return left != NULL && right != NULL
        && left->layout == right->layout
        && left->executable == right->executable
        && left->source_bytes == right->source_bytes
        && left->game_offset_bytes == right->game_offset_bytes
        && left->root_directory_sector == right->root_directory_sector
        && left->root_directory_size == right->root_directory_size
        && strncmp(
            left->launch_executable,
            right->launch_executable,
            GDOX_X360_EXECUTABLE_NAME_CAPACITY
        ) == 0
        && gdox_x360_execution_info_equal(
            &left->execution, &right->execution
        );
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool multiply_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (left != 0U && right > UINT64_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static uint16_t read_le_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8U);
}

static uint32_t read_le_u32(const uint8_t *data)
{
    return (uint32_t)data[0]
        | (uint32_t)data[1] << 8U
        | (uint32_t)data[2] << 16U
        | (uint32_t)data[3] << 24U;
}

static uint32_t read_be_u32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24U
        | (uint32_t)data[1] << 16U
        | (uint32_t)data[2] << 8U
        | (uint32_t)data[3];
}

static bool read_exact(
    gdox_random_disc *disc,
    uint64_t offset,
    uint8_t *output,
    size_t bytes,
    gdox_error *error
)
{
    const uint64_t length = gdox_disc_length(disc);
    size_t read_bytes = 0U;

    if (offset > length || (uint64_t)bytes > length - offset) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "Xbox 360 volume structure exceeds the source"
        );
        return false;
    }
    if (!gdox_disc_read_at(disc, offset, output, bytes, &read_bytes, error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not read Xbox 360 volume structure"
            );
        }
        return false;
    }
    if (read_bytes != bytes) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "Xbox 360 volume structure is truncated"
        );
        return false;
    }
    return true;
}

static uint8_t ascii_fold(uint8_t value)
{
    if (value >= (uint8_t)'A' && value <= (uint8_t)'Z') {
        return (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'));
    }
    return value;
}

static bool ascii_equal(
    const uint8_t *left,
    size_t left_bytes,
    const char *right,
    size_t right_bytes
)
{
    size_t index;

    if (left_bytes != right_bytes) {
        return false;
    }
    for (index = 0U; index < left_bytes; ++index) {
        if (ascii_fold(left[index])
            != ascii_fold((uint8_t)right[index])) {
            return false;
        }
    }
    return true;
}

static bool bounded_name_length(const char *name, size_t *bytes)
{
    size_t index;

    for (index = 0U; index < GDOX_X360_EXECUTABLE_NAME_CAPACITY; ++index) {
        if (name[index] == '\0') {
            *bytes = index;
            return index != 0U;
        }
    }
    return false;
}

static bool descriptor_magic_valid(const uint8_t *descriptor)
{
    return memcmp(descriptor, gdfx_magic, sizeof(gdfx_magic)) == 0
        && memcmp(
            descriptor + GDOX_LOGICAL_SECTOR_BYTES - sizeof(gdfx_magic),
            gdfx_magic,
            sizeof(gdfx_magic)
        ) == 0;
}

static bool find_descriptor(
    gdox_random_disc *disc,
    layout_candidate *candidate,
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES],
    gdox_error *error
)
{
    const uint64_t source_bytes = gdox_disc_length(disc);
    const uint64_t descriptor_relative =
        GDOX_X360_DESCRIPTOR_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    size_t index;

    for (index = 0U;
         index < sizeof(layout_candidates) / sizeof(layout_candidates[0]);
         ++index) {
        uint64_t descriptor_offset;

        if (!add_u64(
                layout_candidates[index].offset,
                descriptor_relative,
                &descriptor_offset
            )
            || descriptor_offset > source_bytes
            || GDOX_LOGICAL_SECTOR_BYTES > source_bytes - descriptor_offset) {
            continue;
        }
        if (!read_exact(
                disc,
                descriptor_offset,
                descriptor,
                GDOX_LOGICAL_SECTOR_BYTES,
                error
            )) {
            return false;
        }
        if (descriptor_magic_valid(descriptor)) {
            *candidate = layout_candidates[index];
            return true;
        }
    }
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        "Xenia-compatible GDFX volume was not found"
    );
    return false;
}

static bool read_executable_kind(
    gdox_random_disc *disc,
    uint64_t file_offset,
    uint32_t file_size,
    gdox_x360_executable_kind *kind,
    gdox_error *error
)
{
    uint8_t magic[4];

    if (file_size < sizeof(magic)
        || !read_exact(disc, file_offset, magic, sizeof(magic), error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_VOLUME,
                "Xbox 360 executable header is truncated"
            );
        }
        return false;
    }
    if (memcmp(magic, "XEX1", sizeof(magic)) == 0
        || memcmp(magic, "XEX2", sizeof(magic)) == 0) {
        if (file_size < GDOX_X360_XEX_HEADER_BYTES) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_VOLUME,
                "XEX header is truncated"
            );
            return false;
        }
        *kind = magic[3] == (uint8_t)'1'
            ? GDOX_X360_EXECUTABLE_XEX1
            : GDOX_X360_EXECUTABLE_XEX2;
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_VOLUME,
        "requested executable is not an XEX1 or XEX2 image"
    );
    return false;
}

static bool read_execution_info(
    gdox_random_disc *disc,
    uint64_t file_offset,
    uint32_t file_size,
    gdox_x360_executable_kind kind,
    gdox_x360_execution_info *execution,
    gdox_error *error
)
{
    uint8_t header[GDOX_X360_XEX_HEADER_BYTES];
    uint32_t header_count;
    uint32_t index;

    memset(execution, 0, sizeof(*execution));
    if (kind != GDOX_X360_EXECUTABLE_XEX1
        && kind != GDOX_X360_EXECUTABLE_XEX2) {
        return true;
    }
    if (!read_exact(disc, file_offset, header, sizeof(header), error)) {
        return false;
    }
    header_count = read_be_u32(header + 20U);
    if (header_count > GDOX_X360_MAX_OPTIONAL_HEADERS
        || (uint64_t)header_count * 8U
            > (uint64_t)file_size - sizeof(header)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "XEX2 optional-header table is invalid"
        );
        return false;
    }
    for (index = 0U; index < header_count; ++index) {
        uint8_t optional[8];
        uint32_t value_offset;
        uint64_t optional_table_offset;
        uint64_t optional_offset;
        uint64_t value_file_offset;

        if (!add_u64(
                file_offset,
                sizeof(header),
                &optional_table_offset
            )
            || !add_u64(
                optional_table_offset,
                (uint64_t)index * sizeof(optional),
                &optional_offset
            )
            || !read_exact(
                disc,
                optional_offset,
                optional,
                sizeof(optional),
                error
            )) {
            return false;
        }
        if (read_be_u32(optional) != UINT32_C(0x00040006)) {
            continue;
        }
        value_offset = read_be_u32(optional + 4U);
        if (value_offset > file_size
            || GDOX_X360_EXECUTION_INFO_BYTES > file_size - value_offset
            || !add_u64(file_offset, value_offset, &value_file_offset)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_VOLUME,
                "XEX2 execution information is outside the executable"
            );
            return false;
        }
        {
            uint8_t value[GDOX_X360_EXECUTION_INFO_BYTES];

            if (!read_exact(
                    disc,
                    value_file_offset,
                    value,
                    sizeof(value),
                    error
                )) {
                return false;
            }
            execution->media_id = read_be_u32(value);
            execution->title_id = read_be_u32(value + 12U);
            execution->platform = value[16];
            execution->executable_type = value[17];
            execution->disc_number = value[18];
            execution->disc_count = value[19];
            execution->valid = execution->title_id != 0U;
        }
        return true;
    }
    return true;
}

static bool directory_walk_create(
    uint32_t root_size,
    directory_walk *walk,
    gdox_error *error
)
{
    const size_t visited_bytes = ((size_t)root_size + 31U) / 32U;

    memset(walk, 0, sizeof(*walk));
    walk->visited = calloc(visited_bytes, 1U);
    walk->pending = malloc(
        (size_t)GDOX_X360_MAX_ROOT_NODES * sizeof(*walk->pending)
    );
    if (walk->visited == NULL || walk->pending == NULL) {
        free(walk->pending);
        free(walk->visited);
        memset(walk, 0, sizeof(*walk));
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Xbox 360 directory probe"
        );
        return false;
    }
    walk->pending[walk->pending_count++] = 0U;
    return true;
}

static void directory_walk_destroy(directory_walk *walk)
{
    free(walk->pending);
    free(walk->visited);
    memset(walk, 0, sizeof(*walk));
}

static bool directory_walk_visit(
    directory_walk *walk,
    uint32_t node_offset,
    uint32_t root_size,
    gdox_error *error
)
{
    const size_t slot = node_offset / 4U;
    const uint8_t mask = (uint8_t)(1U << (slot % 8U));

    if (node_offset % 4U != 0U || node_offset > root_size
        || GDOX_X360_DIRECTORY_HEADER_BYTES > root_size - node_offset) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "GDFX root directory entry is truncated"
        );
        return false;
    }
    if ((walk->visited[slot / 8U] & mask) != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "GDFX root directory contains a cycle or repeated node"
        );
        return false;
    }
    walk->visited[slot / 8U] |= mask;
    ++walk->visited_nodes;
    if (walk->visited_nodes > GDOX_X360_MAX_ROOT_NODES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "GDFX root directory probe limit exceeded"
        );
        return false;
    }
    return true;
}

static bool directory_walk_push(
    directory_walk *walk,
    uint16_t encoded_offset,
    gdox_error *error
)
{
    if (encoded_offset == 0U) {
        return true;
    }
    if (walk->pending_count >= GDOX_X360_MAX_ROOT_NODES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "GDFX root directory probe limit exceeded"
        );
        return false;
    }
    walk->pending[walk->pending_count++] = (uint32_t)encoded_offset * 4U;
    return true;
}

static bool find_root_executable(
    gdox_random_disc *disc,
    uint64_t game_offset,
    uint32_t root_sector,
    uint32_t root_size,
    const char *target_name,
    size_t target_name_bytes,
    gdox_x360_executable_kind *kind,
    gdox_x360_execution_info *execution,
    gdox_error *error
)
{
    const uint64_t source_bytes = gdox_disc_length(disc);
    uint64_t root_relative;
    uint64_t root_offset;
    directory_walk walk;
    bool success = false;

    if (root_size < GDOX_X360_DIRECTORY_HEADER_BYTES
        || root_size > GDOX_X360_MAX_ROOT_BYTES
        || !multiply_u64(root_sector, GDOX_LOGICAL_SECTOR_BYTES, &root_relative)
        || !add_u64(game_offset, root_relative, &root_offset)
        || root_offset > source_bytes
        || root_size > source_bytes - root_offset) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "GDFX root directory is outside the source"
        );
        return false;
    }
    if (!directory_walk_create(root_size, &walk, error)) {
        return false;
    }
    while (walk.pending_count != 0U) {
        uint8_t header[GDOX_X360_DIRECTORY_HEADER_BYTES];
        uint8_t name[UINT8_MAX];
        uint32_t node_offset = walk.pending[--walk.pending_count];
        uint16_t left;
        uint16_t right;
        uint32_t file_sector;
        uint32_t file_size;
        uint8_t attributes;
        uint8_t name_bytes;
        uint64_t node_file_offset;

        if (!directory_walk_visit(&walk, node_offset, root_size, error)
            || !add_u64(root_offset, node_offset, &node_file_offset)
            || !read_exact(
                disc,
                node_file_offset,
                header,
                sizeof(header),
                error
            )) {
            goto cleanup;
        }
        left = read_le_u16(header);
        right = read_le_u16(header + 2U);
        file_sector = read_le_u32(header + 4U);
        file_size = read_le_u32(header + 8U);
        attributes = header[12];
        name_bytes = header[13];
        if (name_bytes == 0U
            || name_bytes > root_size - node_offset - sizeof(header)
            || !read_exact(
                disc,
                node_file_offset + sizeof(header),
                name,
                name_bytes,
                error
            )) {
            if (!gdox_error_is_set(error)) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_INVALID_VOLUME,
                    "GDFX root directory name is invalid"
                );
            }
            goto cleanup;
        }
        if ((attributes & GDOX_X360_DIRECTORY_ATTRIBUTE) == 0U
            && ascii_equal(
                name,
                name_bytes,
                target_name,
                target_name_bytes
            )) {
            uint64_t file_relative;
            uint64_t file_offset;

            if (!multiply_u64(
                    file_sector,
                    GDOX_LOGICAL_SECTOR_BYTES,
                    &file_relative
                )
                || !add_u64(game_offset, file_relative, &file_offset)
                || file_offset > source_bytes
                || file_size > source_bytes - file_offset) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_INVALID_VOLUME,
                    "Xbox 360 executable extent is outside the source"
                );
                goto cleanup;
            }
            success = read_executable_kind(
                disc,
                file_offset,
                file_size,
                kind,
                error
            ) && read_execution_info(
                disc,
                file_offset,
                file_size,
                *kind,
                execution,
                error
            );
            goto cleanup;
        }
        if (!directory_walk_push(&walk, left, error)
            || !directory_walk_push(&walk, right, error)) {
            goto cleanup;
        }
    }
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "GDFX volume does not contain the requested executable"
    );

cleanup:
    directory_walk_destroy(&walk);
    return success;
}

static bool layout_matches_info(const gdox_x360_disc_info *info)
{
    size_t index;

    for (index = 0U;
         index < sizeof(layout_candidates) / sizeof(layout_candidates[0]);
         ++index) {
        if (layout_candidates[index].layout == info->layout
            && layout_candidates[index].offset == info->game_offset_bytes) {
            return true;
        }
    }
    return false;
}

bool gdox_x360_disc_probe(
    gdox_random_disc *disc,
    gdox_x360_disc_info *info,
    gdox_error *error
)
{
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES];
    layout_candidate candidate;
    gdox_x360_disc_info found = {0};
    static const char default_executable[] = "default.xex";

    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc) || info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open disc and Xbox 360 disc information are required"
        );
        return false;
    }
    if (!find_descriptor(disc, &candidate, descriptor, error)) {
        return false;
    }
    found.layout = candidate.layout;
    found.source_bytes = gdox_disc_length(disc);
    found.game_offset_bytes = candidate.offset;
    found.root_directory_sector = read_le_u32(descriptor + 20U);
    found.root_directory_size = read_le_u32(descriptor + 24U);
    if (!find_root_executable(
            disc,
            found.game_offset_bytes,
            found.root_directory_sector,
            found.root_directory_size,
            default_executable,
            sizeof(default_executable) - 1U,
            &found.executable,
            &found.execution,
            error
        )) {
        return false;
    }
    memcpy(
        found.launch_executable,
        default_executable,
        sizeof(default_executable)
    );
    *info = found;
    return true;
}

bool gdox_x360_disc_find_executable(
    gdox_random_disc *disc,
    const gdox_x360_disc_info *info,
    const char *name,
    gdox_x360_executable_kind *kind,
    gdox_x360_execution_info *execution,
    gdox_error *error
)
{
    gdox_x360_executable_kind found_kind = GDOX_X360_EXECUTABLE_NONE;
    gdox_x360_execution_info found_execution = {0};
    size_t name_bytes;

    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc) || info == NULL || name == NULL
        || kind == NULL || execution == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open disc, disc information, and executable name are required"
        );
        return false;
    }
    if (!bounded_name_length(name, &name_bytes)
        || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xbox 360 executable name is invalid"
        );
        return false;
    }
    if (info->source_bytes != gdox_disc_length(disc)
        || !layout_matches_info(info)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xbox 360 disc information does not match the open disc"
        );
        return false;
    }
    if (!find_root_executable(
            disc,
            info->game_offset_bytes,
            info->root_directory_sector,
            info->root_directory_size,
            name,
            name_bytes,
            &found_kind,
            &found_execution,
            error
        )) {
        return false;
    }
    *kind = found_kind;
    *execution = found_execution;
    return true;
}

bool gdox_x360_live_disc_build(
    gdox_sector_source *source,
    gdox_random_disc *output,
    gdox_x360_disc_info *info,
    gdox_error *error
)
{
    gdox_random_disc disc = {0};
    gdox_x360_disc_info found;
    gdox_error cleanup_error;
    bool success = false;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(source) || output == NULL
        || gdox_disc_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open source and empty Xbox 360 disc output are required"
        );
        return false;
    }
    if (!gdox_disc_from_source(source, &disc, error)
        || !gdox_x360_disc_probe(&disc, &found, error)) {
        goto cleanup;
    }
    if (info != NULL) {
        *info = found;
    }
    *output = disc;
    disc.context = NULL;
    disc.ops = NULL;
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        *error = cleanup_error;
        success = false;
        if (gdox_disc_is_valid(&disc)) {
            *output = disc;
            disc.context = NULL;
            disc.ops = NULL;
        }
    }
    return success;
}

const char *gdox_x360_image_layout_name(gdox_x360_image_layout layout)
{
    switch (layout) {
        case GDOX_X360_IMAGE_LAYOUT_PARTITION:
            return "GDFX partition";
        case GDOX_X360_IMAGE_LAYOUT_FB20:
            return "GDFX at 0x0000fb20";
        case GDOX_X360_IMAGE_LAYOUT_20600:
            return "GDFX at 0x00020600";
        case GDOX_X360_IMAGE_LAYOUT_02080000:
            return "GDFX at 0x02080000";
        case GDOX_X360_IMAGE_LAYOUT_0FD90000:
            return "GDFX at 0x0fd90000";
        case GDOX_X360_IMAGE_LAYOUT_NONE:
            break;
    }
    return "Unknown Xbox 360 layout";
}
