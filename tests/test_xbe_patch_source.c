#include "test.h"

#include "core/xbe_patch_source.h"
#include "gdox/disc.h"

#include <stdlib.h>
#include <string.h>

#define TEST_XBE_SECTORS UINT32_C(1024)
#define TEST_SECONDARY_SECTOR UINT32_C(1050)
#define TEST_SOURCE_SECTORS UINT64_C(1100)
#define TEST_BOUNDARY_SECTORS UINT32_C(32)

typedef struct patch_audit {
    uint8_t *bytes;
    uint64_t sectors;
    gdox_media_observation observation;
    gdox_physical_read_stats stats;
    gdox_disc_evidence evidence;
    uint64_t fail_lba;
    uint64_t secondary_reads;
    uint64_t read_sectors;
    unsigned int read_calls;
    unsigned int max_blocks;
    unsigned int abort_calls;
    unsigned int prepare_calls;
    unsigned int close_calls;
    unsigned int prepare_failures;
    bool fail_once;
    bool prepared;
} patch_audit;

typedef struct patch_memory_context {
    patch_audit *audit;
} patch_memory_context;

static const uint8_t media_check[8] = {
    0xe8U, 0xcaU, 0xfdU, 0xffU, 0xffU, 0x85U, 0xc0U, 0x7dU,
};

static uint64_t memory_sector_count(const void *raw_context)
{
    const patch_memory_context *context = raw_context;
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
    patch_memory_context *context = raw_context;
    patch_audit *audit = context->audit;

    ++audit->read_calls;
    audit->read_sectors += blocks;
    if (blocks > audit->max_blocks) {
        audit->max_blocks = blocks;
    }
    if (lba <= TEST_SECONDARY_SECTOR
        && TEST_SECONDARY_SECTOR < lba + blocks) {
        ++audit->secondary_reads;
    }
    if (audit->fail_once && lba == audit->fail_lba) {
        audit->fail_once = false;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated XBE read failure");
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
    const patch_memory_context *context = raw_context;
    return context->audit->observation.readiness
        == GDOX_MEDIA_READINESS_PRESENT;
}

static void memory_observe(
    const void *raw_context,
    gdox_media_observation *output
)
{
    const patch_memory_context *context = raw_context;
    *output = context->audit->observation;
}

static bool memory_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const patch_memory_context *context = raw_context;
    *output = context->audit->evidence;
    return true;
}

static bool memory_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const patch_memory_context *context = raw_context;
    *output = context->audit->stats;
    return true;
}

static void memory_abort(void *raw_context)
{
    patch_memory_context *context = raw_context;
    ++context->audit->abort_calls;
}

static bool memory_prepare(void *raw_context, gdox_error *error)
{
    patch_memory_context *context = raw_context;
    patch_audit *audit = context->audit;

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
    patch_memory_context *context = raw_context;
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
    patch_audit *audit,
    gdox_sector_source *source
)
{
    patch_memory_context *context = malloc(sizeof(*context));

    if (context == NULL) {
        return false;
    }
    context->audit = audit;
    source->context = context;
    source->ops = &memory_ops;
    return true;
}

static void initialize_audit(patch_audit *audit, uint64_t sectors)
{
    memset(audit, 0, sizeof(*audit));
    audit->sectors = sectors;
    audit->bytes = calloc(
        (size_t)sectors, GDOX_LOGICAL_SECTOR_BYTES
    );
    audit->observation.readiness = GDOX_MEDIA_READINESS_PRESENT;
}

static void place_signature(patch_audit *audit, uint64_t offset)
{
    memcpy(audit->bytes + (size_t)offset, media_check, sizeof(media_check));
}

