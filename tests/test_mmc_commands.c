#include "platform/mmc_commands.h"

#include "test.h"

#include <limits.h>
#include <string.h>

typedef struct fake_mmc {
    uint8_t response[96];
    size_t response_bytes;
    size_t reported_bytes;
    uint8_t cdb[16];
    size_t cdb_bytes;
    uint32_t timeout_ms;
    unsigned int command_count;
    bool fail_ready;
    uint8_t cached_sense[18];
    size_t cached_sense_bytes;
    bool fail_media_event;
    uint8_t media_events[20];
    size_t media_event_count;
    size_t media_event_index;
} fake_mmc;

static bool command_in(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    fake_mmc *fake = raw_context;
    const size_t copied = fake->response_bytes < output_bytes
        ? fake->response_bytes
        : output_bytes;

    (void)name;
    gdox_error_clear(error);
    memcpy(fake->cdb, cdb, cdb_bytes);
    fake->cdb_bytes = cdb_bytes;
    fake->timeout_ms = timeout_ms;
    ++fake->command_count;
    if (fake->fail_media_event && cdb_bytes != 0U
        && cdb[0] == 0x4aU) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "event poll failed");
        return false;
    }
    if (cdb_bytes != 0U && cdb[0] == 0x4aU
        && fake->media_event_index < fake->media_event_count) {
        const uint8_t event =
            fake->media_events[fake->media_event_index++];

        memset(output, 0, output_bytes);
        if (output_bytes >= 8U) {
            output[1] = 6U;
            output[2] = event == 0U ? 0x84U : 0x04U;
            output[4] = event;
            *transferred = 8U;
            return true;
        }
    }
    memset(output, 0, output_bytes);
    memcpy(output, fake->response, copied);
    *transferred = fake->reported_bytes == SIZE_MAX
        ? output_bytes
        : fake->reported_bytes;
    return true;
}

static bool command_out(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    const uint8_t *input,
    size_t input_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    (void)raw_context;
    (void)name;
    (void)cdb;
    (void)cdb_bytes;
    (void)input;
    (void)input_bytes;
    (void)timeout_ms;
    (void)transferred;
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected data-out command");
    return false;
}

static bool command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    fake_mmc *fake = raw_context;

    (void)name;
    gdox_error_clear(error);
    memcpy(fake->cdb, cdb, cdb_bytes);
    fake->cdb_bytes = cdb_bytes;
    fake->timeout_ms = timeout_ms;
    ++fake->command_count;
    if (fake->fail_ready && cdb_bytes != 0U && cdb[0] == 0U) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "not ready");
        return false;
    }
    return true;
}

static bool reset(void *context, gdox_error *error)
{
    (void)context;
    gdox_error_clear(error);
    return true;
}

static bool close_transport(void *context, gdox_error *error)
{
    (void)context;
    gdox_error_clear(error);
    return true;
}

static bool last_sense(
    const void *raw_context,
    uint8_t *output,
    size_t output_bytes,
    size_t *sense_bytes
)
{
    const fake_mmc *fake = raw_context;
    const size_t copied = fake->cached_sense_bytes < output_bytes
        ? fake->cached_sense_bytes
        : output_bytes;

    if (copied == 0U) {
        *sense_bytes = 0U;
        return false;
    }
    memcpy(output, fake->cached_sense, copied);
    *sense_bytes = copied;
    return true;
}

static const gdox_scsi_transport_ops fake_ops = {
    command_in,
    command_out,
    command_none,
    reset,
    close_transport,
    NULL,
    last_sense,
};

