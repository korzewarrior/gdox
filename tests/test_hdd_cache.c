#include "test.h"

#include "core/hdd_cache.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#define gdox_test_getpid _getpid
#define gdox_test_remove _unlink
#else
#include <unistd.h>
#define gdox_test_getpid getpid
#define gdox_test_remove unlink
#endif

#define FIXTURE_CLUSTER_BYTES ((size_t)64U * 1024U)
#define FIXTURE_FILE_CLUSTERS 14U
#define FIXTURE_L2_ENTRIES (FIXTURE_CLUSTER_BYTES / 8U)
#define FIXTURE_QCOW_COPIED (UINT64_C(1) << 63U)

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void put_be_u64(uint8_t *output, uint64_t value)
{
    put_be_u32(output, (uint32_t)(value >> 32U));
    put_be_u32(output + 4U, (uint32_t)value);
}

static void map_cluster(
    uint8_t *image,
    uint64_t guest_offset,
    size_t l2_cluster,
    size_t data_cluster
)
{
    const uint64_t guest_cluster = guest_offset / FIXTURE_CLUSTER_BYTES;
    const size_t l1_index = (size_t)(guest_cluster / FIXTURE_L2_ENTRIES);
    const size_t l2_index = (size_t)(guest_cluster % FIXTURE_L2_ENTRIES);

    put_be_u64(
        image + FIXTURE_CLUSTER_BYTES + l1_index * 8U,
        FIXTURE_QCOW_COPIED
            | (uint64_t)l2_cluster * FIXTURE_CLUSTER_BYTES
    );
    put_be_u64(
        image + l2_cluster * FIXTURE_CLUSTER_BYTES + l2_index * 8U,
        FIXTURE_QCOW_COPIED
            | (uint64_t)data_cluster * FIXTURE_CLUSTER_BYTES
    );
}

static bool write_fixture(const char *path)
{
    const size_t image_bytes =
        FIXTURE_FILE_CLUSTERS * FIXTURE_CLUSTER_BYTES;
    uint8_t *image = calloc(1U, image_bytes);
    FILE *file;
    size_t cluster;
    bool success;

    if (image == NULL) {
        return false;
    }
    put_be_u32(image, UINT32_C(0x514649fb));
    put_be_u32(image + 4U, UINT32_C(3));
    put_be_u32(image + 20U, UINT32_C(16));
    put_be_u64(image + 24U, UINT64_C(8) * 1024U * 1024U * 1024U);
    put_be_u32(image + 36U, UINT32_C(16));
    put_be_u64(image + 40U, FIXTURE_CLUSTER_BYTES);

    map_cluster(image, UINT64_C(0x00080000), 2U, 7U);
    map_cluster(
        image,
        UINT64_C(0x00080000) + FIXTURE_CLUSTER_BYTES,
        2U,
        8U
    );
    map_cluster(image, UINT64_C(0x2ee80000), 3U, 9U);
    map_cluster(
        image,
        UINT64_C(0x2ee80000) + FIXTURE_CLUSTER_BYTES,
        3U,
        10U
    );
    map_cluster(image, UINT64_C(0x5dc80000), 4U, 11U);
    map_cluster(
        image,
        UINT64_C(0x5dc80000) + FIXTURE_CLUSTER_BYTES,
        4U,
        12U
    );
    map_cluster(image, UINT64_C(0x8ca80000), 5U, 13U);
    for (cluster = 7U; cluster < 13U; ++cluster) {
        memset(
            image + cluster * FIXTURE_CLUSTER_BYTES,
            0xa5,
            FIXTURE_CLUSTER_BYTES
        );
    }
    memset(
        image + 13U * FIXTURE_CLUSTER_BYTES,
        0x5a,
        FIXTURE_CLUSTER_BYTES
    );
    file = fopen(path, "wb");
    if (file == NULL) {
        free(image);
        return false;
    }
    success = fwrite(image, 1U, image_bytes, file) == image_bytes;
    if (fclose(file) != 0) {
        success = false;
    }
    free(image);
    return success;
}

static bool all_value(const uint8_t *bytes, size_t count, uint8_t value)
{
    size_t index;
    for (index = 0U; index < count; ++index) {
        if (bytes[index] != value) {
            return false;
        }
    }
    return true;
}

void gdox_test_hdd_cache(void)
{
    static const uint32_t volume_ids[3] = {
        UINT32_C(0x000d0137),
        UINT32_C(0x000d01d4),
        UINT32_C(0x000d02a4),
    };
    static const size_t first_clusters[3] = {7U, 9U, 11U};
    const size_t image_bytes =
        FIXTURE_FILE_CLUSTERS * FIXTURE_CLUSTER_BYTES;
    uint8_t *image;
    char path[256];
    bool changed = false;
    gdox_error error;
    FILE *file;
    size_t partition;

    (void)snprintf(
        path,
        sizeof(path),
        "./gdox-hdd-cache-%d-%lld.qcow2",
        gdox_test_getpid(),
        (long long)time(NULL)
    );
    GDOX_TEST_CHECK(write_fixture(path));
    image = malloc(image_bytes);
    GDOX_TEST_CHECK(image != NULL);
    GDOX_TEST_CHECK(
        gdox_hdd_reset_cache_partitions(path, &changed, &error)
    );
    GDOX_TEST_CHECK(changed);
    file = fopen(path, "rb");
    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(fread(image, 1U, image_bytes, file) == image_bytes);
    GDOX_TEST_CHECK(fclose(file) == 0);
    for (partition = 0U; partition < 3U; ++partition) {
        const uint8_t *metadata =
            image + first_clusters[partition] * FIXTURE_CLUSTER_BYTES;
        GDOX_TEST_CHECK(memcmp(metadata, "FATX", 4U) == 0);
        GDOX_TEST_CHECK(metadata[4] == (uint8_t)volume_ids[partition]);
        GDOX_TEST_CHECK(
            metadata[5] == (uint8_t)(volume_ids[partition] >> 8U)
        );
        GDOX_TEST_CHECK(metadata[8] == 32U);
        GDOX_TEST_CHECK(metadata[12] == 1U);
        GDOX_TEST_CHECK(all_value(metadata + 18U, 4096U - 18U, 0xffU));
        GDOX_TEST_CHECK(metadata[4096U] == 0xf8U);
        GDOX_TEST_CHECK(metadata[4097U] == 0xffU);
        GDOX_TEST_CHECK(metadata[4098U] == 0xffU);
        GDOX_TEST_CHECK(metadata[4099U] == 0xffU);
        GDOX_TEST_CHECK(
            all_value(
                metadata + 4100U,
                FIXTURE_CLUSTER_BYTES - 4100U,
                0U
            )
        );
        GDOX_TEST_CHECK(
            all_value(
                image
                    + (first_clusters[partition] + 1U)
                        * FIXTURE_CLUSTER_BYTES,
                FIXTURE_CLUSTER_BYTES,
                0U
            )
        );
    }
    GDOX_TEST_CHECK(
        all_value(
            image + 13U * FIXTURE_CLUSTER_BYTES,
            FIXTURE_CLUSTER_BYTES,
            0x5a
        )
    );
    free(image);
    GDOX_TEST_CHECK(gdox_test_remove(path) == 0);
}