static void test_request_local_patch_is_bounded_and_stable(void)
{
    const uint64_t xbe_offset = GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t xbe_bytes =
        (uint64_t)TEST_XBE_SECTORS * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t crossing =
        xbe_offset
        + (uint64_t)TEST_BOUNDARY_SECTORS * GDOX_LOGICAL_SECTOR_BYTES - 4U;
    const uint64_t tail = xbe_offset + xbe_bytes - sizeof(media_check);
    patch_audit audit;
    gdox_xdvdfs_entry entries[] = {
        {.start_sector = 1U, .size = (uint32_t)xbe_bytes},
        {.start_sector = TEST_SECONDARY_SECTOR,
         .size = GDOX_LOGICAL_SECTOR_BYTES},
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = entries,
        .xbe_file_count = sizeof(entries) / sizeof(entries[0]),
    };
    gdox_sector_source inner = {0};
    gdox_sector_source patched = {0};
    gdox_random_disc disc = {0};
    unsigned int reads_after_first;
    uint8_t output = 0U;
    size_t read_bytes = 0U;
    gdox_error error;

    initialize_audit(&audit, TEST_SOURCE_SECTORS);
    GDOX_TEST_CHECK(audit.bytes != NULL);
    memcpy(audit.bytes + (size_t)xbe_offset, "XBEH", 4U);
    memcpy(
        audit.bytes
            + (size_t)TEST_SECONDARY_SECTOR * GDOX_LOGICAL_SECTOR_BYTES,
        "XBEH",
        4U
    );
    place_signature(&audit, crossing);
    place_signature(&audit, tail);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(audit.read_calls == 0U);
    GDOX_TEST_CHECK(gdox_disc_from_source(&patched, &disc, &error));

    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc,
        crossing + 7U,
        &output,
        1U,
        &read_bytes,
        &error
    ));
    GDOX_TEST_CHECK(read_bytes == 1U);
    GDOX_TEST_CHECK(output == 0xebU);
    GDOX_TEST_CHECK(audit.max_blocks == 1U);
    GDOX_TEST_CHECK(audit.read_sectors == 3U);
    GDOX_TEST_CHECK(audit.secondary_reads == 0U);
    reads_after_first = audit.read_calls;

    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc, tail + 7U, &output, 1U, &read_bytes, &error
    ));
    GDOX_TEST_CHECK(output == 0xebU);
    GDOX_TEST_CHECK(audit.read_calls == reads_after_first + 1U);
    GDOX_TEST_CHECK(audit.bytes[(size_t)crossing + 7U] == 0x7dU);
    GDOX_TEST_CHECK(audit.bytes[(size_t)tail + 7U] == 0x7dU);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    free(audit.bytes);
}

static void test_header_failure_retries(void)
{
    const uint32_t xbe_sectors = 4U * TEST_BOUNDARY_SECTORS;
    const uint64_t xbe_offset = GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t tail = xbe_offset
        + (uint64_t)xbe_sectors * GDOX_LOGICAL_SECTOR_BYTES
        - sizeof(media_check);
    patch_audit audit;
    gdox_xdvdfs_entry entry = {
        .start_sector = 1U,
        .size = xbe_sectors * GDOX_LOGICAL_SECTOR_BYTES,
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = &entry,
        .xbe_file_count = 1U,
    };
    gdox_sector_source inner = {0};
    gdox_sector_source patched = {0};
    gdox_random_disc disc = {0};
    uint8_t output = 0U;
    size_t read_bytes = 99U;
    gdox_error error;

    initialize_audit(&audit, 1U + xbe_sectors);
    GDOX_TEST_CHECK(audit.bytes != NULL);
    memcpy(audit.bytes + (size_t)xbe_offset, "XBEH", 4U);
    place_signature(&audit, tail);
    audit.fail_lba = 1U;
    audit.fail_once = true;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(gdox_disc_from_source(&patched, &disc, &error));
    GDOX_TEST_CHECK(!gdox_disc_read_at(
        &disc, tail + 7U, &output, 1U, &read_bytes, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(read_bytes == 0U);
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc, tail + 7U, &output, 1U, &read_bytes, &error
    ));
    GDOX_TEST_CHECK(output == 0xebU);
    GDOX_TEST_CHECK(audit.max_blocks == 1U);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    free(audit.bytes);
}

static void test_non_xbe_is_not_patched(void)
{
    const uint64_t xbe_offset = GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t signature =
        xbe_offset + 2U * GDOX_LOGICAL_SECTOR_BYTES - 4U;
    patch_audit audit;
    gdox_xdvdfs_entry entry = {
        .start_sector = 1U,
        .size = 4U * GDOX_LOGICAL_SECTOR_BYTES,
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = &entry,
        .xbe_file_count = 1U,
    };
    gdox_sector_source inner = {0};
    gdox_sector_source patched = {0};
    gdox_random_disc disc = {0};
    unsigned int reads_after_first;
    uint8_t output = 0U;
    size_t read_bytes = 0U;
    gdox_error error;

    initialize_audit(&audit, 8U);
    GDOX_TEST_CHECK(audit.bytes != NULL);
    memcpy(audit.bytes + (size_t)xbe_offset, "NOPE", 4U);
    place_signature(&audit, signature);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(gdox_disc_from_source(&patched, &disc, &error));
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc, signature + 7U, &output, 1U, &read_bytes, &error
    ));
    GDOX_TEST_CHECK(output == 0x7dU);
    GDOX_TEST_CHECK(audit.read_calls == 2U);
    reads_after_first = audit.read_calls;
    GDOX_TEST_CHECK(gdox_disc_read_at(
        &disc, signature + 7U, &output, 1U, &read_bytes, &error
    ));
    GDOX_TEST_CHECK(output == 0x7dU);
    GDOX_TEST_CHECK(audit.read_calls == reads_after_first + 1U);
    GDOX_TEST_CHECK(gdox_disc_close(&disc, &error));
    free(audit.bytes);
}

