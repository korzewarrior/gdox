#include "test.h"

#include "platform/scsi_transport.h"

#include <string.h>

typedef struct test_scsi_context {
    bool command_out_called;
    uint8_t cdb[16];
    size_t cdb_bytes;
    const uint8_t *input;
    size_t input_bytes;
    uint32_t timeout_ms;
} test_scsi_context;

static bool command_in(
    void *context,
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
    (void)context;
    (void)name;
    (void)cdb;
    (void)cdb_bytes;
    (void)output;
    (void)output_bytes;
    (void)timeout_ms;
    (void)transferred;
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected data-in command");
    return false;
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
    test_scsi_context *context = raw_context;

    (void)name;
    gdox_error_clear(error);
    context->command_out_called = true;
    memcpy(context->cdb, cdb, cdb_bytes);
    context->cdb_bytes = cdb_bytes;
    context->input = input;
    context->input_bytes = input_bytes;
    context->timeout_ms = timeout_ms;
    *transferred = input_bytes;
    return true;
}

static bool command_none(
    void *context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    (void)context;
    (void)name;
    (void)cdb;
    (void)cdb_bytes;
    (void)timeout_ms;
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected no-data command");
    return false;
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

static const gdox_scsi_transport_ops test_ops = {
    command_in,
    command_out,
    command_none,
    reset,
    close_transport,
};

void gdox_test_scsi_transport(void)
{
    static const uint8_t cdb[10] = {
        0x3bU, 0x05U, 0x00U, 0x80U, 0x46U,
        0x8eU, 0x00U, 0x00U, 0x08U, 0x00U,
    };
    static const uint8_t input[8] = {
        0x00U, 0x3dU, 0x4dU, 0x4fU,
        0x00U, 0x3dU, 0x4dU, 0x4fU,
    };
    test_scsi_context context = {0};
    gdox_scsi_transport transport = {&context, &test_ops};
    gdox_error error;
    size_t transferred = 0U;

    GDOX_TEST_CHECK(gdox_scsi_command_out(
        &transport,
        "WRITE BUFFER",
        cdb,
        sizeof(cdb),
        input,
        sizeof(input),
        UINT32_C(5000),
        &transferred,
        &error
    ));
    GDOX_TEST_CHECK(!gdox_error_is_set(&error));
    GDOX_TEST_CHECK(context.command_out_called);
    GDOX_TEST_CHECK(context.cdb_bytes == sizeof(cdb));
    GDOX_TEST_CHECK(memcmp(context.cdb, cdb, sizeof(cdb)) == 0);
    GDOX_TEST_CHECK(context.input == input);
    GDOX_TEST_CHECK(context.input_bytes == sizeof(input));
    GDOX_TEST_CHECK(context.timeout_ms == UINT32_C(5000));
    GDOX_TEST_CHECK(transferred == sizeof(input));

    context.command_out_called = false;
    GDOX_TEST_CHECK(!gdox_scsi_command_out(
        &transport,
        "WRITE BUFFER",
        cdb,
        sizeof(cdb),
        NULL,
        sizeof(input),
        UINT32_C(5000),
        &transferred,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(!context.command_out_called);

    GDOX_TEST_CHECK(!gdox_scsi_command_out(
        &transport,
        "WRITE BUFFER",
        cdb,
        sizeof(cdb),
        input,
        0U,
        UINT32_C(5000),
        &transferred,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(!context.command_out_called);
}
