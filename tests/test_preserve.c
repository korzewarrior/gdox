#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "gdox/preserve.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_rmdir _rmdir
#define gdox_test_unlink _unlink
#else
#include <sys/stat.h>
#include <unistd.h>
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_rmdir rmdir
#define gdox_test_unlink unlink
#endif

typedef struct pattern_source {
    uint64_t sectors;
    uint64_t bad_lba;
    bool has_bad_lba;
    gdox_disc_evidence evidence;
} pattern_source;

typedef struct cancellation_context {
    bool cancelled;
} cancellation_context;

static uint64_t pattern_count(const void *context)
{
    return ((const pattern_source *)context)->sectors;
}

static bool pattern_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    pattern_source *pattern = context;
    uint32_t index;
    if (!gdox_source_validate_read(
            pattern->sectors,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    if (pattern->has_bad_lba
        && pattern->bad_lba >= lba
        && pattern->bad_lba < lba + blocks) {
        gdox_error_set(error, GDOX_ERROR_IO, "synthetic unreadable sector");
        return false;
    }
    for (index = 0U; index < blocks; ++index) {
        memset(
            output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES,
            (int)((lba + index) & UINT64_C(0xff)),
            GDOX_LOGICAL_SECTOR_BYTES
        );
    }
    return true;
}

static bool pattern_present(const void *context)
{
    (void)context;
    return true;
}

static bool pattern_close(void *context, gdox_error *error)
{
    gdox_error_clear(error);
    free(context);
    return true;
}

static bool pattern_evidence(
    const void *context,
    gdox_disc_evidence *output
)
{
    const pattern_source *pattern = context;
    *output = pattern->evidence;
    return pattern->evidence.pfi_present
        || pattern->evidence.dmi_present
        || pattern->evidence.security_sector_present;
}

static const gdox_sector_source_ops pattern_ops = {
    pattern_count,
    pattern_read,
    pattern_present,
    pattern_close,
    pattern_evidence,
    NULL,
    NULL,
};

static bool make_pattern_source(
    uint64_t sectors,
    bool has_bad_lba,
    uint64_t bad_lba,
    gdox_sector_source *source
)
{
    pattern_source *context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        return false;
    }
    context->sectors = sectors;
    context->has_bad_lba = has_bad_lba;
    context->bad_lba = bad_lba;
    source->context = context;
    source->ops = &pattern_ops;
    return true;
}

static bool cancelled(void *context)
{
    return ((const cancellation_context *)context)->cancelled;
}

static void cancel_after_read(
    void *context,
    const gdox_preservation_progress *progress
)
{
    cancellation_context *cancellation = context;
    if (progress->phase == GDOX_PRESERVATION_READING
        && progress->completed_bytes != 0U) {
        cancellation->cancelled = true;
    }
}

