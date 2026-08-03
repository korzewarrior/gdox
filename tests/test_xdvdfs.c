#include "test.h"

#include "core/compact.h"
#include "gdox/disc.h"
#include "gdox/live.h"
#include "gdox/media.h"
#include "gdox/source.h"
#include "gdox/xdvdfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_BASE_LBA UINT64_C(91)
#define FIXTURE_SECTORS UINT64_C(256)
#define FIXTURE_ROOT_SECTOR UINT32_C(64)
#define FIXTURE_XBE_SECTOR UINT32_C(128)
#define FIXTURE_AUX_XBE_SECTOR UINT32_C(130)
#define FIXTURE_MEDIA_OFFSET 0x380U
#define FIXTURE_AUX_MEDIA_OFFSET 0x500U

typedef struct memory_context {
    uint8_t *bytes;
    uint64_t sectors;
    uint64_t read_sectors;
    gdox_error_code read_failure;
    unsigned int prepare_failures;
} memory_context;

static uint64_t memory_sector_count(const void *context)
{
    const memory_context *memory = context;
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
    memory_context *memory = context;
    if (memory->read_failure != GDOX_ERROR_NONE) {
        gdox_error_set(
            error, memory->read_failure, "synthetic identification failure"
        );
        return false;
    }
    if (!gdox_source_validate_read(
            memory->sectors,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    memory->read_sectors += blocks;
    memcpy(
        output,
        memory->bytes + (size_t)lba * GDOX_LOGICAL_SECTOR_BYTES,
        output_bytes
    );
    return true;
}

static bool memory_present(const void *context)
{
    (void)context;
    return true;
}

static bool memory_close(void *context, gdox_error *error)
{
    gdox_error_clear(error);
    free(context);
    return true;
}

static bool memory_prepare_close(void *context, gdox_error *error)
{
    memory_context *memory = context;

    if (memory->prepare_failures != 0U) {
        --memory->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "synthetic close preparation failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops memory_ops = {
    memory_sector_count,
    memory_read,
    memory_present,
    memory_close,
    NULL,
    NULL,
    NULL,
    memory_prepare_close,
    NULL,
};

static bool make_memory_source(
    uint8_t *bytes,
    uint64_t sectors,
    gdox_sector_source *source
)
{
    memory_context *context = malloc(sizeof(*context));
    if (context == NULL) {
        return false;
    }
    context->bytes = bytes;
    context->sectors = sectors;
    context->read_sectors = 0U;
    context->read_failure = GDOX_ERROR_NONE;
    context->prepare_failures = 0U;
    source->context = context;
    source->ops = &memory_ops;
    return true;
}

static void put_le_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xffU);
    output[1] = (uint8_t)(value >> 8U);
}

static void put_le_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xffU);
    output[1] = (uint8_t)((value >> 8U) & 0xffU);
    output[2] = (uint8_t)((value >> 16U) & 0xffU);
    output[3] = (uint8_t)(value >> 24U);
}

static void put_le_u64(uint8_t *output, uint64_t value)
{
    put_le_u32(output, (uint32_t)(value & UINT64_C(0xffffffff)));
    put_le_u32(output + 4U, (uint32_t)(value >> 32U));
}

