#include "test.h"

#include "gdox/disc.h"

#include <stdlib.h>
#include <string.h>

typedef struct numbered_source {
    uint64_t sectors;
    gdox_physical_read_stats stats;
} numbered_source;

static uint64_t numbered_count(const void *context)
{
    const numbered_source *source = context;
    return source->sectors;
}

static bool numbered_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    numbered_source *source = context;
    uint32_t index;

    if (!gdox_source_validate_read(
            source->sectors,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    for (index = 0U; index < blocks; ++index) {
        const uint64_t sector = lba + index;
        memset(
            output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES,
            (int)(uint8_t)sector,
            GDOX_LOGICAL_SECTOR_BYTES
        );
    }
    return true;
}

static bool numbered_present(const void *context)
{
    (void)context;
    return true;
}

static bool numbered_close(void *context, gdox_error *error)
{
    gdox_error_clear(error);
    free(context);
    return true;
}

static bool numbered_physical_read_stats(
    const void *context,
    gdox_physical_read_stats *output
)
{
    const numbered_source *source = context;
    *output = source->stats;
    return true;
}

static const gdox_sector_source_ops numbered_ops = {
    numbered_count,
    numbered_read,
    numbered_present,
    numbered_close,
    NULL,
    numbered_physical_read_stats,
    NULL,
};

static numbered_source *make_numbered(gdox_sector_source *source)
{
    numbered_source *context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->sectors = 8U;
    context->stats.commands = 3U;
    context->stats.sectors = 24U;
    context->stats.bytes = 24U * GDOX_LOGICAL_SECTOR_BYTES;
    context->stats.last_lba = 91U;
    source->context = context;
    source->ops = &numbered_ops;
    return context;
}

static void test_direct_unaligned_read(void)
{
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    uint8_t output[12];
    size_t received = 0U;
    gdox_physical_read_stats stats;
    gdox_error error;

    GDOX_TEST_CHECK(make_numbered(&source) != NULL);
    GDOX_TEST_CHECK(gdox_disc_from_source(&source, &disc, &error));
    GDOX_TEST_CHECK(
        gdox_disc_read_at(
            &disc,
            GDOX_LOGICAL_SECTOR_BYTES - 4U,
            output,
            sizeof(output),
            &received,
            &error
        )
    );
    GDOX_TEST_CHECK(received == sizeof(output));
    GDOX_TEST_CHECK(output[0] == 0U);
    GDOX_TEST_CHECK(output[3] == 0U);
    GDOX_TEST_CHECK(output[4] == 1U);
    GDOX_TEST_CHECK(output[11] == 1U);
    GDOX_TEST_CHECK(gdox_disc_physical_read_stats(&disc, &stats));
    GDOX_TEST_CHECK(stats.commands == 3U);
    GDOX_TEST_CHECK(stats.sectors == 24U);
    GDOX_TEST_CHECK(stats.bytes == 24U * GDOX_LOGICAL_SECTOR_BYTES);
    GDOX_TEST_CHECK(stats.last_lba == 91U);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
}

void gdox_test_disc(void)
{
    test_direct_unaligned_read();
}
