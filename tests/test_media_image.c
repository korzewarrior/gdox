#include "gdox/disc.h"
#include "gdox/media.h"
#include "gdox/sector.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#define gdox_test_getpid _getpid
#else
#include <unistd.h>
#define gdox_test_getpid getpid
#endif

#define X360_GAME_OFFSET UINT64_C(0x0000fb20)
#define X360_IMAGE_SECTORS UINT64_C(96)
#define X360_ROOT_SECTOR UINT32_C(40)
#define X360_ROOT_BYTES UINT32_C(192)
#define X360_XEX_SECTOR UINT32_C(50)

#define XBOX_IMAGE_SECTORS UINT64_C(160)
#define XBOX_ROOT_SECTOR UINT32_C(64)
#define XBOX_XBE_SECTOR UINT32_C(128)
#define XBOX_MEDIA_OFFSET 0x380U

static const uint8_t gdfx_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

static void put_le_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void put_le_u64(uint8_t output[8], uint64_t value)
{
    put_le_u32(output, (uint32_t)value);
    put_le_u32(output + 4U, (uint32_t)(value >> 32U));
}

static void put_be_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static bool write_fixture(
    const char *path,
    const uint8_t *bytes,
    size_t byte_count
)
{
    FILE *file = fopen(path, "wb");
    bool written;

    if (file == NULL) {
        return false;
    }
    written = fwrite(bytes, 1U, byte_count, file) == byte_count;
    return fclose(file) == 0 && written;
}

static bool check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "failed: %s\n", message);
        return false;
    }
    return true;
}

static uint8_t *make_x360_fixture(size_t *byte_count)
{
    const size_t bytes =
        (size_t)X360_IMAGE_SECTORS * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t descriptor_offset = X360_GAME_OFFSET
        + UINT64_C(32) * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t root_offset = X360_GAME_OFFSET
        + (uint64_t)X360_ROOT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t executable_offset = X360_GAME_OFFSET
        + (uint64_t)X360_XEX_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *image = calloc(bytes, 1U);
    uint8_t *descriptor;
    uint8_t *root;
    uint8_t *executable;

    if (image == NULL || descriptor_offset + GDOX_LOGICAL_SECTOR_BYTES > bytes
        || root_offset + X360_ROOT_BYTES > bytes
        || executable_offset + 128U > bytes) {
        free(image);
        return NULL;
    }
    descriptor = image + (size_t)descriptor_offset;
    root = image + (size_t)root_offset;
    executable = image + (size_t)executable_offset;

    memcpy(descriptor, gdfx_magic, sizeof(gdfx_magic));
    memcpy(
        descriptor + GDOX_LOGICAL_SECTOR_BYTES - sizeof(gdfx_magic),
        gdfx_magic,
        sizeof(gdfx_magic)
    );
    put_le_u32(descriptor + 20U, X360_ROOT_SECTOR);
    put_le_u32(descriptor + 24U, X360_ROOT_BYTES);

    put_le_u32(root + 4U, X360_XEX_SECTOR);
    put_le_u32(root + 8U, 128U);
    root[12] = 0U;
    root[13] = 11U;
    memcpy(root + 14U, "default.xex", 11U);

    memcpy(executable, "XEX2", 4U);
    put_be_u32(executable + 20U, 1U);
    put_be_u32(executable + 24U, UINT32_C(0x00040006));
    put_be_u32(executable + 28U, 48U);
    put_be_u32(executable + 48U, UINT32_C(0x68ec85bf));
    put_be_u32(executable + 60U, UINT32_C(0x555308c2));
    executable[64] = 2U;
    executable[65] = 3U;
    executable[66] = 1U;
    executable[67] = 2U;

    image[bytes - 1U] = 0xa5U;
    *byte_count = bytes;
    return image;
}

