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

static const gdox_scsi_transport_ops fake_ops = {
    command_in,
    command_out,
    command_none,
    reset,
    close_transport,
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

int gdox_test_failures = 0;

int main(void)
{
    test_inquiry();
    test_capacity_and_structure();
    test_reads_and_control();
    return gdox_test_failures == 0 ? 0 : 1;
}
