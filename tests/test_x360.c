#include "test.h"

#include "gdox/disc.h"
#include "gdox/error.h"
#include "gdox/sector.h"
#include "gdox/source.h"
#include "gdox/x360.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DESCRIPTOR_SECTOR UINT64_C(32)
#define TEST_ROOT_SECTOR UINT32_C(40)
#define TEST_XEX_SECTOR UINT32_C(50)
#define TEST_ROOT_SIZE UINT32_C(192)
#define TEST_SECOND_NODE UINT32_C(48)
#define TEST_THIRD_NODE UINT32_C(96)

typedef struct sparse_span {
    uint64_t offset;
    const uint8_t *data;
    size_t bytes;
} sparse_span;

typedef struct sparse_disc_context {
    uint64_t length;
    sparse_span spans[3];
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES];
    uint8_t root[TEST_ROOT_SIZE];
    uint8_t executable[128];
    uint64_t short_read_offset;
    uint64_t failed_read_offset;
    bool short_read_enabled;
    bool failed_read_enabled;
    bool failed_read_sets_error;
} sparse_disc_context;

typedef struct memory_source_context {
    uint8_t *data;
    uint64_t sectors;
    bool *closed;
    unsigned int prepare_failures;
} memory_source_context;

static const uint8_t test_gdfx_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