static uint8_t *make_xbox_fixture(size_t *byte_count)
{
    static const uint8_t media_check[8] = {
        0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
    };
    static const char title[] = "Synthetic Xbox";
    const size_t bytes =
        (size_t)XBOX_IMAGE_SECTORS * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *image = calloc(bytes, 1U);
    uint8_t *descriptor;
    uint8_t *root;
    uint8_t *executable;
    size_t index;

    if (image == NULL) {
        return NULL;
    }
    descriptor = image + 32U * GDOX_LOGICAL_SECTOR_BYTES;
    root = image + (size_t)XBOX_ROOT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    executable = image
        + (size_t)XBOX_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;

    memcpy(descriptor, gdfx_magic, sizeof(gdfx_magic));
    memcpy(
        descriptor + GDOX_LOGICAL_SECTOR_BYTES - sizeof(gdfx_magic),
        gdfx_magic,
        sizeof(gdfx_magic)
    );
    put_le_u32(descriptor + 20U, XBOX_ROOT_SECTOR);
    put_le_u32(descriptor + 24U, 64U);
    put_le_u64(descriptor + 28U, UINT64_C(132537600000000000));

    put_le_u32(root + 4U, XBOX_XBE_SECTOR);
    put_le_u32(root + 8U, 2U * GDOX_LOGICAL_SECTOR_BYTES);
    root[12] = 0x20U;
    root[13] = 11U;
    memcpy(root + 14U, "default.xbe", 11U);

    memcpy(executable, "XBEH", 4U);
    put_le_u32(executable + 0x104U, UINT32_C(0x00010000));
    put_le_u32(executable + 0x108U, GDOX_LOGICAL_SECTOR_BYTES);
    put_le_u32(executable + 0x10cU, 2U * GDOX_LOGICAL_SECTOR_BYTES);
    put_le_u32(executable + 0x118U, UINT32_C(0x00010200));
    put_le_u32(executable + 0x200U, UINT32_C(0x1d0));
    put_le_u32(executable + 0x208U, UINT32_C(0x4d530001));
    for (index = 0U; index < sizeof(title) - 1U; ++index) {
        executable[0x20cU + index * 2U] = (uint8_t)title[index];
    }
    memcpy(
        executable + XBOX_MEDIA_OFFSET,
        media_check,
        sizeof(media_check)
    );
    *byte_count = bytes;
    return image;
}

static bool test_xbox_360_image(void)
{
    char path[128];
    size_t image_bytes = 0U;
    uint8_t *image = make_x360_fixture(&image_bytes);
    gdox_random_disc disc = {0};
    gdox_media_image_info info;
    gdox_error error;
    uint8_t marker = 0U;
    uint8_t magic[4] = {0};
    size_t read_bytes = 0U;
    bool created = false;
    bool success = false;
    bool cleaned = true;

    (void)snprintf(
        path,
        sizeof(path),
        "gdox-media-x360-%d.tmp",
        gdox_test_getpid()
    );
    (void)remove(path);
    if (!check(image != NULL, "allocate Xbox 360 fixture")) {
        goto cleanup;
    }
    if (!check(write_fixture(path, image, image_bytes),
            "write Xbox 360 fixture")) {
        (void)remove(path);
        goto cleanup;
    }
    created = true;
    if (!check(gdox_media_open_image(path, &disc, &info, &error),
            "open Xbox 360 image")
        || !check(info.platform == GDOX_MEDIA_PLATFORM_XBOX_360,
            "classify Xbox 360 platform")
        || !check(info.backend == GDOX_MEDIA_BACKEND_XENIA,
            "select Xenia backend")
        || !check(info.layout == GDOX_MEDIA_IMAGE_NONE,
            "leave Original Xbox layout unset")
        || !check(info.source_sectors == X360_IMAGE_SECTORS,
            "retain source sector count")
        || !check(info.x360.layout == GDOX_X360_IMAGE_LAYOUT_FB20,
            "detect exact Xenia image layout")
        || !check(info.x360.source_bytes == image_bytes,
            "retain exact Xbox 360 source length metadata")
        || !check(info.x360.game_offset_bytes == X360_GAME_OFFSET,
            "retain exact Xbox 360 game offset")
        || !check(info.x360.root_directory_sector == X360_ROOT_SECTOR,
            "retain Xbox 360 root sector")
        || !check(info.x360.root_directory_size == X360_ROOT_BYTES,
            "retain Xbox 360 root size")
        || !check(info.x360.executable == GDOX_X360_EXECUTABLE_XEX2,
            "identify XEX2 executable")
        || !check(strcmp(info.x360.launch_executable, "default.xex") == 0,
            "retain launch executable")
        || !check(info.x360.execution.valid,
            "parse execution information")
        || !check(info.x360.execution.media_id == UINT32_C(0x68ec85bf),
            "retain media ID")
        || !check(info.x360.execution.title_id == UINT32_C(0x555308c2),
            "retain title ID")
        || !check(info.x360.execution.platform == 2U,
            "retain execution platform")
        || !check(info.x360.execution.executable_type == 3U,
            "retain executable type")
        || !check(info.x360.execution.disc_number == 1U,
            "retain disc number")
        || !check(info.x360.execution.disc_count == 2U,
            "retain disc count")
        || !check(gdox_disc_length(&disc) == image_bytes,
            "keep the random-disc view byte-exact")
        || !check(gdox_disc_read_at(
            &disc,
            X360_GAME_OFFSET
                + (uint64_t)X360_XEX_SECTOR * GDOX_LOGICAL_SECTOR_BYTES,
            magic,
            sizeof(magic),
            &read_bytes,
            &error
        ), "read XEX through unchanged random-disc view")
        || !check(read_bytes == sizeof(magic)
            && memcmp(magic, "XEX2", sizeof(magic)) == 0,
            "preserve Xbox 360 executable bytes")
        || !check(gdox_disc_read_at(
            &disc,
            image_bytes - 1U,
            &marker,
            1U,
            &read_bytes,
            &error
        ), "read final source byte")
        || !check(read_bytes == 1U && marker == 0xa5U,
            "preserve final source byte")) {
        goto cleanup;
    }
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc) && !gdox_disc_close(&disc, &error)) {
        cleaned = false;
    }
    if (created && remove(path) != 0) {
        cleaned = false;
    }
    free(image);
    return success && check(cleaned, "clean Xbox 360 fixture");
}

