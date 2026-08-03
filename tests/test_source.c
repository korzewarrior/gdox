#include "test.h"

#include "gdox/source.h"

#include <string.h>

typedef struct retry_source_context {
    unsigned int prepare_calls;
    unsigned int close_calls;
    unsigned int prepare_failures;
    unsigned int observe_calls;
    gdox_media_observation observation;
} retry_source_context;

static uint64_t retry_sector_count(const void *context)
{
    (void)context;
    return UINT64_C(4);
}

static bool retry_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    (void)context;
    if (!gdox_source_validate_read(
            UINT64_C(4),
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    memset(output, 0, output_bytes);
    return true;
}

static bool retry_present(const void *context)
{
    (void)context;
    return true;
}

static void retry_observe(
    const void *raw_context,
    gdox_media_observation *output
)
{
    retry_source_context *context = (retry_source_context *)raw_context;
    ++context->observe_calls;
    *output = context->observation;
}

static bool retry_close(void *raw_context, gdox_error *error)
{
    retry_source_context *context = raw_context;
    ++context->close_calls;
    gdox_error_clear(error);
    return true;
}

static bool retry_prepare_close(void *raw_context, gdox_error *error)
{
    retry_source_context *context = raw_context;
    ++context->prepare_calls;
    if (context->prepare_failures != 0U) {
        --context->prepare_failures;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated prepare failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops retry_ops = {
    retry_sector_count,
    retry_read,
    retry_present,
    retry_close,
    NULL,
    NULL,
    NULL,
    retry_prepare_close,
    retry_observe,
};

static void test_adapter_close_retry(void)
{
    retry_source_context context = {0};
    gdox_sector_source base = {&context, &retry_ops};
    gdox_sector_source partition = {0};
    gdox_sector_source patched = {0};
    const gdox_byte_patch patch = {0U, 0x5aU};
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_media_observation observation;
    gdox_error error;

    context.prepare_failures = 1U;
    context.observation.readiness = GDOX_MEDIA_READINESS_ABSENT;
    context.observation.generation = UINT64_C(42);
    GDOX_TEST_CHECK(gdox_source_make_partition(
        &base,
        UINT64_C(1),
        &partition,
        &error
    ));
    GDOX_TEST_CHECK(gdox_source_make_patched(
        &partition,
        &patch,
        1U,
        &patched,
        &error
    ));
    GDOX_TEST_CHECK(gdox_source_observe_media(&patched, &observation));
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_ABSENT
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(42));
    GDOX_TEST_CHECK(context.observe_calls == 1U);
    GDOX_TEST_CHECK(!gdox_source_media_present(&patched));
    GDOX_TEST_CHECK(context.observe_calls == 2U);

    GDOX_TEST_CHECK(!gdox_source_close(&patched, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_source_is_valid(&patched));
    GDOX_TEST_CHECK(context.close_calls == 0U);
    GDOX_TEST_CHECK(gdox_source_read(
        &patched,
        UINT64_C(0),
        UINT32_C(1),
        output,
        sizeof(output),
        &error
    ));
    GDOX_TEST_CHECK(output[0] == 0x5aU);

    GDOX_TEST_CHECK(gdox_source_close(&patched, &error));
    GDOX_TEST_CHECK(!gdox_source_is_valid(&patched));
    GDOX_TEST_CHECK(context.prepare_calls >= 2U);
    GDOX_TEST_CHECK(context.close_calls == 1U);
}

static void test_adapters_reject_occupied_output(void)
{
    retry_source_context input_context = {0};
    retry_source_context output_context = {0};
    gdox_sector_source input = {&input_context, &retry_ops};
    gdox_sector_source output = {&output_context, &retry_ops};
    const gdox_byte_patch patch = {0U, 0x5aU};
    gdox_error error;

    GDOX_TEST_CHECK(!gdox_source_make_partition(
        &input, UINT64_C(1), &output, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_source_is_valid(&input));
    GDOX_TEST_CHECK(gdox_source_is_valid(&output));
    GDOX_TEST_CHECK(!gdox_source_make_patched(
        &input, &patch, 1U, &output, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_source_is_valid(&input));
    GDOX_TEST_CHECK(gdox_source_is_valid(&output));
    GDOX_TEST_CHECK(gdox_source_close(&input, &error));
    GDOX_TEST_CHECK(gdox_source_close(&output, &error));
}

static void test_removable_session_classification(void)
{
    gdox_media_observation observation = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(8),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };

    GDOX_TEST_CHECK(gdox_removable_session_classify(
        &observation, true, UINT64_C(8)
    ) == GDOX_REMOVABLE_SESSION_EJECT_REQUESTED);
    GDOX_TEST_CHECK(gdox_removable_session_classify(
        &observation, true, UINT64_C(7)
    ) == GDOX_REMOVABLE_SESSION_CHANGED);
    observation.event = GDOX_MEDIA_EVENT_NONE;
    GDOX_TEST_CHECK(gdox_removable_session_classify(
        &observation, true, UINT64_C(8)
    ) == GDOX_REMOVABLE_SESSION_PRESENT);
    GDOX_TEST_CHECK(gdox_removable_session_classify(
        &observation, true, UINT64_C(7)
    ) == GDOX_REMOVABLE_SESSION_CHANGED);
    observation.event = GDOX_MEDIA_EVENT_NEW_MEDIA;
    GDOX_TEST_CHECK(gdox_removable_session_classify(
        &observation, false, 0U
    ) == GDOX_REMOVABLE_SESSION_CHANGED);
    observation.event = GDOX_MEDIA_EVENT_NONE;
    observation.readiness = GDOX_MEDIA_READINESS_ABSENT;
    GDOX_TEST_CHECK(gdox_removable_session_classify(
        &observation, true, UINT64_C(8)
    ) == GDOX_REMOVABLE_SESSION_UNAVAILABLE);
    GDOX_TEST_CHECK(gdox_removable_session_classify(
        NULL, false, 0U
    ) == GDOX_REMOVABLE_SESSION_UNAVAILABLE);
}

void gdox_test_source(void)
{
    gdox_media_observation observation = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(99),
    };
    gdox_error error;

    GDOX_TEST_CHECK(!gdox_source_observe_media(NULL, &observation));
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_UNKNOWN
    );
    GDOX_TEST_CHECK(observation.generation == 0U);

    GDOX_TEST_CHECK(
        gdox_source_validate_read(
            UINT64_C(100),
            UINT64_C(99),
            UINT32_C(1),
            GDOX_LOGICAL_SECTOR_BYTES,
            &error
        )
    );
    GDOX_TEST_CHECK(
        !gdox_source_validate_read(
            UINT64_C(100),
            UINT64_C(100),
            UINT32_C(1),
            GDOX_LOGICAL_SECTOR_BYTES,
            &error
        )
    );
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_OUT_OF_BOUNDS);
    GDOX_TEST_CHECK(
        !gdox_source_validate_read(
            UINT64_C(100),
            UINT64_C(0),
            UINT32_C(1),
            GDOX_LOGICAL_SECTOR_BYTES - 1U,
            &error
        )
    );
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_PROTOCOL);
    test_adapter_close_retry();
    test_adapters_reject_occupied_output();
    test_removable_session_classification();
}
