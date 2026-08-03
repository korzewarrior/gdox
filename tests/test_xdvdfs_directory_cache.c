#include "test.h"

#include "core/xdvdfs_directory_cache.h"

#include <stdlib.h>
#include <string.h>

#define TEST_SECTORS UINT64_C(32)
#define MAX_READ_CALLS 8U
#define CACHE_LIMIT_BYTES ((size_t)64U * 1024U * 1024U)

typedef struct read_call {
    uint64_t lba;
    uint32_t blocks;
} read_call;

typedef struct cache_audit {
    uint8_t *bytes;
    read_call reads[MAX_READ_CALLS];
    size_t read_count;
    gdox_media_observation observation;
    unsigned int abort_calls;
    unsigned int prepare_calls;
    unsigned int prepare_failures;
    unsigned int close_calls;
} cache_audit;

typedef struct memory_context {
    cache_audit *audit;
} memory_context;

static uint64_t memory_sector_count(const void *raw_context)
{
    (void)raw_context;
    return TEST_SECTORS;
}

static bool memory_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    memory_context *context = raw_context;
    cache_audit *audit = context->audit;

    if (audit->read_count < MAX_READ_CALLS) {
        audit->reads[audit->read_count] = (read_call){lba, blocks};
    }
    ++audit->read_count;
    if (!gdox_source_validate_read(
            TEST_SECTORS,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    memcpy(
        output,
        audit->bytes + (size_t)lba * GDOX_LOGICAL_SECTOR_BYTES,
        output_bytes
    );
    return true;
}

static bool memory_present(const void *raw_context)
{
    const memory_context *context = raw_context;
    return context->audit->observation.readiness
        == GDOX_MEDIA_READINESS_PRESENT;
}

static void memory_observe(
    const void *raw_context,
    gdox_media_observation *output
)
{
    const memory_context *context = raw_context;
    *output = context->audit->observation;
}

static bool memory_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    (void)raw_context;
    output->dmi_present = true;
    return true;
}

static bool memory_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const memory_context *context = raw_context;

    output->commands = context->audit->read_count;
    output->sectors = 17U;
    output->bytes = 17U * GDOX_LOGICAL_SECTOR_BYTES;
    output->last_lba = 10U;
    return true;
}

static void memory_abort(void *raw_context)
{
    memory_context *context = raw_context;
    ++context->audit->abort_calls;
}

static bool memory_prepare(void *raw_context, gdox_error *error)
{
    memory_context *context = raw_context;

    ++context->audit->prepare_calls;
    if (context->audit->prepare_failures != 0U) {
        --context->audit->prepare_failures;
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "simulated close preparation failure"
        );
        return false;
    }
    return true;
}