static uint8_t *fixture_create(uint64_t *sector_count)
{
    static const uint8_t magic[20] = {
        'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
        'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
    };
    static const uint8_t media_check[8] = {
        0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
    };
    const uint64_t sectors = FIXTURE_BASE_LBA + FIXTURE_SECTORS;
    uint8_t *image = calloc((size_t)sectors, GDOX_LOGICAL_SECTOR_BYTES);
    uint8_t *descriptor;
    uint8_t *root;
    uint8_t *xbe;
    uint8_t *auxiliary;
    const char *title = "Synthetic Halo";
    size_t index;

    if (image == NULL) {
        return NULL;
    }
    descriptor = image
        + (size_t)(FIXTURE_BASE_LBA + GDOX_XDVDFS_VOLUME_DESCRIPTOR_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    memcpy(descriptor, magic, sizeof(magic));
    put_le_u32(descriptor + 20U, FIXTURE_ROOT_SECTOR);
    put_le_u32(descriptor + 24U, 64U);
    put_le_u64(descriptor + 28U, UINT64_C(132537600000000000));
    memcpy(
        descriptor + GDOX_LOGICAL_SECTOR_BYTES - sizeof(magic),
        magic,
        sizeof(magic)
    );

    root = image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_ROOT_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    put_le_u16(root + 2U, 8U);
    put_le_u32(root + 4U, FIXTURE_XBE_SECTOR);
    put_le_u32(root + 8U, 2U * GDOX_LOGICAL_SECTOR_BYTES);
    root[12] = 0x20U;
    root[13] = 11U;
    memcpy(root + 14U, "default.xbe", 11U);
    put_le_u32(root + 32U + 4U, FIXTURE_AUX_XBE_SECTOR);
    put_le_u32(root + 32U + 8U, GDOX_LOGICAL_SECTOR_BYTES);
    root[32U + 12U] = 0x20U;
    root[32U + 13U] = 8U;
    memcpy(root + 32U + 14U, "game.xbe", 8U);
    root[64U] = 0xa5U;

    xbe = image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_XBE_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    memcpy(xbe, "XBEH", 4U);
    put_le_u32(xbe + 0x104U, UINT32_C(0x00010000));
    put_le_u32(xbe + 0x108U, GDOX_LOGICAL_SECTOR_BYTES);
    put_le_u32(xbe + 0x10cU, 2U * GDOX_LOGICAL_SECTOR_BYTES);
    put_le_u32(xbe + 0x118U, UINT32_C(0x00010200));
    put_le_u32(xbe + 0x200U, UINT32_C(0x1d0));
    put_le_u32(xbe + 0x208U, UINT32_C(0x4d530001));
    for (index = 0U; title[index] != '\0'; ++index) {
        put_le_u16(xbe + 0x20cU + index * 2U, (uint16_t)(unsigned char)title[index]);
    }
    memcpy(xbe + FIXTURE_MEDIA_OFFSET, media_check, sizeof(media_check));

    auxiliary = image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_AUX_XBE_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    memcpy(auxiliary, "XBEH", 4U);
    memcpy(auxiliary + FIXTURE_AUX_MEDIA_OFFSET, media_check, sizeof(media_check));
    *sector_count = sectors;
    return image;
}

static void test_fixture_metadata_and_overlay(void)
{
    uint64_t sectors = 0U;
    uint8_t *image = fixture_create(&sectors);
    gdox_sector_source whole = {0};
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_metadata metadata;
    gdox_byte_patch *patches = NULL;
    size_t patch_count = 0U;
    gdox_sector_source partition = {0};
    gdox_sector_source patched = {0};
    gdox_sector_source compact = {0};
    gdox_xdvdfs_volume partition_volume;
    gdox_xdvdfs_volume compact_volume;
    gdox_xdvdfs_metadata compact_metadata;
    gdox_xdvdfs_compact_stats compact_stats;
    uint64_t trimmed_sectors = 0U;
    uint8_t xbe_sector[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    GDOX_TEST_CHECK(make_memory_source(image, sectors, &whole));
    GDOX_TEST_CHECK(gdox_xdvdfs_find_volume(&whole, &volume, &error));
    GDOX_TEST_CHECK(volume.base_lba == FIXTURE_BASE_LBA);
    GDOX_TEST_CHECK(volume.root_directory_sector == FIXTURE_ROOT_SECTOR);
    GDOX_TEST_CHECK(gdox_xdvdfs_inspect(&whole, &volume, &metadata, &error));
    GDOX_TEST_CHECK(metadata.xbe_file_count == 2U);
    GDOX_TEST_CHECK(metadata.file_extent_count == 2U);
    GDOX_TEST_CHECK(metadata.file_extents[0].start_sector == FIXTURE_XBE_SECTOR);
    GDOX_TEST_CHECK(metadata.file_extents[0].sector_count == 2U);
    GDOX_TEST_CHECK(metadata.file_extents[0].prefix_max_end == 130U);
    GDOX_TEST_CHECK(metadata.file_extents[1].start_sector == FIXTURE_AUX_XBE_SECTOR);
    GDOX_TEST_CHECK(metadata.file_extents[1].sector_count == 1U);
    GDOX_TEST_CHECK(metadata.file_extents[1].prefix_max_end == 131U);
    GDOX_TEST_CHECK(metadata.default_xbe_index == 0U);
    GDOX_TEST_CHECK(strcmp(metadata.xbe_files[0].path, "/default.xbe") == 0);
    GDOX_TEST_CHECK(strcmp(metadata.xbe_files[1].path, "/game.xbe") == 0);
    GDOX_TEST_CHECK(metadata.title != NULL);
    GDOX_TEST_CHECK(strcmp(metadata.title, "Synthetic Halo") == 0);
    GDOX_TEST_CHECK(metadata.title_id_present);
    GDOX_TEST_CHECK(metadata.title_id == UINT32_C(0x4d530001));
    GDOX_TEST_CHECK(
        gdox_xdvdfs_collect_media_patches(
            &whole,
            &metadata,
            &patches,
            &patch_count,
            &error
        )
    );
    GDOX_TEST_CHECK(patch_count == 2U);
    GDOX_TEST_CHECK(
        patches[0].offset
        == (uint64_t)FIXTURE_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES
            + FIXTURE_MEDIA_OFFSET + 7U
    );
    GDOX_TEST_CHECK(
        patches[1].offset
        == (uint64_t)FIXTURE_AUX_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES
            + FIXTURE_AUX_MEDIA_OFFSET + 7U
    );
    GDOX_TEST_CHECK(
        gdox_source_make_partition(&whole, volume.base_lba, &partition, &error)
    );
    GDOX_TEST_CHECK(
        gdox_source_make_patched(
            &partition,
            patches,
            patch_count,
            &patched,
            &error
        )
    );
    partition_volume = volume;
    partition_volume.base_lba = 0U;
    GDOX_TEST_CHECK(gdox_xdvdfs_measure_trimmed_sectors(
        &patched,
        &partition_volume,
        &trimmed_sectors,
        &error
    ));
    GDOX_TEST_CHECK(trimmed_sectors == 160U);
    GDOX_TEST_CHECK(
        gdox_source_read(
            &patched,
            FIXTURE_XBE_SECTOR,
            1U,
            xbe_sector,
            sizeof(xbe_sector),
            &error
        )
    );
    GDOX_TEST_CHECK(xbe_sector[FIXTURE_MEDIA_OFFSET + 7U] == 0xebU);
    GDOX_TEST_CHECK(
        image[
            (size_t)(FIXTURE_BASE_LBA + FIXTURE_XBE_SECTOR)
                * GDOX_LOGICAL_SECTOR_BYTES
                + FIXTURE_MEDIA_OFFSET + 7U
        ] == 0x7dU
    );
    GDOX_TEST_CHECK(gdox_source_make_compact_xiso(
        &patched,
        &partition_volume,
        &compact,
        &compact_stats,
        &error
    ));
    GDOX_TEST_CHECK(compact_stats.input_sectors == FIXTURE_SECTORS);
    GDOX_TEST_CHECK(compact_stats.output_sectors == 64U);
    GDOX_TEST_CHECK(compact_stats.file_count == 2U);
    GDOX_TEST_CHECK(compact_stats.directory_count == 1U);
    GDOX_TEST_CHECK(gdox_source_sector_count(&compact) == 64U);
    GDOX_TEST_CHECK(gdox_xdvdfs_find_volume(
        &compact,
        &compact_volume,
        &error
    ));
    GDOX_TEST_CHECK(compact_volume.base_lba == 0U);
    GDOX_TEST_CHECK(gdox_xdvdfs_inspect(
        &compact,
        &compact_volume,
        &compact_metadata,
        &error
    ));
    GDOX_TEST_CHECK(compact_metadata.xbe_file_count == 2U);
    GDOX_TEST_CHECK(strcmp(compact_metadata.title, "Synthetic Halo") == 0);
    GDOX_TEST_CHECK(gdox_source_read(
        &compact,
        compact_volume.root_directory_sector,
        1U,
        xbe_sector,
        sizeof(xbe_sector),
        &error
    ));
    GDOX_TEST_CHECK(xbe_sector[64U] == 0U);
    GDOX_TEST_CHECK(gdox_source_read(
        &compact,
        compact_metadata.xbe_files[0].start_sector,
        1U,
        xbe_sector,
        sizeof(xbe_sector),
        &error
    ));
    GDOX_TEST_CHECK(xbe_sector[FIXTURE_MEDIA_OFFSET + 7U] == 0xebU);

    gdox_xdvdfs_metadata_destroy(&compact_metadata);
    gdox_source_destroy(&compact);
    free(patches);
    gdox_xdvdfs_metadata_destroy(&metadata);
    free(image);
}

static void test_portable_file_source(void)
{
    static const char path[] = "gdox-xdvdfs-fixture.tmp";
    uint64_t sectors = 0U;
    uint8_t *image = fixture_create(&sectors);
    const size_t bytes = (size_t)sectors * GDOX_LOGICAL_SECTOR_BYTES;
    FILE *file;
    gdox_sector_source source = {0};
    gdox_xdvdfs_volume volume;
    gdox_media_observation observation;
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    file = fopen(path, "wb");
    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(fwrite(image, 1U, bytes, file) == bytes);
    GDOX_TEST_CHECK(fclose(file) == 0);
    GDOX_TEST_CHECK(gdox_source_open_file(path, &source, &error));
    GDOX_TEST_CHECK(gdox_source_sector_count(&source) == sectors);
    GDOX_TEST_CHECK(gdox_source_observe_media(&source, &observation));
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_PRESENT
    );
    GDOX_TEST_CHECK(observation.generation == 0U);
    GDOX_TEST_CHECK(gdox_xdvdfs_find_volume(&source, &volume, &error));
    GDOX_TEST_CHECK(volume.base_lba == FIXTURE_BASE_LBA);
    gdox_source_destroy(&source);
    GDOX_TEST_CHECK(remove(path) == 0);
    free(image);
}

static void test_non_xbe_file_extent(void)
{
    uint64_t sectors = 0U;
    uint8_t *image = fixture_create(&sectors);
    uint8_t *root;
    gdox_sector_source source = {0};
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_metadata metadata;
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    root = image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_ROOT_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    memcpy(root + 32U + 14U, "game.bin", 8U);
    GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
    GDOX_TEST_CHECK(gdox_xdvdfs_find_volume(&source, &volume, &error));
    GDOX_TEST_CHECK(gdox_xdvdfs_inspect(
        &source, &volume, &metadata, &error
    ));
    GDOX_TEST_CHECK(metadata.xbe_file_count == 1U);
    GDOX_TEST_CHECK(metadata.file_extent_count == 2U);
    GDOX_TEST_CHECK(
        metadata.file_extents[1].start_sector == FIXTURE_AUX_XBE_SECTOR
    );
    GDOX_TEST_CHECK(metadata.file_extents[1].sector_count == 1U);
    gdox_xdvdfs_metadata_destroy(&metadata);
    gdox_source_destroy(&source);
    free(image);
}

static void test_disc_image_source(void)
{
    static const char whole_path[] = "gdox-whole-disc-fixture.iso";
    static const char xiso_path[] = "gdox-xiso-fixture.iso";
    uint64_t sectors = 0U;
    uint8_t *image = fixture_create(&sectors);
    const size_t whole_bytes =
        (size_t)sectors * GDOX_LOGICAL_SECTOR_BYTES;
    const size_t xiso_bytes =
        (size_t)FIXTURE_SECTORS * GDOX_LOGICAL_SECTOR_BYTES;
    FILE *file;
    gdox_random_disc disc = {0};
    gdox_media_image_info info;
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    file = fopen(whole_path, "wb");
    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(fwrite(image, 1U, whole_bytes, file) == whole_bytes);
    GDOX_TEST_CHECK(fclose(file) == 0);
    GDOX_TEST_CHECK(gdox_media_open_image(
        whole_path,
        &disc,
        &info,
        &error
    ));
    GDOX_TEST_CHECK(info.layout == GDOX_MEDIA_IMAGE_WHOLE_DISC);
    GDOX_TEST_CHECK(info.source_sectors == sectors);
    GDOX_TEST_CHECK(info.game_partition_lba == FIXTURE_BASE_LBA);
    GDOX_TEST_CHECK(strcmp(info.disc.title, "Synthetic Halo") == 0);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(remove(whole_path) == 0);

    memmove(
        image,
        image
            + (size_t)FIXTURE_BASE_LBA * GDOX_LOGICAL_SECTOR_BYTES,
        xiso_bytes
    );
    file = fopen(xiso_path, "wb");
    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(fwrite(image, 1U, xiso_bytes, file) == xiso_bytes);
    GDOX_TEST_CHECK(fclose(file) == 0);
    GDOX_TEST_CHECK(gdox_media_open_image(
        xiso_path,
        &disc,
        &info,
        &error
    ));
    GDOX_TEST_CHECK(info.layout == GDOX_MEDIA_IMAGE_PLAYABLE_XISO);
    GDOX_TEST_CHECK(info.source_sectors == FIXTURE_SECTORS);
    GDOX_TEST_CHECK(info.game_partition_lba == 0U);
    GDOX_TEST_CHECK(strcmp(info.disc.title, "Synthetic Halo") == 0);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(remove(xiso_path) == 0);
    free(image);
}

static void test_live_disc_identity(void)
{
    uint64_t sectors = 0U;
    uint8_t *image = fixture_create(&sectors);
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    gdox_live_disc_info info = {0};
    memory_context *memory;
    uint64_t reads_after_build;
    uint8_t root_sector[GDOX_LOGICAL_SECTOR_BYTES];
    size_t read_bytes = 0U;
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
    GDOX_TEST_CHECK(gdox_live_disc_identify(&source, &info, &error));
    GDOX_TEST_CHECK(strcmp(info.title, "Synthetic Halo") == 0);
    GDOX_TEST_CHECK(info.title_id_present);
    GDOX_TEST_CHECK(info.title_id == UINT32_C(0x4d530001));
    GDOX_TEST_CHECK(info.input_sectors == 0U);
    GDOX_TEST_CHECK(info.output_sectors == 0U);
    memset(&info, 0, sizeof(info));
    memory = source.context;
    GDOX_TEST_CHECK(gdox_live_disc_build(&source, &disc, &info, &error));
    GDOX_TEST_CHECK(strcmp(info.title, "Synthetic Halo") == 0);
    GDOX_TEST_CHECK(info.title_id_present);
    GDOX_TEST_CHECK(info.title_id == UINT32_C(0x4d530001));
    GDOX_TEST_CHECK(info.game_partition_lba == FIXTURE_BASE_LBA);
    GDOX_TEST_CHECK(info.input_sectors == FIXTURE_SECTORS);
    GDOX_TEST_CHECK(info.output_sectors == FIXTURE_SECTORS);
    GDOX_TEST_CHECK(
        gdox_disc_length(&disc)
            == FIXTURE_SECTORS * GDOX_LOGICAL_SECTOR_BYTES
    );
    reads_after_build = memory->read_sectors;
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        (uint64_t)FIXTURE_ROOT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES,
        root_sector,
        sizeof(root_sector),
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(read_bytes == sizeof(root_sector));
    GDOX_TEST_CHECK(root_sector[64U] == 0xa5U);
    GDOX_TEST_CHECK(memory->read_sectors == reads_after_build);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    free(image);
}

static void test_live_disc_request_local_patch(void)
{
    static const uint8_t media_check[8] = {
        0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
    };
    const uint32_t large_xbe_bytes = 4U * 1024U * 1024U;
    const uint64_t large_xbe_sectors =
        large_xbe_bytes / GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t large_source_sectors =
        FIXTURE_BASE_LBA + FIXTURE_XBE_SECTOR + large_xbe_sectors;
    uint64_t small_sectors = 0U;
    uint64_t fixture_sectors = 0U;
    uint8_t *small_image = fixture_create(&small_sectors);
    uint8_t *fixture_image = fixture_create(&fixture_sectors);
    uint8_t *large_image = calloc(
        (size_t)large_source_sectors, GDOX_LOGICAL_SECTOR_BYTES
    );
    uint8_t *large_root;
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    memory_context *memory;
    uint64_t small_reads;
    uint64_t large_reads;
    uint64_t reads_after_first_touch;
    uint8_t patched_check[sizeof(media_check)];
    size_t read_bytes = 0U;
    gdox_error error;

    GDOX_TEST_CHECK(small_image != NULL);
    GDOX_TEST_CHECK(fixture_image != NULL);
    GDOX_TEST_CHECK(large_image != NULL);
    memcpy(
        large_image,
        fixture_image,
        (size_t)fixture_sectors * GDOX_LOGICAL_SECTOR_BYTES
    );
    free(fixture_image);
    GDOX_TEST_CHECK(make_memory_source(
        small_image, small_sectors, &source
    ));
    memory = source.context;
    GDOX_TEST_CHECK(gdox_live_disc_build(
        &source, &disc, NULL, &error
    ));
    small_reads = memory->read_sectors;
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));

    large_root = large_image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_ROOT_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    put_le_u32(large_root + 8U, large_xbe_bytes);
    memcpy(large_root + 32U + 14U, "game.bin", 8U);
    memcpy(
        large_image
            + (size_t)(FIXTURE_BASE_LBA + FIXTURE_XBE_SECTOR)
                * GDOX_LOGICAL_SECTOR_BYTES
            + large_xbe_bytes - sizeof(media_check),
        media_check,
        sizeof(media_check)
    );
    GDOX_TEST_CHECK(make_memory_source(
        large_image,
        large_source_sectors,
        &source
    ));
    memory = source.context;
    GDOX_TEST_CHECK(gdox_live_disc_build(
        &source, &disc, NULL, &error
    ));
    large_reads = memory->read_sectors;
    GDOX_TEST_CHECK(
        large_reads
            == small_reads + large_xbe_sectors - 2U
    );
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        (uint64_t)FIXTURE_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES
            + large_xbe_bytes - sizeof(patched_check),
        patched_check,
        sizeof(patched_check),
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(read_bytes == sizeof(patched_check));
    GDOX_TEST_CHECK(
        patched_check[sizeof(patched_check) - 1U] == 0xebU
    );
    GDOX_TEST_CHECK(memory->read_sectors == large_reads);
    reads_after_first_touch = memory->read_sectors;
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        (uint64_t)FIXTURE_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES
            + large_xbe_bytes - 1U,
        patched_check,
        1U,
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(patched_check[0] == 0xebU);
    GDOX_TEST_CHECK(memory->read_sectors == reads_after_first_touch);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    free(small_image);
    free(large_image);
}

