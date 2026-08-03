#include "test.h"

#include "gdox/disc.h"

#include <stdlib.h>
#include <string.h>

typedef struct numbered_source {
    uint64_t sectors;
    gdox_physical_read_stats stats;
    gdox_media_observation observation;
    struct numbered_close_audit *close_audit;
} numbered_source;

typedef struct numbered_close_audit {
    unsigned int prepare_calls;
    unsigned int close_calls;
    unsigned int prepare_failures;
} numbered_close_audit;

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

static void numbered_observe(
    const void *context,
    gdox_media_observation *output
)
{
    const numbered_source *source = context;
    *output = source->observation;
}

static bool numbered_close(void *context, gdox_error *error)
{
    numbered_source *source = context;
    gdox_error_clear(error);
    if (source->close_audit != NULL) {
        ++source->close_audit->close_calls;
    }
    free(source);
    return true;
}

static bool numbered_prepare_close(void *context, gdox_error *error)
{
    numbered_source *source = context;
    numbered_close_audit *audit = source->close_audit;

    gdox_error_clear(error);
    if (audit == NULL) {
        return true;
    }
    ++audit->prepare_calls;
    if (audit->prepare_failures != 0U) {
        --audit->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated disc prepare failure");
        return false;
    }
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
    numbered_prepare_close,
    numbered_observe,
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
    context->observation.readiness = GDOX_MEDIA_READINESS_PRESENT;
    context->observation.generation = UINT64_C(11);
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
    gdox_media_observation observation;
    numbered_source *context;
    gdox_error error;

    context = make_numbered(&source);
    GDOX_TEST_CHECK(context != NULL);
    GDOX_TEST_CHECK(gdox_disc_from_source(&source, &disc, &error));
    GDOX_TEST_CHECK(gdox_disc_observe_media(&disc, &observation));
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_PRESENT
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(11));
    context->observation.readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    context->observation.generation = UINT64_C(12);
    GDOX_TEST_CHECK(!gdox_disc_media_present(&disc));
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

static void test_direct_close_retry(void)
{
    numbered_close_audit audit = {0};
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    numbered_source *context;
    uint8_t output[4];
    size_t received = 0U;
    gdox_error error;

    context = make_numbered(&source);
    GDOX_TEST_CHECK(context != NULL);
    context->close_audit = &audit;
    audit.prepare_failures = 1U;
    GDOX_TEST_CHECK(gdox_disc_from_source(&source, &disc, &error));

    GDOX_TEST_CHECK(!gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_disc_is_valid(&disc));
    GDOX_TEST_CHECK(audit.close_calls == 0U);
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        0U,
        output,
        sizeof(output),
        &received,
        &error
    ));
    GDOX_TEST_CHECK(received == sizeof(output));

    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(!gdox_disc_is_valid(&disc));
    GDOX_TEST_CHECK(audit.prepare_calls >= 2U);
    GDOX_TEST_CHECK(audit.close_calls == 1U);
}

static void test_direct_disc_rejects_occupied_output(void)
{
    gdox_sector_source input = {0};
    gdox_sector_source occupied_source = {0};
    gdox_random_disc occupied = {0};
    gdox_error error;

    GDOX_TEST_CHECK(make_numbered(&input) != NULL);
    GDOX_TEST_CHECK(make_numbered(&occupied_source) != NULL);
    GDOX_TEST_CHECK(gdox_disc_from_source(
        &occupied_source, &occupied, &error
    ));
    GDOX_TEST_CHECK(!gdox_disc_from_source(&input, &occupied, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_source_is_valid(&input));
    GDOX_TEST_CHECK(gdox_disc_is_valid(&occupied));
    GDOX_TEST_CHECK(gdox_source_close(&input, &error));
    GDOX_TEST_CHECK(gdox_disc_close(&occupied, &error));
}

void gdox_test_disc(void)
{
    test_direct_unaligned_read();
    test_direct_close_retry();
    test_direct_disc_rejects_occupied_output();
}