static bool memory_close(void *raw_context, gdox_error *error)
{
    memory_context *context = raw_context;

    ++context->audit->close_calls;
    free(context);
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops memory_ops = {
    memory_sector_count,
    memory_read,
    memory_present,
    memory_close,
    memory_evidence,
    memory_stats,
    memory_abort,
    memory_prepare,
    memory_observe,
};

static bool initialize_audit(cache_audit *audit)
{
    size_t sector;

    memset(audit, 0, sizeof(*audit));
    audit->observation.readiness = GDOX_MEDIA_READINESS_PRESENT;
    audit->observation.generation = 41U;
    audit->bytes = malloc(
        (size_t)TEST_SECTORS * GDOX_LOGICAL_SECTOR_BYTES
    );
    if (audit->bytes == NULL) {
        return false;
    }
    for (sector = 0U; sector < TEST_SECTORS; ++sector) {
        memset(
            audit->bytes + sector * GDOX_LOGICAL_SECTOR_BYTES,
            (int)sector,
            GDOX_LOGICAL_SECTOR_BYTES
        );
    }
    return true;
}

static bool make_memory_source(
    cache_audit *audit,
    gdox_sector_source *source
)
{
    memory_context *context = malloc(sizeof(*context));

    if (context == NULL) {
        return false;
    }
    context->audit = audit;
    source->context = context;
    source->ops = &memory_ops;
    return true;
}

static bool retain_extent(
    gdox_xdvdfs_directory_cache *cache,
    uint32_t start_sector,
    const uint8_t *values,
    uint32_t blocks,
    gdox_error *error
)
{
    const size_t bytes = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *buffer = malloc(bytes);
    uint32_t block;

    if (buffer == NULL) {
        return false;
    }
    for (block = 0U; block < blocks; ++block) {
        memset(
            buffer + (size_t)block * GDOX_LOGICAL_SECTOR_BYTES,
            values[block],
            GDOX_LOGICAL_SECTOR_BYTES
        );
    }
    if (!gdox_xdvdfs_directory_cache_retain(
            cache,
            start_sector,
            blocks,
            &buffer,
            error
        )) {
        free(buffer);
        return false;
    }
    if (buffer != NULL) {
        free(buffer);
        return false;
    }
    return true;
}

static bool make_split_cache(
    gdox_xdvdfs_directory_cache **cache,
    gdox_error *error
)
{
    static const uint8_t at_eight[] = {0xd8U, 0xd9U};
    static const uint8_t at_five[] = {0xb5U, 0xb6U};
    static const uint8_t at_four[] = {0xa4U, 0xa5U, 0xa6U};
    static const uint8_t duplicate_at_eight[] = {0xc8U};

    if (!gdox_xdvdfs_directory_cache_create(cache, error)
        || !retain_extent(*cache, 8U, at_eight, 2U, error)
        || !retain_extent(*cache, 5U, at_five, 2U, error)
        || !retain_extent(*cache, 4U, at_four, 3U, error)
        || !retain_extent(
            *cache,
            8U,
            duplicate_at_eight,
            1U,
            error
        )) {
        gdox_xdvdfs_directory_cache_destroy(cache);
        return false;
    }
    gdox_xdvdfs_directory_cache_finalize(*cache);
    return true;
}

static void test_split_reads_and_retryable_close(void)
{
    static const uint8_t expected[] = {
        3U, 0xa4U, 0xa5U, 0xa6U, 7U, 0xc8U, 0xd9U, 10U,
    };
    cache_audit audit;
    gdox_xdvdfs_directory_cache *cache = NULL;
    gdox_sector_source inner = {0};
    gdox_sector_source cached = {0};
    gdox_media_observation observation;
    gdox_disc_evidence evidence;
    gdox_physical_read_stats stats;
    uint8_t output[8U * GDOX_LOGICAL_SECTOR_BYTES];
    size_t sector;
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(make_split_cache(&cache, &error));
    GDOX_TEST_CHECK(gdox_source_make_xdvdfs_directory_cache(
        &inner, &cache, &cached, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(cache == NULL);
    GDOX_TEST_CHECK(gdox_source_read(
        &cached, 3U, 8U, output, sizeof(output), &error
    ));
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(audit.reads[0].lba == 3U);
    GDOX_TEST_CHECK(audit.reads[0].blocks == 1U);
    GDOX_TEST_CHECK(audit.reads[1].lba == 7U);
    GDOX_TEST_CHECK(audit.reads[1].blocks == 1U);
    GDOX_TEST_CHECK(audit.reads[2].lba == 10U);
    GDOX_TEST_CHECK(audit.reads[2].blocks == 1U);
    for (sector = 0U; sector < sizeof(expected); ++sector) {
        GDOX_TEST_CHECK(
            output[sector * GDOX_LOGICAL_SECTOR_BYTES]
                == expected[sector]
        );
    }
    GDOX_TEST_CHECK(gdox_source_observe_media(&cached, &observation));
    GDOX_TEST_CHECK(observation.generation == 41U);
    GDOX_TEST_CHECK(gdox_source_evidence(&cached, &evidence));
    GDOX_TEST_CHECK(evidence.dmi_present);
    GDOX_TEST_CHECK(gdox_source_physical_read_stats(&cached, &stats));
    GDOX_TEST_CHECK(stats.commands == 3U);

    audit.prepare_failures = 1U;
    GDOX_TEST_CHECK(!gdox_source_close(&cached, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_source_is_valid(&cached));
    GDOX_TEST_CHECK(gdox_source_read(
        &cached,
        4U,
        1U,
        output,
        GDOX_LOGICAL_SECTOR_BYTES,
        &error
    ));
    GDOX_TEST_CHECK(output[0] == 0xa4U);
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(gdox_source_close(&cached, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_abort_cancels_cached_and_forwarded_reads(void)
{
    cache_audit audit;
    gdox_xdvdfs_directory_cache *cache = NULL;
    gdox_sector_source inner = {0};
    gdox_sector_source cached = {0};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(make_split_cache(&cache, &error));
    GDOX_TEST_CHECK(gdox_source_make_xdvdfs_directory_cache(
        &inner, &cache, &cached, &error
    ));
    gdox_source_abort(&cached);
    GDOX_TEST_CHECK(audit.abort_calls == 1U);
    GDOX_TEST_CHECK(!gdox_source_read(
        &cached, 4U, 1U, output, sizeof(output), &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(!gdox_source_read(
        &cached, 3U, 1U, output, sizeof(output), &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(audit.read_count == 0U);
    GDOX_TEST_CHECK(gdox_source_close(&cached, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_constructor_failure_preserves_ownership(void)
{
    static const uint8_t values[] = {0x31U, 0x32U};
    cache_audit audit;
    gdox_xdvdfs_directory_cache *cache = NULL;
    gdox_sector_source inner = {0};
    gdox_sector_source output = {0};
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_xdvdfs_directory_cache_create(&cache, &error));
    GDOX_TEST_CHECK(retain_extent(cache, 31U, values, 2U, &error));
    gdox_xdvdfs_directory_cache_finalize(cache);
    GDOX_TEST_CHECK(!gdox_source_make_xdvdfs_directory_cache(
        &inner, &cache, &output, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_OUT_OF_BOUNDS);
    GDOX_TEST_CHECK(gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(cache != NULL);
    GDOX_TEST_CHECK(!gdox_source_is_valid(&output));
    gdox_xdvdfs_directory_cache_destroy(&cache);
    GDOX_TEST_CHECK(gdox_source_close(&inner, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_retention_limit_is_fail_open(void)
{
    gdox_xdvdfs_directory_cache *cache = NULL;
    uint8_t *limit = malloc(CACHE_LIMIT_BYTES);
    uint8_t *overflow = malloc(GDOX_LOGICAL_SECTOR_BYTES);
    uint8_t *overflow_original = overflow;
    gdox_error error;

    GDOX_TEST_CHECK(limit != NULL);
    GDOX_TEST_CHECK(overflow != NULL);
    GDOX_TEST_CHECK(gdox_xdvdfs_directory_cache_create(&cache, &error));
    GDOX_TEST_CHECK(gdox_xdvdfs_directory_cache_retain(
        cache,
        0U,
        (uint32_t)(CACHE_LIMIT_BYTES / GDOX_LOGICAL_SECTOR_BYTES),
        &limit,
        &error
    ));
    GDOX_TEST_CHECK(limit == NULL);
    GDOX_TEST_CHECK(gdox_xdvdfs_directory_cache_retain(
        cache,
        (uint32_t)(CACHE_LIMIT_BYTES / GDOX_LOGICAL_SECTOR_BYTES),
        1U,
        &overflow,
        &error
    ));
    GDOX_TEST_CHECK(overflow == overflow_original);
    free(overflow);
    gdox_xdvdfs_directory_cache_destroy(&cache);
}

void gdox_test_xdvdfs_directory_cache(void)
{
    test_split_reads_and_retryable_close();
    test_abort_cancels_cached_and_forwarded_reads();
    test_constructor_failure_preserves_ownership();
    test_retention_limit_is_fail_open();
}