static void test_inquiry(void)
{
    fake_mmc fake = {0};
    gdox_scsi_transport transport = {&fake, &fake_ops};
    gdox_mmc_identity identity;
    gdox_error error;

    memset(fake.response, ' ', 36U);
    memcpy(fake.response + 8U, "HL-DT-ST", 8U);
    memcpy(fake.response + 16U, "DVDRAM GP65NB60", 16U);
    memcpy(fake.response + 32U, "PB00", 4U);
    fake.response_bytes = 36U;
    fake.reported_bytes = 36U;
    GDOX_TEST_CHECK(gdox_mmc_inquiry(
        &transport,
        UINT32_C(4321),
        &identity,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(identity.vendor, "HL-DT-ST") == 0);
    GDOX_TEST_CHECK(strcmp(identity.model, "DVDRAM GP65NB60") == 0);
    GDOX_TEST_CHECK(strcmp(identity.revision, "PB00") == 0);
    GDOX_TEST_CHECK(fake.cdb_bytes == 6U && fake.cdb[0] == 0x12U);
    GDOX_TEST_CHECK(fake.cdb[4] == 96U);
    GDOX_TEST_CHECK(fake.timeout_ms == UINT32_C(4321));

    fake.reported_bytes = 35U;
    GDOX_TEST_CHECK(!gdox_mmc_inquiry(
        &transport,
        UINT32_C(1),
        &identity,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_PROTOCOL);
}

static void test_capacity_and_structure(void)
{
    fake_mmc fake = {0};
    gdox_scsi_transport transport = {&fake, &fake_ops};
    gdox_error error;
    uint32_t last_lba;
    uint32_t block_size;
    uint8_t structure[32];
    size_t transferred;

    memcpy(
        fake.response,
        (const uint8_t[]){0x00U, 0x3aU, 0x4dU, 0x4fU,
                          0x00U, 0x00U, 0x08U, 0x00U},
        8U
    );
    fake.response_bytes = 8U;
    fake.reported_bytes = 8U;
    GDOX_TEST_CHECK(gdox_mmc_read_capacity_10(
        &transport,
        UINT32_C(10000),
        &last_lba,
        &block_size,
        &error
    ));
    GDOX_TEST_CHECK(last_lba == UINT32_C(0x003a4d4f));
    GDOX_TEST_CHECK(block_size == UINT32_C(2048));

    fake.response_bytes = 0U;
    fake.reported_bytes = SIZE_MAX;
    GDOX_TEST_CHECK(gdox_mmc_read_dvd_structure(
        &transport,
        0x04U,
        structure,
        sizeof(structure),
        UINT32_C(9876),
        &transferred,
        &error
    ));
    GDOX_TEST_CHECK(fake.cdb_bytes == 12U && fake.cdb[0] == 0xadU);
    GDOX_TEST_CHECK(fake.cdb[7] == 0x04U);
    GDOX_TEST_CHECK(fake.cdb[8] == 0U && fake.cdb[9] == 32U);
    GDOX_TEST_CHECK(fake.timeout_ms == UINT32_C(9876));
}

static void test_reads_and_control(void)
{
    fake_mmc fake = {0};
    gdox_scsi_transport transport = {&fake, &fake_ops};
    gdox_error error;
    uint8_t output[4096];
    uint8_t sense[18];
    size_t transferred;

    fake.reported_bytes = SIZE_MAX;
    GDOX_TEST_CHECK(gdox_mmc_read_10(
        &transport,
        UINT32_C(0x12345678),
        2U,
        32U,
        2048U,
        output,
        sizeof(output),
        UINT32_C(30000),
        &error
    ));
    GDOX_TEST_CHECK(fake.cdb_bytes == 10U && fake.cdb[0] == 0x28U);
    GDOX_TEST_CHECK(memcmp(
        fake.cdb + 2U,
        (const uint8_t[]){0x12U, 0x34U, 0x56U, 0x78U},
        4U
    ) == 0);
    GDOX_TEST_CHECK(fake.cdb[7] == 0U && fake.cdb[8] == 2U);

    GDOX_TEST_CHECK(gdox_mmc_read_12(
        &transport,
        UINT32_C(7),
        2U,
        128U,
        2048U,
        output,
        sizeof(output),
        UINT32_C(30000),
        &error
    ));
    GDOX_TEST_CHECK(fake.cdb_bytes == 12U && fake.cdb[0] == 0xa8U);
    GDOX_TEST_CHECK(fake.cdb[9] == 2U);

    GDOX_TEST_CHECK(!gdox_mmc_read_10(
        &transport,
        0U,
        33U,
        32U,
        2048U,
        output,
        sizeof(output),
        UINT32_C(1),
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    fake.reported_bytes = 18U;
    GDOX_TEST_CHECK(gdox_mmc_request_sense(
        &transport,
        UINT32_C(2000),
        sense,
        &transferred,
        &error
    ));
    GDOX_TEST_CHECK(fake.cdb[0] == 0x03U && fake.cdb[4] == 18U);
    GDOX_TEST_CHECK(gdox_mmc_test_unit_ready(
        &transport,
        UINT32_C(1500),
        &error
    ));
    GDOX_TEST_CHECK(fake.cdb_bytes == 6U && fake.cdb[0] == 0U);
    GDOX_TEST_CHECK(fake.timeout_ms == UINT32_C(1500));
}

static void test_media_observation(void)
{
    fake_mmc fake = {0};
    gdox_scsi_transport transport = {&fake, &fake_ops};
    gdox_mmc_media_tracker tracker = {0};
    gdox_mmc_media_tracker recovery_tracker = {0};
    gdox_mmc_media_tracker fallback_tracker = {0};
    gdox_media_observation observation;

    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_PRESENT
    );
    GDOX_TEST_CHECK(observation.generation == 0U);

    fake.fail_ready = true;
    fake.command_count = 0U;
    memcpy(
        fake.response,
        (const uint8_t[]){
            0x70U, 0U, 0x02U, 0U, 0U, 0U, 0U, 0U,
            0U, 0U, 0U, 0U, 0x3aU, 0U, 0U, 0U, 0U, 0U,
        },
        18U
    );
    fake.response_bytes = 18U;
    fake.reported_bytes = 18U;
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_ABSENT
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(1));
    GDOX_TEST_CHECK(fake.command_count == 3U);
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(1));

    fake.fail_ready = false;
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_PRESENT
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(1));

    fake.fail_ready = true;
    memset(fake.response, 0, sizeof(fake.response));
    fake.response[0] = 0x72U;
    fake.response[1] = 0x06U;
    fake.response[2] = 0x28U;
    fake.response_bytes = 4U;
    fake.reported_bytes = 4U;
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_UNKNOWN
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(2));
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(2));

    memset(fake.response, 0, sizeof(fake.response));
    fake.response[0] = 0x70U;
    fake.response[2] = 0x02U;
    fake.response[12] = 0x04U;
    fake.response[13] = 0x01U;
    fake.response_bytes = 18U;
    fake.reported_bytes = 18U;
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_UNKNOWN
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(2));

    fake.command_count = 0U;
    memcpy(
        fake.cached_sense,
        (const uint8_t[]){
            0x70U, 0U, 0x02U, 0U, 0U, 0U, 0U, 0U,
            0U, 0U, 0U, 0U, 0x3aU, 0U, 0U, 0U, 0U, 0U,
        },
        18U
    );
    fake.cached_sense_bytes = 18U;
    gdox_mmc_observe_media(
        &transport,
        UINT32_C(1000),
        &tracker,
        &observation
    );
    GDOX_TEST_CHECK(
        observation.readiness == GDOX_MEDIA_READINESS_ABSENT
    );
    GDOX_TEST_CHECK(observation.generation == UINT64_C(2));
    GDOX_TEST_CHECK(fake.command_count == 2U);

    memset(fake.cached_sense, 0, sizeof(fake.cached_sense));
    fake.cached_sense[0] = 0x72U;
    fake.cached_sense[1] = 0x06U;
    fake.cached_sense[2] = 0x28U;
    fake.cached_sense_bytes = 4U;
    GDOX_TEST_CHECK(gdox_mmc_media_tracker_capture_transport_sense(
        &transport,
        UINT32_C(1000),
        &recovery_tracker
    ));
    GDOX_TEST_CHECK(recovery_tracker.generation == UINT64_C(1));
    GDOX_TEST_CHECK(gdox_mmc_media_tracker_capture_transport_sense(
        &transport,
        UINT32_C(1000),
        &recovery_tracker
    ));
    GDOX_TEST_CHECK(recovery_tracker.generation == UINT64_C(1));

    fake.cached_sense_bytes = 0U;
    memcpy(
        fake.response,
        (const uint8_t[]){
            0x70U, 0U, 0x06U, 0U, 0U, 0U, 0U, 0U,
            0U, 0U, 0U, 0U, 0x28U, 0U, 0U, 0U, 0U, 0U,
        },
        18U
    );
    fake.response_bytes = 18U;
    fake.reported_bytes = 18U;
    fake.command_count = 0U;
    GDOX_TEST_CHECK(gdox_mmc_media_tracker_capture_transport_sense(
        &transport,
        UINT32_C(1000),
        &fallback_tracker
    ));
    GDOX_TEST_CHECK(fallback_tracker.generation == UINT64_C(1));
    GDOX_TEST_CHECK(fake.command_count == 1U);
}

