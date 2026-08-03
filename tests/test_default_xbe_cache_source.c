#include "test.h"

#include "core/default_xbe_cache_source.h"
#include "gdox/disc.h"

#include <stdlib.h>
#include <string.h>

#define TEST_SECTORS UINT64_C(16)
#define TEST_DEFAULT_SECTOR UINT32_C(4)
#define TEST_DEFAULT_BYTES \
    (2U * GDOX_LOGICAL_SECTOR_BYTES + 17U)

typedef struct cache_audit {
    uint8_t *bytes;
    uint64_t sectors;
    uint64_t rejected_start;
    uint64_t rejected_end;
    unsigned int read_calls;
    unsigned int read_sectors;
    unsigned int max_blocks;
    unsigned int abort_calls;
    unsigned int prepare_calls;
    unsigned int close_calls;
    unsigned int prepare_failures;
    bool reject_cached_reads;
    bool prepared;
} cache_audit;

typedef struct cache_memory_context {
    cache_audit *audit;
} cache_memory_context;

static const uint8_t media_check[8] = {
    0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
};

static uint64_t memory_sector_count(const void *raw_context)
{
    const cache_memory_context *context = raw_context;
    return context->audit->sectors;
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
    cache_memory_context *context = raw_context;
    cache_audit *audit = context->audit;
    const uint64_t end = lba + blocks;

    ++audit->read_calls;
    audit->read_sectors += blocks;
    if (blocks > audit->max_blocks) {
        audit->max_blocks = blocks;
    }
    if (audit->reject_cached_reads
        && lba < audit->rejected_end
        && audit->rejected_start < end) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "cold source received a prepared-XBE read"
        );
        return false;
    }
    if (!gdox_source_validate_read(
            audit->sectors, lba, blocks, output_bytes, error
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
    (void)raw_context;
    return true;
}

static void memory_observe(
    const void *raw_context,
    gdox_media_observation *output
)
{
    (void)raw_context;
    output->readiness = GDOX_MEDIA_READINESS_PRESENT;
    output->generation = 23U;
    output->event = GDOX_MEDIA_EVENT_NONE;
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
    const cache_memory_context *context = raw_context;
    output->commands = context->audit->read_calls;
    output->sectors = context->audit->read_sectors;
    output->bytes =
        (uint64_t)context->audit->read_sectors
            * GDOX_LOGICAL_SECTOR_BYTES;
    output->last_lba = 13U;
    return true;
}

static void memory_abort(void *raw_context)
{
    cache_memory_context *context = raw_context;
    ++context->audit->abort_calls;
}

static bool memory_prepare(void *raw_context, gdox_error *error)
{
    cache_memory_context *context = raw_context;
    cache_audit *audit = context->audit;

    ++audit->prepare_calls;
    if (audit->prepared) {
        return true;
    }
    if (audit->prepare_failures != 0U) {
        --audit->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated prepare failure");
        return false;
    }
    audit->prepared = true;
    return true;
}

static bool memory_close(void *raw_context, gdox_error *error)
{
    cache_memory_context *context = raw_context;
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

static bool make_memory_source(
    cache_audit *audit,
    gdox_sector_source *source
)
{
    cache_memory_context *context = malloc(sizeof(*context));

    if (context == NULL) {
        return false;
    }
    context->audit = audit;
    source->context = context;
    source->ops = &memory_ops;
    return true;
}

static void initialize_audit(cache_audit *audit)
{
    const size_t default_offset =
        (size_t)TEST_DEFAULT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const size_t crossing =
        default_offset + GDOX_LOGICAL_SECTOR_BYTES - 4U;
    const size_t tail = default_offset + TEST_DEFAULT_BYTES
        - sizeof(media_check);
    size_t index;

    memset(audit, 0, sizeof(*audit));
    audit->sectors = TEST_SECTORS;
    audit->bytes = malloc(
        (size_t)TEST_SECTORS * GDOX_LOGICAL_SECTOR_BYTES
    );
    if (audit->bytes == NULL) {
        return;
    }
    for (index = 0U;
         index < (size_t)TEST_SECTORS * GDOX_LOGICAL_SECTOR_BYTES;
         ++index) {
        audit->bytes[index] = (uint8_t)(index * 37U + 11U);
    }
    memcpy(audit->bytes + default_offset, "XBEH", 4U);
    memcpy(audit->bytes + crossing, media_check, sizeof(media_check));
    memcpy(audit->bytes + tail, media_check, sizeof(media_check));
    audit->rejected_start = TEST_DEFAULT_SECTOR;
    audit->rejected_end = TEST_DEFAULT_SECTOR + 3U;
}

static void test_cold_boot_reads_use_prepared_cache(void)
{
    const uint64_t default_offset =
        (uint64_t)TEST_DEFAULT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t crossing =
        default_offset + GDOX_LOGICAL_SECTOR_BYTES - 4U;
    const uint64_t tail =
        default_offset + TEST_DEFAULT_BYTES - sizeof(media_check);
    cache_audit audit;
    gdox_xdvdfs_entry entry = {
        .start_sector = TEST_DEFAULT_SECTOR,
        .size = TEST_DEFAULT_BYTES,
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = &entry,
        .xbe_file_count = 1U,
        .default_xbe_index = 0U,
    };
    gdox_sector_source inner = {0};
    gdox_sector_source cached = {0};
    gdox_random_disc disc = {0};
    uint8_t output[7U * GDOX_LOGICAL_SECTOR_BYTES];
    unsigned int reads_after_prepare;
    size_t read_bytes = 0U;
    gdox_error error;

    initialize_audit(&audit);
    GDOX_TEST_CHECK(audit.bytes != NULL);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_default_xbe_cache(
        &inner, &metadata, &cached, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(audit.read_calls == 1U);
    GDOX_TEST_CHECK(audit.read_sectors == 3U);
    reads_after_prepare = audit.read_calls;
    audit.reject_cached_reads = true;

    GDOX_TEST_CHECK(gdox_source_read(
        &cached,
        TEST_DEFAULT_SECTOR - 1U,
        5U,
        output,
        5U * GDOX_LOGICAL_SECTOR_BYTES,
        &error
    ));
    GDOX_TEST_CHECK(audit.read_calls == reads_after_prepare + 2U);
    GDOX_TEST_CHECK(
        output[GDOX_LOGICAL_SECTOR_BYTES
            + (size_t)(crossing - default_offset) + 7U] == 0xebU
    );
    GDOX_TEST_CHECK(
        output[GDOX_LOGICAL_SECTOR_BYTES
            + (size_t)(tail - default_offset) + 7U] == 0xebU
    );
    GDOX_TEST_CHECK(
        audit.bytes[(size_t)crossing + 7U] == 0x7dU
    );

    GDOX_TEST_CHECK(gdox_disc_from_source(&cached, &disc, &error));
    reads_after_prepare = audit.read_calls;
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        default_offset,
        output,
        4U,
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(read_bytes == 4U);
    GDOX_TEST_CHECK(memcmp(output, "XBEH", 4U) == 0);
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        crossing + 7U,
        output,
        1U,
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(output[0] == 0xebU);
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        tail + 7U,
        output,
        1U,
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(output[0] == 0xebU);
    GDOX_TEST_CHECK(audit.read_calls == reads_after_prepare);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_preparation_uses_bounded_reads(void)
{
    const uint64_t sectors = UINT64_C(520);
    const uint32_t default_sector = UINT32_C(2);
    const uint32_t default_sectors = UINT32_C(513);
    cache_audit audit = {0};
    gdox_xdvdfs_entry entry = {
        .start_sector = default_sector,
        .size = default_sectors * GDOX_LOGICAL_SECTOR_BYTES,
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = &entry,
        .xbe_file_count = 1U,
        .default_xbe_index = 0U,
    };
    gdox_sector_source inner = {0};
    gdox_sector_source cached = {0};
    gdox_error error;

    audit.sectors = sectors;
    audit.bytes = calloc(
        (size_t)sectors, GDOX_LOGICAL_SECTOR_BYTES
    );
    GDOX_TEST_CHECK(audit.bytes != NULL);
    memcpy(
        audit.bytes
            + (size_t)default_sector * GDOX_LOGICAL_SECTOR_BYTES,
        "XBEH",
        4U
    );
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_default_xbe_cache(
        &inner, &metadata, &cached, &error
    ));
    GDOX_TEST_CHECK(audit.read_calls == 2U);
    GDOX_TEST_CHECK(audit.read_sectors == default_sectors);
    GDOX_TEST_CHECK(audit.max_blocks == 512U);
    GDOX_TEST_CHECK(gdox_source_close(&cached, &error));
    free(audit.bytes);
}

static void test_bounded_fallback_and_lifecycle(void)
{
    cache_audit audit;
    gdox_xdvdfs_entry entry = {
        .start_sector = TEST_DEFAULT_SECTOR,
        .size = 64U * 1024U * 1024U + 1U,
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = &entry,
        .xbe_file_count = 1U,
        .default_xbe_index = 0U,
    };
    gdox_sector_source inner = {0};
    gdox_sector_source cached = {0};
    gdox_media_observation observation;
    gdox_physical_read_stats stats;
    gdox_disc_evidence evidence;
    uint8_t sector[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    initialize_audit(&audit);
    GDOX_TEST_CHECK(audit.bytes != NULL);
    audit.prepare_failures = 1U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_default_xbe_cache(
        &inner, &metadata, &cached, &error
    ));
    GDOX_TEST_CHECK(audit.read_calls == 0U);
    GDOX_TEST_CHECK(gdox_source_read(
        &cached, 0U, 1U, sector, sizeof(sector), &error
    ));
    GDOX_TEST_CHECK(audit.read_calls == 1U);
    GDOX_TEST_CHECK(gdox_source_observe_media(&cached, &observation));
    GDOX_TEST_CHECK(observation.generation == 23U);
    GDOX_TEST_CHECK(gdox_source_evidence(&cached, &evidence));
    GDOX_TEST_CHECK(evidence.dmi_present);
    GDOX_TEST_CHECK(gdox_source_physical_read_stats(&cached, &stats));
    GDOX_TEST_CHECK(stats.commands == 1U);
    gdox_source_abort(&cached);
    GDOX_TEST_CHECK(audit.abort_calls == 1U);
    GDOX_TEST_CHECK(!gdox_source_read(
        &cached, 0U, 1U, sector, sizeof(sector), &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(audit.read_calls == 1U);
    GDOX_TEST_CHECK(!gdox_source_close(&cached, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_source_is_valid(&cached));
    GDOX_TEST_CHECK(gdox_source_close(&cached, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);

    metadata.default_xbe_index = 2U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(!gdox_source_make_default_xbe_cache(
        &inner, &metadata, &cached, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(gdox_source_close(&inner, &error));
    free(audit.bytes);
}

void gdox_test_default_xbe_cache_source(void)
{
    test_cold_boot_reads_use_prepared_cache();
    test_preparation_uses_bounded_reads();
    test_bounded_fallback_and_lifecycle();
}
