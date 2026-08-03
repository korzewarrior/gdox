#include "app/runtime_media.h"

#include "gdox/disc.h"
#include "gdox/sector.h"
#include "gdox/xenia.h"

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
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

#define TEST_GAME_OFFSET UINT64_C(0x0000fb20)
#define TEST_IMAGE_SECTORS UINT64_C(96)
#define TEST_ROOT_SECTOR UINT32_C(40)
#define TEST_ROOT_BYTES UINT32_C(192)
#define TEST_DEFAULT_SECTOR UINT32_C(50)
#define TEST_MODULE_SECTOR UINT32_C(52)
#define TEST_EXECUTABLE_BYTES UINT32_C(128)
#define TEST_SECOND_NODE UINT16_C(12)

static const uint8_t gdfx_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

static bool check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "failed: %s\n", message);
        return false;
    }
    return true;
}

typedef struct runtime_close_audit {
    unsigned int prepare_calls;
    unsigned int close_calls;
    unsigned int prepare_failures;
} runtime_close_audit;

static uint64_t retry_disc_length(const void *context)
{
    (void)context;
    return GDOX_LOGICAL_SECTOR_BYTES;
}

static bool retry_disc_read(
    void *context,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    (void)context;
    (void)offset;
    (void)output;
    (void)output_bytes;
    *read_bytes = 0U;
    gdox_error_clear(error);
    return true;
}

static bool retry_disc_close(void *context, gdox_error *error)
{
    runtime_close_audit *audit = context;
    ++audit->close_calls;
    gdox_error_clear(error);
    return true;
}