static void test_lifecycle_and_extent_validation(void)
{
    patch_audit audit;
    gdox_xdvdfs_entry entry = {
        .start_sector = 1U,
        .size = GDOX_LOGICAL_SECTOR_BYTES,
    };
    gdox_xdvdfs_entry duplicates[] = {
        {.start_sector = 1U, .size = GDOX_LOGICAL_SECTOR_BYTES},
        {.start_sector = 1U, .size = GDOX_LOGICAL_SECTOR_BYTES},
    };
    gdox_xdvdfs_entry overlaps[] = {
        {.start_sector = 1U, .size = 2U * GDOX_LOGICAL_SECTOR_BYTES},
        {.start_sector = 2U, .size = GDOX_LOGICAL_SECTOR_BYTES},
    };
    gdox_xdvdfs_entry large = {
        .start_sector = 1U,
        .size = 64U * 1024U * 1024U + 1U,
    };
    gdox_xdvdfs_metadata metadata = {
        .xbe_files = &entry,
        .xbe_file_count = 1U,
    };
    gdox_sector_source inner = {0};
    gdox_sector_source patched = {0};
    gdox_media_observation observation;
    gdox_physical_read_stats stats;
    gdox_disc_evidence evidence;
    gdox_error error;

    initialize_audit(&audit, 4U);
    GDOX_TEST_CHECK(audit.bytes != NULL);
    audit.observation.generation = 17U;
    audit.observation.event = GDOX_MEDIA_EVENT_NEW_MEDIA;
    audit.stats.commands = 23U;
    audit.stats.sectors = 29U;
    audit.stats.bytes = 31U;
    audit.stats.last_lba = 37U;
    audit.evidence.pfi_present = true;
    memcpy(audit.evidence.note, "test evidence", 14U);
    audit.prepare_failures = 1U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(gdox_source_media_present(&patched));
    GDOX_TEST_CHECK(gdox_source_observe_media(&patched, &observation));
    GDOX_TEST_CHECK(observation.generation == 17U);
    GDOX_TEST_CHECK(observation.event == GDOX_MEDIA_EVENT_NEW_MEDIA);
    GDOX_TEST_CHECK(gdox_source_physical_read_stats(&patched, &stats));
    GDOX_TEST_CHECK(stats.commands == 23U);
    GDOX_TEST_CHECK(stats.last_lba == 37U);
    GDOX_TEST_CHECK(gdox_source_evidence(&patched, &evidence));
    GDOX_TEST_CHECK(evidence.pfi_present);
    GDOX_TEST_CHECK(strcmp(evidence.note, "test evidence") == 0);
    gdox_source_abort(&patched);
    GDOX_TEST_CHECK(audit.abort_calls == 1U);
    GDOX_TEST_CHECK(!gdox_source_close(&patched, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_source_is_valid(&patched));
    GDOX_TEST_CHECK(audit.close_calls == 0U);
    GDOX_TEST_CHECK(gdox_source_close(&patched, &error));
    GDOX_TEST_CHECK(audit.close_calls == 1U);

    metadata.xbe_files = duplicates;
    metadata.xbe_file_count = sizeof(duplicates) / sizeof(duplicates[0]);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(gdox_source_close(&patched, &error));

    metadata.xbe_files = overlaps;
    metadata.xbe_file_count = sizeof(overlaps) / sizeof(overlaps[0]);
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(!gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_VOLUME);
    GDOX_TEST_CHECK(gdox_source_is_valid(&inner));
    GDOX_TEST_CHECK(gdox_source_close(&inner, &error));

    audit.sectors = 1U
        + ((uint64_t)large.size + GDOX_LOGICAL_SECTOR_BYTES - 1U)
            / GDOX_LOGICAL_SECTOR_BYTES;
    metadata.xbe_files = &large;
    metadata.xbe_file_count = 1U;
    GDOX_TEST_CHECK(make_memory_source(&audit, &inner));
    GDOX_TEST_CHECK(gdox_source_make_xbe_patch_source(
        &inner, &metadata, &patched, &error
    ));
    GDOX_TEST_CHECK(audit.read_calls == 0U);
    GDOX_TEST_CHECK(gdox_source_close(&patched, &error));
    free(audit.bytes);
}

void gdox_test_xbe_patch_source(void)
{
    test_request_local_patch_is_bounded_and_stable();
    test_header_failure_retries();
    test_non_xbe_is_not_patched();
    test_lifecycle_and_extent_validation();
}