static void test_media_events(void)
{
    fake_mmc fake = {0};
    gdox_scsi_transport transport = {&fake, &fake_ops};
    gdox_mmc_media_tracker tracker = {0};
    gdox_mmc_media_tracker change_tracker = {0};
    gdox_mmc_media_tracker failed_tracker = {0};
    gdox_media_event event = GDOX_MEDIA_EVENT_CHANGED;

    GDOX_TEST_CHECK(gdox_mmc_parse_media_event(
        (const uint8_t[]){0U, 6U, 0x04U, 0U, 0x01U, 0U, 0U, 0U},
        8U,
        &event
    ));
    GDOX_TEST_CHECK(event == GDOX_MEDIA_EVENT_EJECT_REQUEST);
    GDOX_TEST_CHECK(gdox_mmc_parse_media_event(
        (const uint8_t[]){0U, 6U, 0x84U, 0U, 0U, 0U, 0U, 0U},
        8U,
        &event
    ));
    GDOX_TEST_CHECK(event == GDOX_MEDIA_EVENT_NONE);
    GDOX_TEST_CHECK(!gdox_mmc_parse_media_event(
        (const uint8_t[]){0U, 6U, 0x02U, 0U, 0x01U, 0U, 0U, 0U},
        8U,
        &event
    ));
    GDOX_TEST_CHECK(!gdox_mmc_parse_media_event(
        (const uint8_t[]){0U, 6U, 0x04U, 0U, 0x01U, 0U, 0U},
        7U,
        &event
    ));

    memcpy(
        fake.response,
        (const uint8_t[]){0U, 6U, 0x04U, 0U, 0x01U, 0U, 0U, 0U},
        8U
    );
    fake.response_bytes = 8U;
    fake.reported_bytes = 8U;
    GDOX_TEST_CHECK(gdox_mmc_poll_media_event(
        &transport, UINT32_C(1500), &tracker
    ));
    GDOX_TEST_CHECK(fake.cdb_bytes == 10U && fake.cdb[0] == 0x4aU);
    GDOX_TEST_CHECK(fake.cdb[1] == 1U && fake.cdb[4] == 0x10U);
    GDOX_TEST_CHECK(fake.cdb[7] == 0U && fake.cdb[8] == 8U);
    GDOX_TEST_CHECK(tracker.pending_event
        == GDOX_MEDIA_EVENT_EJECT_REQUEST);
    GDOX_TEST_CHECK(gdox_mmc_media_tracker_transitioned(&tracker, 0U));

    fake.response[4] = 0x02U;
    GDOX_TEST_CHECK(gdox_mmc_poll_media_event(
        &transport, UINT32_C(1500), &tracker
    ));
    GDOX_TEST_CHECK(tracker.generation == UINT64_C(1));
    GDOX_TEST_CHECK(tracker.pending_event == GDOX_MEDIA_EVENT_NONE);

    GDOX_TEST_CHECK(gdox_mmc_poll_media_event(
        &transport, UINT32_C(1500), &change_tracker
    ));
    GDOX_TEST_CHECK(change_tracker.generation == UINT64_C(1));
    GDOX_TEST_CHECK(change_tracker.pending_event == GDOX_MEDIA_EVENT_NONE);
    GDOX_TEST_CHECK(gdox_mmc_media_tracker_transitioned(
        &change_tracker, 0U
    ));

    memcpy(
        fake.cached_sense,
        (const uint8_t[]){0x72U, 0x06U, 0x28U, 0U},
        4U
    );
    fake.cached_sense_bytes = 4U;
    fake.fail_media_event = true;
    GDOX_TEST_CHECK(!gdox_mmc_poll_media_event(
        &transport, UINT32_C(1500), &failed_tracker
    ));
    GDOX_TEST_CHECK(failed_tracker.generation == UINT64_C(1));
    fake.fail_media_event = false;
    fake.cached_sense_bytes = 0U;
    fake.response[4] = 0x01U;
    GDOX_TEST_CHECK(gdox_mmc_poll_media_event(
        &transport, UINT32_C(1500), &failed_tracker
    ));
    GDOX_TEST_CHECK(failed_tracker.pending_event
        == GDOX_MEDIA_EVENT_EJECT_REQUEST);

    tracker = (gdox_mmc_media_tracker){0};
    memcpy(
        fake.media_events,
        (const uint8_t[]){0x01U, 0x03U, 0x02U, 0U, 0x01U},
        5U
    );
    fake.media_event_count = 4U;
    fake.media_event_index = 0U;
    gdox_mmc_media_tracker_begin_session(
        &transport, UINT32_C(1500), &tracker
    );
    GDOX_TEST_CHECK(tracker.pending_event == GDOX_MEDIA_EVENT_NONE);
    GDOX_TEST_CHECK(fake.media_event_index == 4U);
    fake.media_event_count = 5U;
    GDOX_TEST_CHECK(gdox_mmc_poll_media_event(
        &transport, UINT32_C(1500), &tracker
    ));
    GDOX_TEST_CHECK(tracker.pending_event
        == GDOX_MEDIA_EVENT_EJECT_REQUEST);
}

int gdox_test_failures = 0;

int main(void)
{
    test_inquiry();
    test_capacity_and_structure();
    test_reads_and_control();
    test_media_observation();
    test_media_events();
    return gdox_test_failures == 0 ? 0 : 1;
}
