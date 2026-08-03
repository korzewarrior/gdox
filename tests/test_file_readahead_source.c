#include "test.h"

#include "core/file_readahead_source.h"

#include <stdlib.h>
#include <string.h>

#define TEST_SECTORS UINT64_C(64)
#define LRU_WINDOW_BLOCKS 512U
#define LRU_SLOT_COUNT 16U
#define LRU_TEST_SECTORS ((LRU_SLOT_COUNT + 1U) * LRU_WINDOW_BLOCKS)
#define MAX_READ_CALLS 64U

typedef struct read_call {
    uint64_t lba;
    uint32_t blocks;
} read_call;

typedef struct readahead_audit {
    uint8_t *bytes;
    uint64_t sector_count;
    read_call reads[MAX_READ_CALLS];
    size_t read_count;
    gdox_error_code next_large_failure;
    gdox_media_observation observation;
    unsigned int observe_calls;
    unsigned int abort_calls;
    unsigned int prepare_calls;
    unsigned int prepare_failures;
    unsigned int close_calls;
    bool prepared;
} readahead_audit;

typedef struct memory_context {
    readahead_audit *audit;
} memory_context;

static uint64_t memory_sector_count(const void *raw_context)
{
    const memory_context *context = raw_context;
    return context->audit->sector_count;
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
    readahead_audit *audit = context->audit;

    if (audit->read_count < MAX_READ_CALLS) {
        audit->reads[audit->read_count] = (read_call){lba, blocks};
    }
    ++audit->read_count;
    if (blocks > 1U && audit->next_large_failure != GDOX_ERROR_NONE) {
        const gdox_error_code failure = audit->next_large_failure;
        audit->next_large_failure = GDOX_ERROR_NONE;
        gdox_error_set(error, failure, "simulated speculative read failure");
        return false;
    }
    if (!gdox_source_validate_read(
            audit->sector_count,
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
    memory_context *context = (memory_context *)raw_context;
    ++context->audit->observe_calls;
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
    const readahead_audit *audit = context->audit;

    output->commands = audit->read_count;
    output->sectors = 77U;
    output->bytes = 77U * GDOX_LOGICAL_SECTOR_BYTES;
    output->last_lba = 31U;
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
    readahead_audit *audit = context->audit;

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

static bool make_memory_source(
    readahead_audit *audit,
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

static bool initialize_audit_sectors(
    readahead_audit *audit,
    uint64_t sector_count
)
{
    uint64_t sector;

    memset(audit, 0, sizeof(*audit));
    audit->sector_count = sector_count;
    audit->observation.readiness = GDOX_MEDIA_READINESS_PRESENT;
    audit->observation.generation = 7U;
    audit->bytes = malloc(
        (size_t)sector_count * GDOX_LOGICAL_SECTOR_BYTES
    );
    if (audit->bytes == NULL) {
        return false;
    }
    for (sector = 0U; sector < sector_count; ++sector) {
        memset(
            audit->bytes
                + (size_t)sector * GDOX_LOGICAL_SECTOR_BYTES,
            (int)(sector & UINT64_C(0xff)),
            GDOX_LOGICAL_SECTOR_BYTES
        );
    }
    return true;
}

static bool initialize_audit(readahead_audit *audit)
{
    return initialize_audit_sectors(audit, TEST_SECTORS);
}

static void initialize_metadata(
    gdox_xdvdfs_metadata *metadata,
    gdox_xdvdfs_file_extent *extents
)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    extents[0] = (gdox_xdvdfs_file_extent){10U, 5U, 0U};
    extents[1] = (gdox_xdvdfs_file_extent){20U, 6U, 0U};
    metadata->file_extents = extents;
    metadata->file_extent_count = 2U;
}

static bool read_blocks(
    gdox_sector_source *source,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    gdox_error *error
)
{
    return gdox_source_read(
        source,
        lba,
        blocks,
        output,
        (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES,
        error
    );
}

static void test_sequential_extent_reads(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2];
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[3U * GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&inner));

    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(audit.reads[0].lba == 10U);
    GDOX_TEST_CHECK(audit.reads[0].blocks == 4U);
    GDOX_TEST_CHECK(read_blocks(&source, 11U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(output[0] == 11U);
    GDOX_TEST_CHECK(read_blocks(&source, 12U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(output[0] == 12U);

    GDOX_TEST_CHECK(read_blocks(&source, 20U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(audit.reads[1].lba == 20U);
    GDOX_TEST_CHECK(audit.reads[1].blocks == 4U);
    GDOX_TEST_CHECK(read_blocks(&source, 13U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(output[0] == 13U);
    GDOX_TEST_CHECK(read_blocks(&source, 21U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(read_blocks(&source, 22U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(read_blocks(&source, 25U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(audit.reads[2].lba == 25U);
    GDOX_TEST_CHECK(audit.reads[2].blocks == 1U);

    GDOX_TEST_CHECK(read_blocks(&source, 30U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 31U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 31U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 6U);
    GDOX_TEST_CHECK(audit.observe_calls == 1U);
    GDOX_TEST_CHECK(audit.reads[3].blocks == 1U);
    GDOX_TEST_CHECK(audit.reads[4].blocks == 1U);
    GDOX_TEST_CHECK(audit.reads[5].blocks == 1U);

    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_random_retention_and_per_slot_adjacency(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2] = {
        {10U, 20U, 0U},
        {40U, 8U, 0U},
    };
    gdox_xdvdfs_metadata metadata = {0};
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    metadata.file_extents = extents;
    metadata.file_extent_count = 2U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));

    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 18U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(audit.reads[1].blocks == 1U);
    GDOX_TEST_CHECK(read_blocks(&source, 40U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(read_blocks(&source, 18U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(output[0] == 18U);

    GDOX_TEST_CHECK(read_blocks(&source, 19U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 4U);
    GDOX_TEST_CHECK(audit.reads[3].lba == 19U);
    GDOX_TEST_CHECK(audit.reads[3].blocks == 4U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

static void test_cache_does_not_stitch_slots(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extent = {10U, 16U, 0U};
    gdox_xdvdfs_metadata metadata = {0};
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[2U * GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    metadata.file_extents = &extent;
    metadata.file_extent_count = 1U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));

    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 14U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(read_blocks(&source, 13U, 2U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(audit.reads[2].lba == 13U);
    GDOX_TEST_CHECK(audit.reads[2].blocks == 2U);
    GDOX_TEST_CHECK(output[0] == 13U);
    GDOX_TEST_CHECK(
        output[GDOX_LOGICAL_SECTOR_BYTES] == 14U
    );
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

static void test_speculative_failure_policy(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2];
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    audit.next_large_failure = GDOX_ERROR_IO;
    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(audit.reads[0].blocks == 4U);
    GDOX_TEST_CHECK(audit.reads[1].blocks == 1U);
    GDOX_TEST_CHECK(output[0] == 10U);
    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);
    GDOX_TEST_CHECK(read_blocks(&source, 12U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 3U);
    GDOX_TEST_CHECK(audit.reads[2].lba == 12U);
    GDOX_TEST_CHECK(audit.reads[2].blocks == 1U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    audit.next_large_failure = GDOX_ERROR_CANCELLED;
    GDOX_TEST_CHECK(!read_blocks(&source, 20U, 1U, output, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    audit.next_large_failure = GDOX_ERROR_IO;
    audit.observation.readiness = GDOX_MEDIA_READINESS_ABSENT;
    audit.observation.event = GDOX_MEDIA_EVENT_REMOVAL;
    GDOX_TEST_CHECK(!read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 1U);
    audit.observation.readiness = GDOX_MEDIA_READINESS_PRESENT;
    audit.observation.event = GDOX_MEDIA_EVENT_NONE;
    GDOX_TEST_CHECK(!read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

static void test_mid_file_first_touch_stops_at_extent_end(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2];
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    GDOX_TEST_CHECK(read_blocks(&source, 11U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(audit.reads[0].lba == 11U);
    GDOX_TEST_CHECK(audit.reads[0].blocks == 4U);
    GDOX_TEST_CHECK(audit.reads[0].lba + audit.reads[0].blocks == 15U);
    GDOX_TEST_CHECK(read_blocks(&source, 14U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 1U);
    GDOX_TEST_CHECK(output[0] == 14U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

static void test_lru_eviction_and_once_per_extent_speculation(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extent = {
        0U,
        LRU_TEST_SECTORS,
        0U,
    };
    gdox_xdvdfs_metadata metadata = {0};
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    size_t index;
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit_sectors(
        &audit,
        LRU_TEST_SECTORS
    ));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    metadata.file_extents = &extent;
    metadata.file_extent_count = 1U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner,
        &metadata,
        LRU_WINDOW_BLOCKS,
        &source,
        &error
    ));

    for (index = 0U; index < LRU_SLOT_COUNT; ++index) {
        GDOX_TEST_CHECK(read_blocks(
            &source,
            (uint64_t)index * LRU_WINDOW_BLOCKS,
            1U,
            output,
            &error
        ));
    }
    GDOX_TEST_CHECK(audit.read_count == LRU_SLOT_COUNT);

    GDOX_TEST_CHECK(read_blocks(&source, 0U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == LRU_SLOT_COUNT);
    GDOX_TEST_CHECK(read_blocks(
        &source,
        (uint64_t)LRU_SLOT_COUNT * LRU_WINDOW_BLOCKS,
        1U,
        output,
        &error
    ));
    GDOX_TEST_CHECK(audit.read_count == LRU_SLOT_COUNT + 1U);
    GDOX_TEST_CHECK(read_blocks(&source, 0U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == LRU_SLOT_COUNT + 1U);

    GDOX_TEST_CHECK(read_blocks(&source, 513U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == LRU_SLOT_COUNT + 2U);
    GDOX_TEST_CHECK(
        audit.reads[LRU_SLOT_COUNT + 1U].lba == 513U
    );
    GDOX_TEST_CHECK(
        audit.reads[LRU_SLOT_COUNT + 1U].blocks == 1U
    );
    GDOX_TEST_CHECK(read_blocks(&source, 513U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == LRU_SLOT_COUNT + 2U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

static void test_unknown_media_invalidates_all_slots(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2];
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    gdox_media_observation observation;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 20U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 2U);

    audit.observation.readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    GDOX_TEST_CHECK(gdox_source_observe_media(
        &source,
        &observation
    ));
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_UNKNOWN
    );
    audit.observation.readiness = GDOX_MEDIA_READINESS_PRESENT;
    GDOX_TEST_CHECK(read_blocks(&source, 11U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 21U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 4U);
    GDOX_TEST_CHECK(audit.reads[2].blocks == 1U);
    GDOX_TEST_CHECK(audit.reads[3].blocks == 1U);
    GDOX_TEST_CHECK(read_blocks(&source, 11U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 21U, 1U, output, &error));
    GDOX_TEST_CHECK(audit.read_count == 4U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

static void test_media_abort_and_ownership(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2];
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    gdox_disc_evidence evidence;
    gdox_physical_read_stats stats;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    size_t reads_before;
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(read_blocks(&source, 20U, 1U, output, &error));
    reads_before = audit.read_count;
    audit.observation.generation = 8U;
    GDOX_TEST_CHECK(source.ops->media_present(source.context));
    GDOX_TEST_CHECK(!read_blocks(&source, 12U, 1U, output, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(audit.read_count == reads_before);
    GDOX_TEST_CHECK(gdox_source_evidence(&source, &evidence));
    GDOX_TEST_CHECK(evidence.dmi_present);
    GDOX_TEST_CHECK(gdox_source_physical_read_stats(&source, &stats));
    GDOX_TEST_CHECK(stats.commands == audit.read_count);

    gdox_source_abort(&source);
    GDOX_TEST_CHECK(audit.abort_calls == 1U);
    GDOX_TEST_CHECK(!read_blocks(&source, 12U, 1U, output, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    GDOX_TEST_CHECK(audit.read_count == reads_before);
    audit.prepare_failures = 1U;
    GDOX_TEST_CHECK(!gdox_source_close(&source, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(audit.prepare_calls >= 2U);
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_disabled_passthrough(void)
{
    readahead_audit audit;
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    void *original_context;
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    memset(&metadata, 0, sizeof(metadata));
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    original_context = inner.context;
    GDOX_TEST_CHECK(!gdox_source_make_file_readahead(
        &inner, &metadata, 513U, &source, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, NULL, 0U, &source, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(source.context == original_context);
    GDOX_TEST_CHECK(source.ops == &memory_ops);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);
    free(audit.bytes);
}

static void test_initial_new_media_is_the_baseline(void)
{
    readahead_audit audit;
    gdox_xdvdfs_file_extent extents[2];
    gdox_xdvdfs_metadata metadata;
    gdox_sector_source inner = {0};
    gdox_sector_source source = {0};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error error;

    GDOX_TEST_CHECK(initialize_audit(&audit));
    audit.observation.event = GDOX_MEDIA_EVENT_NEW_MEDIA;
    initialize_metadata(&metadata, extents);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_file_readahead(
        &inner, &metadata, 4U, &source, &error
    ));
    GDOX_TEST_CHECK(read_blocks(&source, 10U, 1U, output, &error));
    GDOX_TEST_CHECK(output[0] == 10U);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    free(audit.bytes);
}

void gdox_test_file_readahead_source(void)
{
    test_sequential_extent_reads();
    test_random_retention_and_per_slot_adjacency();
    test_cache_does_not_stitch_slots();
    test_speculative_failure_policy();
    test_mid_file_first_touch_stops_at_extent_end();
    test_lru_eviction_and_once_per_extent_speculation();
    test_unknown_media_invalidates_all_slots();
    test_media_abort_and_ownership();
    test_disabled_passthrough();
    test_initial_new_media_is_the_baseline();
}