static bool temporary_directory(char output[256])
{
    unsigned int attempt;
    for (attempt = 0U; attempt < 100U; ++attempt) {
        (void)snprintf(
            output,
            256U,
            "./gdox-preserve-%d-%lld-%u",
            gdox_test_getpid(),
            (long long)time(NULL),
            attempt
        );
        if (gdox_test_mkdir(output) == 0) {
            return true;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    return false;
}

static void remove_bundle(const char *image)
{
    static const char *suffixes[] = {
        "", ".part", ".gdox.json", ".crc32", ".md5", ".sha1",
        ".sha256", ".log", ".pfi.bin", ".dmi.bin", ".ss.bin",
        ".security-map.json", ".dvd", ".sectors.txt",
    };
    size_t index;
    for (index = 0U; index < sizeof(suffixes) / sizeof(suffixes[0]); ++index) {
        char path[512];
        (void)snprintf(path, sizeof(path), "%s%s", image, suffixes[index]);
        (void)gdox_test_unlink(path);
    }
}

static bool file_contains(const char *path, const char *needle)
{
    FILE *file;
    char *text;
    long length;
    size_t bytes;
    bool found;

    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    length = ftell(file);
    if (length < 0L
        || length > 1024L * 1024L
        || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return false;
    }
    bytes = (size_t)length;
    text = calloc(bytes + 1U, 1U);
    if (text == NULL || fread(text, 1U, bytes, file) != bytes) {
        free(text);
        (void)fclose(file);
        return false;
    }
    found = strstr(text, needle) != NULL;
    free(text);
    return fclose(file) == 0 && found;
}

static void test_linear_image(const char *directory)
{
    char output[512];
    gdox_sector_source source = {0};
    gdox_preservation_request request;
    gdox_preservation_input input;
    gdox_preservation_result result;
    gdox_error error;
    FILE *file;
    uint8_t sector[GDOX_LOGICAL_SECTOR_BYTES];

    (void)snprintf(output, sizeof(output), "%s/linear.iso", directory);
    GDOX_TEST_CHECK(make_pattern_source(8U, true, 3U, &source));
    request = (gdox_preservation_request){
        GDOX_PRESERVATION_XISO_COMPACT,
        output,
        true,
        false,
        NULL,
    };
    input = (gdox_preservation_input){
        &source,
        91U,
        "Synthetic title",
        true,
        UINT32_C(0x4d530001),
        "test fixture",
        2U,
        0U,
        true,
    };
    GDOX_TEST_CHECK(gdox_preservation_run(
        &request,
        &input,
        NULL,
        NULL,
        NULL,
        &result,
        &error
    ));
    GDOX_TEST_CHECK(result.status == GDOX_PRESERVATION_PLAYABLE_XISO);
    GDOX_TEST_CHECK(result.readback_verified);
    GDOX_TEST_CHECK(result.bytes == UINT64_C(8) * GDOX_LOGICAL_SECTOR_BYTES);
    GDOX_TEST_CHECK(result.unreadable_sectors == 1U);
    GDOX_TEST_CHECK(result.unreadable_range_count == 1U);
    GDOX_TEST_CHECK(result.unreadable_ranges[0].start_lba == 94U);
    GDOX_TEST_CHECK(result.unreadable_ranges[0].end_lba == 94U);
    GDOX_TEST_CHECK(result.manifest_path[0] != '\0');
    GDOX_TEST_CHECK(file_contains(
        result.manifest_path,
        "\"compacted\":true"
    ));

    file = fopen(output, "rb");
    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(fseek(file, 3L * (long)GDOX_LOGICAL_SECTOR_BYTES, SEEK_SET) == 0);
    GDOX_TEST_CHECK(fread(sector, 1U, sizeof(sector), file) == sizeof(sector));
    GDOX_TEST_CHECK(fclose(file) == 0);
    for (size_t index = 0U; index < sizeof(sector); ++index) {
        GDOX_TEST_CHECK(sector[index] == 0U);
    }

    GDOX_TEST_CHECK(!gdox_preservation_run(
        &request,
        &input,
        NULL,
        NULL,
        NULL,
        &(gdox_preservation_result){0},
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    gdox_preservation_result_destroy(&result);
    gdox_source_destroy(&source);
    remove_bundle(output);
}

static void test_normalization_and_cancellation(const char *directory)
{
    char output[512];
    char part[520];
    gdox_sector_source source = {0};
    gdox_preservation_map map;
    gdox_preservation_request request;
    gdox_preservation_input input;
    gdox_preservation_result result;
    gdox_error error;
    cancellation_context cancellation = {false};
    FILE *file;
    uint8_t sector[GDOX_LOGICAL_SECTOR_BYTES];
    size_t index;

    memset(&map, 0, sizeof(map));
    map.source = GDOX_SECURITY_MAP_USER;
    for (index = 0U; index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
        map.ranges[index].start_lba = index * UINT64_C(8192);
        map.ranges[index].end_lba =
            map.ranges[index].start_lba + GDOX_XGD1_RANGE_SECTORS - 1U;
    }
    (void)snprintf(output, sizeof(output), "%s/cancel.iso", directory);
    (void)snprintf(part, sizeof(part), "%s.part", output);
    GDOX_TEST_CHECK(make_pattern_source(
        GDOX_XGD1_REDUMP_SECTORS,
        false,
        0U,
        &source
    ));
    request = (gdox_preservation_request){
        GDOX_PRESERVATION_REDUMP,
        output,
        false,
        true,
        &map,
    };
    input = (gdox_preservation_input){
        &source,
        0U,
        "Cancellation fixture",
        false,
        0U,
        "test fixture",
        0U,
        0U,
        false,
    };
    GDOX_TEST_CHECK(!gdox_preservation_run(
        &request,
        &input,
        cancelled,
        cancel_after_read,
        &cancellation,
        &result,
        &error
    ));
    if (error.code != GDOX_ERROR_CANCELLED) {
        (void)fprintf(
            stderr,
            "preservation cancellation returned %d: %s\n",
            (int)error.code,
            error.message
        );
    }
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_CANCELLED);
    file = fopen(part, "rb");
    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(fread(sector, 1U, sizeof(sector), file) == sizeof(sector));
    GDOX_TEST_CHECK(fclose(file) == 0);
    for (index = 0U; index < sizeof(sector); ++index) {
        GDOX_TEST_CHECK(sector[index] == 0U);
    }
    gdox_source_destroy(&source);
    remove_bundle(output);
}

void gdox_test_preserve(void)
{
    char directory[256];
    GDOX_TEST_CHECK(temporary_directory(directory));
    test_linear_image(directory);
    if (gdox_test_failures != 0) {
        return;
    }
    test_normalization_and_cancellation(directory);
    (void)gdox_test_rmdir(directory);
}