static void write_le_u16(uint8_t output[2], uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void write_le_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void write_be_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void set_entry(
    uint8_t *root,
    uint32_t offset,
    uint16_t left,
    uint16_t right,
    uint32_t sector,
    uint32_t size,
    uint8_t attributes,
    const char *name
)
{
    uint8_t *entry = root + offset;
    const size_t name_bytes = strlen(name);

    memset(entry, 0, TEST_SECOND_NODE);
    write_le_u16(entry, left);
    write_le_u16(entry + 2U, right);
    write_le_u32(entry + 4U, sector);
    write_le_u32(entry + 8U, size);
    entry[12] = attributes;
    entry[13] = (uint8_t)name_bytes;
    memcpy(entry + 14U, name, name_bytes);
}

static void overlay_span(
    uint64_t request_offset,
    uint8_t *output,
    size_t output_bytes,
    const sparse_span *span
)
{
    const uint64_t request_end = request_offset + output_bytes;
    const uint64_t span_end = span->offset + span->bytes;
    const uint64_t start = request_offset > span->offset
        ? request_offset
        : span->offset;
    const uint64_t end = request_end < span_end ? request_end : span_end;

    if (start < end) {
        memcpy(
            output + (size_t)(start - request_offset),
            span->data + (size_t)(start - span->offset),
            (size_t)(end - start)
        );
    }
}

static uint64_t sparse_length(const void *context)
{
    const sparse_disc_context *sparse = context;
    return sparse->length;
}

static bool sparse_read_at(
    void *context,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    sparse_disc_context *sparse = context;
    size_t readable;
    size_t index;

    if (sparse->failed_read_enabled
        && offset == sparse->failed_read_offset) {
        if (sparse->failed_read_sets_error) {
            gdox_error_set(error, GDOX_ERROR_IO, "injected read failure");
        }
        return false;
    }
    if (offset >= sparse->length || output_bytes == 0U) {
        *read_bytes = 0U;
        return true;
    }
    readable = sparse->length - offset < output_bytes
        ? (size_t)(sparse->length - offset)
        : output_bytes;
    if (sparse->short_read_enabled
        && offset == sparse->short_read_offset && readable != 0U) {
        --readable;
    }
    memset(output, 0, readable);
    for (index = 0U; index < sizeof(sparse->spans) / sizeof(sparse->spans[0]);
         ++index) {
        overlay_span(offset, output, readable, &sparse->spans[index]);
    }
    *read_bytes = readable;
    return true;
}

static bool sparse_close(void *context, gdox_error *error)
{
    (void)error;
    free(context);
    return true;
}

static const gdox_random_disc_ops sparse_ops = {
    sparse_length,
    sparse_read_at,
    NULL,
    sparse_close,
    NULL,
    NULL,
    NULL,
    NULL,
};

static bool make_sparse_disc(uint64_t game_offset, gdox_random_disc *disc)
{
    sparse_disc_context *context = calloc(1U, sizeof(*context));
    const uint64_t descriptor_offset = game_offset
        + TEST_DESCRIPTOR_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t root_offset = game_offset
        + (uint64_t)TEST_ROOT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t executable_offset = game_offset
        + (uint64_t)TEST_XEX_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;

    if (context == NULL) {
        return false;
    }
    memcpy(context->descriptor, test_gdfx_magic, sizeof(test_gdfx_magic));
    memcpy(
        context->descriptor + GDOX_LOGICAL_SECTOR_BYTES
            - sizeof(test_gdfx_magic),
        test_gdfx_magic,
        sizeof(test_gdfx_magic)
    );
    write_le_u32(context->descriptor + 20U, TEST_ROOT_SECTOR);
    write_le_u32(context->descriptor + 24U, TEST_ROOT_SIZE);
    set_entry(
        context->root,
        0U,
        0U,
        0U,
        TEST_XEX_SECTOR,
        sizeof(context->executable),
        0U,
        "DeFaUlT.XeX"
    );
    memcpy(context->executable, "XEX2", 4U);
    context->length = executable_offset + sizeof(context->executable);
    context->spans[0] = (sparse_span){
        descriptor_offset,
        context->descriptor,
        sizeof(context->descriptor),
    };
    context->spans[1] = (sparse_span){
        root_offset,
        context->root,
        sizeof(context->root),
    };
    context->spans[2] = (sparse_span){
        executable_offset,
        context->executable,
        sizeof(context->executable),
    };
    disc->context = context;
    disc->ops = &sparse_ops;
    return true;
}

static uint64_t descriptor_offset(const sparse_disc_context *context)
{
    return context->spans[0].offset;
}

static uint64_t root_offset(const sparse_disc_context *context)
{
    return context->spans[1].offset;
}

static void set_execution_info(
    sparse_disc_context *context,
    uint32_t media_id,
    uint32_t title_id,
    uint8_t disc_number,
    uint8_t disc_count
)
{
    write_be_u32(context->executable + 20U, 1U);
    write_be_u32(context->executable + 24U, UINT32_C(0x00040006));
    write_be_u32(context->executable + 28U, 48U);
    write_be_u32(context->executable + 48U, media_id);
    write_be_u32(context->executable + 60U, title_id);
    context->executable[64] = 2U;
    context->executable[65] = 0U;
    context->executable[66] = disc_number;
    context->executable[67] = disc_count;
}

static void test_recognized_layouts(void)
{
    static const uint64_t offsets[] = {
        UINT64_C(0x00000000),
        UINT64_C(0x0000fb20),
        UINT64_C(0x00020600),
        UINT64_C(0x02080000),
        UINT64_C(0x0fd90000),
    };
    static const gdox_x360_image_layout layouts[] = {
        GDOX_X360_IMAGE_LAYOUT_PARTITION,
        GDOX_X360_IMAGE_LAYOUT_FB20,
        GDOX_X360_IMAGE_LAYOUT_20600,
        GDOX_X360_IMAGE_LAYOUT_02080000,
        GDOX_X360_IMAGE_LAYOUT_0FD90000,
    };
    size_t index;

    for (index = 0U; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        gdox_random_disc disc = {0};
        gdox_x360_disc_info info;
        gdox_error error;

        GDOX_TEST_CHECK(make_sparse_disc(offsets[index], &disc));
        GDOX_TEST_CHECK(gdox_x360_disc_probe(&disc, &info, &error));
        GDOX_TEST_CHECK(info.layout == layouts[index]);
        GDOX_TEST_CHECK(info.executable == GDOX_X360_EXECUTABLE_XEX2);
        GDOX_TEST_CHECK(info.source_bytes == gdox_disc_length(&disc));
        GDOX_TEST_CHECK(info.game_offset_bytes == offsets[index]);
        GDOX_TEST_CHECK(info.root_directory_sector == TEST_ROOT_SECTOR);
        GDOX_TEST_CHECK(info.root_directory_size == TEST_ROOT_SIZE);
        GDOX_TEST_CHECK(strcmp(info.launch_executable, "default.xex") == 0);
        GDOX_TEST_CHECK(
            strcmp(
                gdox_x360_image_layout_name(info.layout),
                index == 0U ? "GDFX partition" :
                    index == 1U ? "GDFX at 0x0000fb20" :
                    index == 2U ? "GDFX at 0x00020600" :
                    index == 3U ? "GDFX at 0x02080000" :
                        "GDFX at 0x0fd90000"
            ) == 0
        );
        GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    }
    GDOX_TEST_CHECK(
        strcmp(
            gdox_x360_image_layout_name(GDOX_X360_IMAGE_LAYOUT_NONE),
            "Unknown Xbox 360 layout"
        ) == 0
    );
}

static void test_execution_information(void)
{
    gdox_random_disc disc = {0};
    sparse_disc_context *context;
    gdox_x360_disc_info info;
    gdox_error error;

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    set_execution_info(
        context,
        UINT32_C(0x11223344),
        UINT32_C(0x4d5307e6),
        1U,
        2U
    );
    GDOX_TEST_CHECK(gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(info.execution.valid);
    GDOX_TEST_CHECK(info.execution.media_id == UINT32_C(0x11223344));
    GDOX_TEST_CHECK(info.execution.title_id == UINT32_C(0x4d5307e6));
    GDOX_TEST_CHECK(info.execution.platform == 2U);
    GDOX_TEST_CHECK(info.execution.executable_type == 0U);
    GDOX_TEST_CHECK(info.execution.disc_number == 1U);
    GDOX_TEST_CHECK(info.execution.disc_count == 2U);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    memcpy(context->executable, "XEX1", 4U);
    set_execution_info(
        context,
        UINT32_C(0xaabbccdd),
        UINT32_C(0x12345678),
        1U,
        1U
    );
    GDOX_TEST_CHECK(gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(info.executable == GDOX_X360_EXECUTABLE_XEX1);
    GDOX_TEST_CHECK(info.execution.valid);
    GDOX_TEST_CHECK(info.execution.media_id == UINT32_C(0xaabbccdd));
    GDOX_TEST_CHECK(info.execution.title_id == UINT32_C(0x12345678));
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static void test_xex1_and_named_lookup(void)
{
    gdox_random_disc disc = {0};
    sparse_disc_context *context;
    gdox_x360_disc_info info;
    gdox_x360_executable_kind kind;
    gdox_x360_execution_info execution;
    gdox_error error;
    char unterminated[GDOX_X360_EXECUTABLE_NAME_CAPACITY];

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    memcpy(context->executable, "XEX1", 4U);
    write_le_u16(context->root, (uint16_t)(TEST_SECOND_NODE / 4U));
    set_entry(
        context->root,
        TEST_SECOND_NODE,
        0U,
        0U,
        TEST_XEX_SECTOR,
        sizeof(context->executable),
        0U,
        "launch.xex"
    );
    GDOX_TEST_CHECK(gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(info.executable == GDOX_X360_EXECUTABLE_XEX1);
    GDOX_TEST_CHECK(!info.execution.valid);
    GDOX_TEST_CHECK(gdox_x360_disc_find_executable(
        &disc,
        &info,
        "LaUnCh.XeX",
        &kind,
        &execution,
        &error
    ));
    GDOX_TEST_CHECK(kind == GDOX_X360_EXECUTABLE_XEX1);
    GDOX_TEST_CHECK(!execution.valid);
    GDOX_TEST_CHECK(!gdox_x360_disc_find_executable(
        &disc, &info, "folder/launch.xex", &kind, &execution, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(!gdox_x360_disc_find_executable(
        &disc, &info, "", &kind, &execution, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    memset(unterminated, 'a', sizeof(unterminated));
    GDOX_TEST_CHECK(!gdox_x360_disc_find_executable(
        &disc, &info, unterminated, &kind, &execution, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    --info.source_bytes;
    GDOX_TEST_CHECK(!gdox_x360_disc_find_executable(
        &disc, &info, "launch.xex", &kind, &execution, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static void test_directory_tree_validation(void)
{
    gdox_random_disc disc = {0};
    sparse_disc_context *context;
    gdox_x360_disc_info info;
    gdox_error error;

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    set_entry(
        context->root,
        0U,
        (uint16_t)(TEST_SECOND_NODE / 4U),
        0U,
        0U,
        0U,
        0U,
        "other.bin"
    );
    set_entry(
        context->root,
        TEST_SECOND_NODE,
        0U,
        0U,
        TEST_XEX_SECTOR,
        sizeof(context->executable),
        0U,
        "default.xex"
    );
    GDOX_TEST_CHECK(gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    set_entry(
        context->root,
        0U,
        (uint16_t)(TEST_SECOND_NODE / 4U),
        (uint16_t)(TEST_SECOND_NODE / 4U),
        0U,
        0U,
        0U,
        "other.bin"
    );
    set_entry(
        context->root,
        TEST_SECOND_NODE,
        0U,
        0U,
        0U,
        0U,
        0U,
        "also.bin"
    );
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    set_entry(
        context->root,
        0U,
        (uint16_t)(TEST_SECOND_NODE / 4U),
        0U,
        0U,
        0U,
        0U,
        "first.bin"
    );
    set_entry(
        context->root,
        TEST_SECOND_NODE,
        (uint16_t)(TEST_THIRD_NODE / 4U),
        0U,
        0U,
        0U,
        0U,
        "second.bin"
    );
    set_entry(
        context->root,
        TEST_THIRD_NODE,
        (uint16_t)(TEST_SECOND_NODE / 4U),
        0U,
        0U,
        0U,
        0U,
        "third.bin"
    );
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    set_entry(
        context->root,
        0U,
        UINT16_MAX,
        0U,
        0U,
        0U,
        0U,
        "other.bin"
    );
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static void test_directory_extent_validation(void)
{
    gdox_random_disc disc = {0};
    sparse_disc_context *context;
    gdox_x360_disc_info info;
    gdox_error error;

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_le_u32(context->descriptor + 24U, 0U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_le_u32(context->descriptor + 24U, 32U * 1024U * 1024U + 1U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_le_u32(context->descriptor + 20U, UINT32_MAX);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    context->root[13] = UINT8_MAX;
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    context->root[12] = 0x10U;
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static void test_executable_validation(void)
{
    gdox_random_disc disc = {0};
    sparse_disc_context *context;
    gdox_x360_disc_info info;
    gdox_error error;

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    memcpy(context->executable, "NOPE", 4U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_le_u32(context->root + 8U, 3U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_le_u32(context->root + 8U, 23U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_le_u32(context->root + 4U, UINT32_MAX);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_be_u32(context->executable + 20U, 4097U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    write_be_u32(context->executable + 20U, 1U);
    write_be_u32(context->executable + 24U, UINT32_C(0x00040006));
    write_be_u32(context->executable + 28U, 120U);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static void test_short_and_failed_reads(void)
{
    gdox_random_disc disc = {0};
    sparse_disc_context *context;
    gdox_x360_disc_info info;
    gdox_error error;

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    context->short_read_enabled = true;
    context->short_read_offset = root_offset(context);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    context->failed_read_enabled = true;
    context->failed_read_sets_error = false;
    context->failed_read_offset = descriptor_offset(context);
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    context = disc.context;
    context->descriptor[GDOX_LOGICAL_SECTOR_BYTES - 1U] ^= 1U;
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static uint64_t memory_sector_count(const void *context)
{
    const memory_source_context *memory = context;
    return memory->sectors;
}

static bool memory_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    memory_source_context *memory = context;

    if (!gdox_source_validate_read(
            memory->sectors,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    memcpy(
        output,
        memory->data + (size_t)lba * GDOX_LOGICAL_SECTOR_BYTES,
        output_bytes
    );
    return true;
}

static bool memory_close(void *context, gdox_error *error)
{
    memory_source_context *memory = context;

    (void)error;
    *memory->closed = true;
    free(memory->data);
    free(memory);
    return true;
}

static bool memory_prepare_close(void *context, gdox_error *error)
{
    memory_source_context *memory = context;

    if (memory->prepare_failures != 0U) {
        --memory->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "injected source close preparation failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops memory_source_ops = {
    memory_sector_count,
    memory_read,
    NULL,
    memory_close,
    NULL,
    NULL,
    NULL,
    memory_prepare_close,
    NULL,
};

static bool make_memory_source_at(
    bool valid_descriptor,
    uint64_t base_lba,
    bool *closed,
    gdox_sector_source *source
)
{
    memory_source_context *context = calloc(1U, sizeof(*context));
    uint8_t *descriptor;
    uint8_t *root;
    uint8_t *executable;

    if (context == NULL) {
        return false;
    }
    context->sectors = base_lba + 64U;
    context->closed = closed;
    context->data = calloc(
        (size_t)context->sectors,
        GDOX_LOGICAL_SECTOR_BYTES
    );
    if (context->data == NULL) {
        free(context);
        return false;
    }
    descriptor = context->data
        + (base_lba + TEST_DESCRIPTOR_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    root = context->data
        + (base_lba + TEST_ROOT_SECTOR) * GDOX_LOGICAL_SECTOR_BYTES;
    executable = context->data
        + (base_lba + TEST_XEX_SECTOR) * GDOX_LOGICAL_SECTOR_BYTES;
    if (valid_descriptor) {
        memcpy(descriptor, test_gdfx_magic, sizeof(test_gdfx_magic));
        memcpy(
            descriptor + GDOX_LOGICAL_SECTOR_BYTES
                - sizeof(test_gdfx_magic),
            test_gdfx_magic,
            sizeof(test_gdfx_magic)
        );
    }
    write_le_u32(descriptor + 20U, TEST_ROOT_SECTOR);
    write_le_u32(descriptor + 24U, TEST_ROOT_SIZE);
    set_entry(
        root,
        0U,
        0U,
        0U,
        TEST_XEX_SECTOR,
        128U,
        0U,
        "default.xex"
    );
    memcpy(executable, "XEX2", 4U);
    *closed = false;
    source->context = context;
    source->ops = &memory_source_ops;
    return true;
}

static bool make_memory_source(
    bool valid_descriptor,
    bool *closed,
    gdox_sector_source *source
)
{
    return make_memory_source_at(
        valid_descriptor, 0U, closed, source
    );
}

static void test_partition_only_live_disc(void)
{
    static const uint64_t base_lba = UINT64_C(17);
    gdox_sector_source whole = {0};
    gdox_sector_source partition = {0};
    gdox_random_disc disc = {0};
    gdox_x360_disc_info info;
    gdox_error error;
    bool closed;

    GDOX_TEST_CHECK(make_memory_source_at(
        true, base_lba, &closed, &whole
    ));
    GDOX_TEST_CHECK(gdox_source_sector_count(&whole) == base_lba + 64U);
    GDOX_TEST_CHECK(gdox_source_make_partition(
        &whole, base_lba, &partition, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&whole));
    GDOX_TEST_CHECK(gdox_source_sector_count(&partition) == 64U);
    GDOX_TEST_CHECK(gdox_x360_live_disc_build(
        &partition, &disc, &info, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&partition));
    GDOX_TEST_CHECK(!closed);
    GDOX_TEST_CHECK(info.layout == GDOX_X360_IMAGE_LAYOUT_PARTITION);
    GDOX_TEST_CHECK(info.game_offset_bytes == 0U);
    GDOX_TEST_CHECK(
        info.source_bytes == UINT64_C(64) * GDOX_LOGICAL_SECTOR_BYTES
    );
    GDOX_TEST_CHECK(gdox_disc_length(&disc) == info.source_bytes);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(closed);
}

static void test_live_disc_ownership(void)
{
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    gdox_x360_disc_info info;
    gdox_error error;
    bool closed;

    GDOX_TEST_CHECK(make_memory_source(true, &closed, &source));
    GDOX_TEST_CHECK(gdox_x360_live_disc_build(
        &source, &disc, &info, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(!closed);
    GDOX_TEST_CHECK(info.layout == GDOX_X360_IMAGE_LAYOUT_PARTITION);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(closed);

    GDOX_TEST_CHECK(make_memory_source(false, &closed, &source));
    GDOX_TEST_CHECK(!gdox_x360_live_disc_build(
        &source, &disc, &info, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    GDOX_TEST_CHECK(!gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(closed);

    GDOX_TEST_CHECK(make_memory_source(false, &closed, &source));
    {
        memory_source_context *context = source.context;
        context->prepare_failures = 1U;
        GDOX_TEST_CHECK(!gdox_x360_live_disc_build(
            &source, &disc, &info, &error
        ));
        GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
        GDOX_TEST_CHECK(!gdox_source_is_valid(&source));
        GDOX_TEST_CHECK(gdox_disc_is_valid(&disc));
        GDOX_TEST_CHECK(!closed);
        GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
        GDOX_TEST_CHECK(closed);
    }
}

static void test_invalid_arguments(void)
{
    gdox_random_disc disc = {0};
    gdox_x360_disc_info info;
    gdox_x360_executable_kind kind;
    gdox_x360_execution_info execution;
    gdox_error error;

    GDOX_TEST_CHECK(!gdox_x360_disc_probe(NULL, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(make_sparse_disc(0U, &disc));
    GDOX_TEST_CHECK(!gdox_x360_disc_probe(&disc, NULL, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(!gdox_x360_disc_find_executable(
        &disc, NULL, "default.xex", &kind, &execution, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

static void test_identity_comparisons(void)
{
    gdox_x360_disc_info left = {0};
    gdox_x360_disc_info right = {0};

    left.layout = GDOX_X360_IMAGE_LAYOUT_FB20;
    left.executable = GDOX_X360_EXECUTABLE_XEX2;
    left.source_bytes = UINT64_C(123456);
    left.game_offset_bytes = UINT64_C(0xfb20);
    left.root_directory_sector = 40U;
    left.root_directory_size = 192U;
    memcpy(left.launch_executable, "default.xex", sizeof("default.xex"));
    left.execution = (gdox_x360_execution_info){
        true,
        UINT32_C(0x68ec85bf),
        UINT32_C(0x555308c2),
        2U,
        3U,
        1U,
        2U,
    };
    right = left;
    GDOX_TEST_CHECK(gdox_x360_execution_info_equal(
        &left.execution, &right.execution
    ));
    GDOX_TEST_CHECK(gdox_x360_disc_info_equal(&left, &right));

    right.execution.platform = 1U;
    GDOX_TEST_CHECK(!gdox_x360_execution_info_equal(
        &left.execution, &right.execution
    ));
    GDOX_TEST_CHECK(!gdox_x360_disc_info_equal(&left, &right));
    right = left;
    right.execution.executable_type = 4U;
    GDOX_TEST_CHECK(!gdox_x360_execution_info_equal(
        &left.execution, &right.execution
    ));
    right = left;
    right.root_directory_size = 196U;
    GDOX_TEST_CHECK(!gdox_x360_disc_info_equal(&left, &right));
    GDOX_TEST_CHECK(!gdox_x360_disc_info_equal(NULL, &right));
}

void gdox_test_x360(void)
{
    test_recognized_layouts();
    test_execution_information();
    test_xex1_and_named_lookup();
    test_directory_tree_validation();
    test_directory_extent_validation();
    test_executable_validation();
    test_short_and_failed_reads();
    test_partition_only_live_disc();
    test_live_disc_ownership();
    test_invalid_arguments();
    test_identity_comparisons();
}