static bool test_original_xbox_image(void)
{
    char path[128];
    size_t image_bytes = 0U;
    uint8_t *image = make_xbox_fixture(&image_bytes);
    gdox_random_disc disc = {0};
    gdox_media_image_info info;
    gdox_error error;
    uint8_t patched = 0U;
    size_t read_bytes = 0U;
    bool created = false;
    bool success = false;
    bool cleaned = true;

    (void)snprintf(
        path,
        sizeof(path),
        "gdox-media-xbox-%d.tmp",
        gdox_test_getpid()
    );
    (void)remove(path);
    if (!check(image != NULL, "allocate Original Xbox fixture")) {
        goto cleanup;
    }
    if (!check(write_fixture(path, image, image_bytes),
            "write Original Xbox fixture")) {
        (void)remove(path);
        goto cleanup;
    }
    created = true;
    if (!check(gdox_media_open_image(path, &disc, &info, &error),
            "open Original Xbox image")
        || !check(info.platform == GDOX_MEDIA_PLATFORM_XBOX,
            "classify Original Xbox platform")
        || !check(info.backend == GDOX_MEDIA_BACKEND_XEMU,
            "select xemu backend")
        || !check(info.layout == GDOX_MEDIA_IMAGE_PLAYABLE_XISO,
            "classify playable XISO")
        || !check(info.source_sectors == XBOX_IMAGE_SECTORS,
            "retain Original Xbox source sectors")
        || !check(info.game_partition_lba == 0U,
            "retain zero XISO partition base")
        || !check(strcmp(info.disc.title, "Synthetic Xbox") == 0,
            "retain Original Xbox title")
        || !check(info.disc.title_id_present
            && info.disc.title_id == UINT32_C(0x4d530001),
            "retain Original Xbox title ID")
        || !check(info.disc.input_sectors == XBOX_IMAGE_SECTORS,
            "record Original Xbox input sectors")
        || !check(info.disc.output_sectors == info.disc.input_sectors,
            "expose Original Xbox game partition directly")
        || !check(gdox_disc_length(&disc)
            == info.disc.output_sectors * GDOX_LOGICAL_SECTOR_BYTES,
            "match direct Original Xbox length")
        || !check(gdox_disc_read_at(
            &disc,
            (uint64_t)XBOX_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES
                + XBOX_MEDIA_OFFSET + 7U,
            &patched,
            sizeof(patched),
            &read_bytes,
            &error
        ), "read Original Xbox compatibility byte")
        || !check(read_bytes == sizeof(patched) && patched == 0xebU,
            "apply Original Xbox compatibility byte before exposure")
        || !check(
            image[
                (size_t)XBOX_XBE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES
                    + XBOX_MEDIA_OFFSET + 7U
            ] == 0x7dU,
            "leave Original Xbox source bytes unchanged")) {
        goto cleanup;
    }
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc) && !gdox_disc_close(&disc, &error)) {
        cleaned = false;
    }
    if (created && remove(path) != 0) {
        cleaned = false;
    }
    free(image);
    return success && check(cleaned, "clean Original Xbox fixture");
}

int main(void)
{
    return test_xbox_360_image() && test_original_xbox_image() ? 0 : 1;
}