static void test_live_disc_failure_classification(void)
{
    static const gdox_error_code failures[] = {
        GDOX_ERROR_IO,
        GDOX_ERROR_TRANSPORT,
        GDOX_ERROR_PROTOCOL,
    };
    const uint64_t sectors = 64U;
    uint8_t *image = calloc(
        (size_t)sectors, GDOX_LOGICAL_SECTOR_BYTES
    );
    size_t index;
    gdox_sector_source source = {0};
    gdox_live_disc_info info;
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
    GDOX_TEST_CHECK(!gdox_live_disc_identify(&source, &info, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    gdox_source_destroy(&source);

    {
        gdox_random_disc disc = {0};
        memory_context *memory;

        GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
        memory = source.context;
        memory->prepare_failures = 1U;
        GDOX_TEST_CHECK(!gdox_live_disc_build(
            &source, &disc, &info, &error
        ));
        GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
        GDOX_TEST_CHECK(gdox_source_is_valid(&source));
        GDOX_TEST_CHECK(!gdox_disc_is_valid(&disc));
        GDOX_TEST_CHECK(!gdox_source_close(&source, &error));
        GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
        GDOX_TEST_CHECK(gdox_source_is_valid(&source));
        GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    }

    for (index = 0U; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        memory_context *memory;

        GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
        memory = source.context;
        memory->read_failure = failures[index];
        GDOX_TEST_CHECK(!gdox_live_disc_identify(
            &source, &info, &error
        ));
        GDOX_TEST_CHECK(error.code == failures[index]);
        gdox_source_destroy(&source);
    }
    free(image);
}

static void test_live_disc_requires_original_xbox_boot_identity(void)
{
    uint64_t sectors = 0U;
    uint8_t *image = fixture_create(&sectors);
    uint8_t *root;
    uint8_t *xbe;
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    gdox_error error;

    GDOX_TEST_CHECK(image != NULL);
    root = image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_ROOT_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    memcpy(root + 14U, "default.xex", 11U);
    GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
    GDOX_TEST_CHECK(!gdox_live_disc_build(
        &source, &disc, NULL, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(!gdox_disc_is_valid(&disc));
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));

    memcpy(root + 14U, "default.xbe", 11U);
    xbe = image
        + (size_t)(FIXTURE_BASE_LBA + FIXTURE_XBE_SECTOR)
            * GDOX_LOGICAL_SECTOR_BYTES;
    memcpy(xbe, "XEX2", 4U);
    GDOX_TEST_CHECK(make_memory_source(image, sectors, &source));
    GDOX_TEST_CHECK(!gdox_live_disc_build(
        &source, &disc, NULL, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(!gdox_disc_is_valid(&disc));
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(image);
}

void gdox_test_xdvdfs(void)
{
    test_fixture_metadata_and_overlay();
    test_portable_file_source();
    test_non_xbe_file_extent();
    test_disc_image_source();
    test_live_disc_identity();
    test_live_disc_request_local_patch();
    test_live_disc_failure_classification();
    test_live_disc_requires_original_xbox_boot_identity();
}