static bool retry_disc_prepare_close(void *context, gdox_error *error)
{
    runtime_close_audit *audit = context;
    ++audit->prepare_calls;
    if (audit->prepare_failures != 0U) {
        --audit->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated runtime close failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_random_disc_ops retry_disc_ops = {
    retry_disc_length,
    retry_disc_read,
    NULL,
    retry_disc_close,
    NULL,
    NULL,
    retry_disc_prepare_close,
    NULL,
};

static uint64_t retry_source_sectors(const void *context)
{
    (void)context;
    return 1U;
}

static bool retry_source_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    (void)context;
    (void)lba;
    (void)blocks;
    (void)output;
    (void)output_bytes;
    gdox_error_clear(error);
    return true;
}

static bool retry_source_close(void *context, gdox_error *error)
{
    runtime_close_audit *audit = context;
    ++audit->close_calls;
    gdox_error_clear(error);
    return true;
}

static bool retry_source_prepare_close(void *context, gdox_error *error)
{
    runtime_close_audit *audit = context;
    ++audit->prepare_calls;
    if (audit->prepare_failures != 0U) {
        --audit->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated source restore failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops retry_source_ops = {
    retry_source_sectors,
    retry_source_read,
    NULL,
    retry_source_close,
    NULL,
    NULL,
    NULL,
    retry_source_prepare_close,
    NULL,
};

static bool test_runtime_close_retry(void)
{
    runtime_close_audit audit = {0};
    gdox_runtime_media_session session = {0};
    gdox_error error;

    audit.prepare_failures = 1U;
    session.open = true;
    session.validated_disc.context = &audit;
    session.validated_disc.ops = &retry_disc_ops;
    if (!check(
            !gdox_runtime_media_close(&session, &error)
                && error.code == GDOX_ERROR_IO,
            "retain media session after failed close preparation"
        ) || !check(
            !session.open && gdox_runtime_media_is_owned(&session)
                && gdox_disc_is_valid(&session.validated_disc)
                && audit.close_calls == 0U,
            "retain retryable disc ownership"
        ) || !check(
            gdox_runtime_media_close(&session, &error),
            "retry media session close"
        ) || !check(
            !session.open && !gdox_runtime_media_is_owned(&session)
                && !gdox_disc_is_valid(&session.validated_disc)
                && audit.prepare_calls == 2U && audit.close_calls == 1U,
            "consume media session exactly once"
        )) {
        return false;
    }
    return true;
}

static bool test_runtime_retained_source_retry(void)
{
    runtime_close_audit audit = {0};
    gdox_runtime_media_session session = {0};
    gdox_sector_source source = {
        .context = &audit,
        .ops = &retry_source_ops,
    };
    gdox_error error;

    audit.prepare_failures = 1U;
    if (!check(
            gdox_runtime_media_retain_cleanup_source(
                &session, &source, &error
            ),
            "move failed-open source into runtime cleanup"
        ) || !check(
            !gdox_source_is_valid(&source)
                && gdox_runtime_media_is_owned(&session)
                && !session.open,
            "retain cleanup without publishing a playable session"
        ) || !check(
            !gdox_runtime_media_close(&session, &error)
                && error.code == GDOX_ERROR_IO,
            "report retained-source restore failure"
        ) || !check(
            gdox_runtime_media_is_owned(&session)
                && gdox_source_is_valid(&session.retained_source)
                && audit.close_calls == 0U,
            "preserve source ownership after failed restore"
        ) || !check(
            gdox_runtime_media_close(&session, &error),
            "retry retained-source cleanup"
        ) || !check(
            !gdox_runtime_media_is_owned(&session)
                && audit.prepare_calls == 2U && audit.close_calls == 1U,
            "consume retained source after verified cleanup"
        )) {
        return false;
    }
    return true;
}

static void put_le_u16(uint8_t output[2], uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void put_le_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void put_be_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void set_entry(
    uint8_t *entry,
    uint16_t left,
    uint16_t right,
    uint32_t sector,
    const char *name
)
{
    const size_t name_bytes = strlen(name);

    memset(entry, 0, 48U);
    put_le_u16(entry, left);
    put_le_u16(entry + 2U, right);
    put_le_u32(entry + 4U, sector);
    put_le_u32(entry + 8U, TEST_EXECUTABLE_BYTES);
    entry[13] = (uint8_t)name_bytes;
    memcpy(entry + 14U, name, name_bytes);
}

static void set_execution_info(uint8_t *executable)
{
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
}

static uint8_t *make_image(size_t *byte_count)
{
    const size_t bytes =
        (size_t)TEST_IMAGE_SECTORS * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t descriptor_offset = TEST_GAME_OFFSET
        + UINT64_C(32) * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t root_offset = TEST_GAME_OFFSET
        + (uint64_t)TEST_ROOT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t default_offset = TEST_GAME_OFFSET
        + (uint64_t)TEST_DEFAULT_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t module_offset = TEST_GAME_OFFSET
        + (uint64_t)TEST_MODULE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *image = calloc(bytes, 1U);
    uint8_t *descriptor;
    uint8_t *root;

    if (image == NULL
        || module_offset + TEST_EXECUTABLE_BYTES > bytes) {
        free(image);
        return NULL;
    }
    descriptor = image + (size_t)descriptor_offset;
    root = image + (size_t)root_offset;
    memcpy(descriptor, gdfx_magic, sizeof(gdfx_magic));
    memcpy(
        descriptor + GDOX_LOGICAL_SECTOR_BYTES - sizeof(gdfx_magic),
        gdfx_magic,
        sizeof(gdfx_magic)
    );
    put_le_u32(descriptor + 20U, TEST_ROOT_SECTOR);
    put_le_u32(descriptor + 24U, TEST_ROOT_BYTES);
    set_entry(
        root,
        0U,
        TEST_SECOND_NODE,
        TEST_DEFAULT_SECTOR,
        "default.xex"
    );
    set_entry(
        root + 48U,
        0U,
        0U,
        TEST_MODULE_SECTOR,
        "scimitar_final.xex"
    );
    set_execution_info(image + (size_t)default_offset);
    set_execution_info(image + (size_t)module_offset);
    *byte_count = bytes;
    return image;
}

static bool write_file(const char *path, const uint8_t *data, size_t bytes)
{
    FILE *file = fopen(path, "wb");
    bool success;

    if (file == NULL) {
        return false;
    }
    success = fwrite(data, 1U, bytes, file) == bytes;
    return fclose(file) == 0 && success;
}

static bool xenia_media_target_ready(void)
{
    gdox_error error;

    if (gdox_xenia_target_supported(GDOX_XENIA_TARGET_PRIVATE_NBD)) {
        return gdox_xenia_target_preflight(
            GDOX_XENIA_TARGET_PRIVATE_NBD, &error
        );
    }
    return gdox_xenia_target_supported(GDOX_XENIA_TARGET_IMAGE)
        && gdox_xenia_target_preflight(GDOX_XENIA_TARGET_IMAGE, &error);
}

#if !defined(_WIN32)
static bool write_byte(const char *path, uint64_t offset, uint8_t value)
{
    FILE *file = fopen(path, "r+b");
    bool success = false;

    if (file != NULL && offset <= (uint64_t)LONG_MAX
        && fseek(file, (long)offset, SEEK_SET) == 0
        && fputc((int)value, file) != EOF) {
        success = true;
    }
    return file != NULL && fclose(file) == 0 && success;
}
#endif

static bool test_xenia_media_session(void)
{
    char path[160];
    char held_path[160];
    char request_path[160];
    size_t image_bytes = 0U;
    uint8_t *image = make_image(&image_bytes);
    uint8_t *replacement = calloc(image_bytes, 1U);
    gdox_runtime_media_session session = {0};
    gdox_runtime_media_open_result result;
    gdox_xenia_target target;
    gdox_error error;
#if !defined(_WIN32)
    const uint64_t module_platform_offset = TEST_GAME_OFFSET
        + (uint64_t)TEST_MODULE_SECTOR * GDOX_LOGICAL_SECTOR_BYTES + 64U;
#endif
    bool created = false;
    bool renamed = false;
    bool success = false;
    bool cleaned = true;

    (void)snprintf(
        path, sizeof(path), "gdox-runtime-media-%d.tmp", gdox_test_getpid()
    );
    (void)snprintf(
        held_path,
        sizeof(held_path),
        "gdox-runtime-media-held-%d.tmp",
        gdox_test_getpid()
    );
    (void)remove(path);
    (void)remove(held_path);
    if (!check(image != NULL && replacement != NULL, "allocate media fixture")
        || !check(write_file(path, image, image_bytes), "write media fixture")) {
        goto cleanup;
    }
    created = true;
    (void)snprintf(request_path, sizeof(request_path), "%s", path);
    if (!xenia_media_target_ready()) {
        success = check(
            !gdox_runtime_media_open_image(
                request_path, &session, &result, &error
            )
                && (error.code == GDOX_ERROR_UNSUPPORTED
                    || error.code == GDOX_ERROR_NOT_FOUND)
                && result.state == GDOX_RUNTIME_MEDIA_IDENTIFIED
                && result.info.backend == GDOX_MEDIA_BACKEND_XENIA,
            "reject Xbox 360 image before session commit"
        );
        goto cleanup;
    }
    if (!check(
            gdox_runtime_media_open_image(
                request_path, &session, &result, &error
            ),
            "open validated Xbox 360 media session"
        ) || !check(session.open, "commit media session")
        || !check(
            result.state == GDOX_RUNTIME_MEDIA_READY
                && result.info.platform == GDOX_MEDIA_PLATFORM_XBOX_360
                && result.info.backend == GDOX_MEDIA_BACKEND_XENIA,
            "publish ready Xbox 360 identity"
        )
        || !check(
            gdox_disc_is_valid(&session.validated_disc),
            "retain validated image ownership"
        ) || !check(
            strcmp(session.image_path, path) == 0,
            "retain stable image path"
        ) || !check(
            session.info.xenia_module_kind == GDOX_X360_EXECUTABLE_XEX2,
            "retain reviewed launch-module kind"
        ) || !check(
            session.info.xenia_module_execution.platform == 2U
                && session.info.xenia_module_execution.executable_type == 3U,
            "retain complete launch-module identity"
        )) {
        goto cleanup;
    }
    memset(request_path, 'x', sizeof(request_path));
    request_path[sizeof(request_path) - 1U] = '\0';
    if (!check(
            strcmp(session.image_path, path) == 0,
            "ignore caller path mutation"
        )) {
        goto cleanup;
    }
#if !defined(_WIN32)
    if (!check(
            write_byte(path, module_platform_offset, 1U),
            "mutate launch-module platform"
        ) || !check(
            !gdox_runtime_media_prepare_xenia_target(
                &session, &target, &error
            ) && error.code == GDOX_ERROR_INVALID_VOLUME,
            "reject changed launch-module identity"
        ) || !check(
            write_byte(path, module_platform_offset, 2U),
            "restore launch-module platform"
        )) {
        goto cleanup;
    }
#endif
#if defined(__linux__)
    if (!check(rename(path, held_path) == 0, "move validated image path")) {
        goto cleanup;
    }
    renamed = true;
    if (!check(
            write_file(path, replacement, image_bytes),
            "replace original image path"
        )) {
        goto cleanup;
    }
#endif
    if (!check(
            gdox_runtime_media_prepare_xenia_target(
                &session, &target, &error
            ),
            "prepare fully revalidated Xenia target"
        )) {
        goto cleanup;
    }
    if (gdox_xenia_runtime_target_supported(
            session.info.xenia_policy->runtime,
            GDOX_XENIA_TARGET_PRIVATE_NBD
        )) {
        if (!check(
                target.kind == GDOX_XENIA_TARGET_PRIVATE_NBD,
                "move the exact validated disc into private NBD"
            ) || !check(
                session.exported != NULL
                    && !gdox_disc_is_valid(&session.validated_disc),
                "transfer validated ownership exactly once"
            ) || !check(
                gdox_runtime_media_prepare_xenia_target(
                    &session, &target, &error
                ),
                "revalidate the owned NBD disc for restart"
            )) {
            goto cleanup;
        }
    } else if (!check(
            target.kind == GDOX_XENIA_TARGET_IMAGE
                && strcmp(target.location, session.image_path) == 0,
            "launch the guarded image path"
        )) {
        goto cleanup;
    }
    success = true;

cleanup:
    if ((session.open || session.exported != NULL
            || gdox_disc_is_valid(&session.validated_disc))
        && !gdox_runtime_media_close(&session, &error)) {
        cleaned = false;
    }
    if (created && remove(path) != 0) {
        cleaned = false;
    }
    if (renamed && remove(held_path) != 0) {
        cleaned = false;
    }
    free(replacement);
    free(image);
    return success && check(cleaned, "clean media fixture");
}

int main(void)
{
    return test_runtime_close_retry()
            && test_runtime_retained_source_retry()
            && test_xenia_media_session()
        ? 0
        : 1;
}
