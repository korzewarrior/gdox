#include "platform/mmc_commands.h"

#include <limits.h>
#include <string.h>

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static void put_be_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void copy_ascii_field(
    char *output,
    size_t output_bytes,
    const uint8_t *input,
    size_t input_bytes
)
{
    size_t begin = 0U;
    size_t end = input_bytes;
    size_t length;

    while (begin < end && (input[begin] == ' ' || input[begin] == 0U)) {
        ++begin;
    }
    while (end > begin
        && (input[end - 1U] == ' ' || input[end - 1U] == 0U)) {
        --end;
    }
    length = end - begin;
    if (length >= output_bytes) {
        length = output_bytes - 1U;
    }
    memcpy(output, input + begin, length);
    output[length] = '\0';
}

bool gdox_mmc_inquiry(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_identity *identity,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x12U, 0U, 0U, 0U, 96U, 0U};
    uint8_t response[96];
    size_t transferred;

    if (identity == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "MMC identity output is required"
        );
        return false;
    }
    if (!gdox_scsi_command_in(
            transport,
            "INQUIRY",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            timeout_ms,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred < 36U) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "INQUIRY returned fewer than 36 bytes"
        );
        return false;
    }
    copy_ascii_field(
        identity->vendor,
        sizeof(identity->vendor),
        response + 8U,
        8U
    );
    copy_ascii_field(
        identity->model,
        sizeof(identity->model),
        response + 16U,
        16U
    );
    copy_ascii_field(
        identity->revision,
        sizeof(identity->revision),
        response + 32U,
        4U
    );
    return true;
}

bool gdox_mmc_test_unit_ready(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0};
    return gdox_scsi_command_none(
        transport,
        "TEST UNIT READY",
        cdb,
        sizeof(cdb),
        timeout_ms,
        error
    );
}

bool gdox_mmc_request_sense(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    uint8_t output[18],
    size_t *transferred,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x03U, 0U, 0U, 0U, 18U, 0U};

    if (output == NULL || transferred == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "REQUEST SENSE outputs are required"
        );
        return false;
    }
    return gdox_scsi_command_in(
        transport,
        "REQUEST SENSE",
        cdb,
        sizeof(cdb),
        output,
        18U,
        timeout_ms,
        transferred,
        error
    );
}

bool gdox_mmc_read_capacity_10(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    uint32_t *last_lba,
    uint32_t *block_size,
    gdox_error *error
)
{
    static const uint8_t cdb[10] = {0x25U};
    uint8_t response[8];
    size_t transferred;

    if (last_lba == NULL || block_size == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "READ CAPACITY outputs are required"
        );
        return false;
    }
    if (!gdox_scsi_command_in(
            transport,
            "READ CAPACITY(10)",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            timeout_ms,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != sizeof(response)) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "READ CAPACITY(10) returned a short response"
        );
        return false;
    }
    *last_lba = read_be_u32(response);
    *block_size = read_be_u32(response + 4U);
    return true;
}

bool gdox_mmc_read_dvd_structure(
    gdox_scsi_transport *transport,
    uint8_t format,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};

    if (output == NULL || transferred == NULL
        || output_bytes > UINT16_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "invalid READ DVD STRUCTURE output"
        );
        return false;
    }
    cdb[0] = 0xadU;
    cdb[7] = format;
    put_be_u16(cdb + 8U, (uint16_t)output_bytes);
    return gdox_scsi_command_in(
        transport,
        "READ DVD STRUCTURE",
        cdb,
        sizeof(cdb),
        output,
        output_bytes,
        timeout_ms,
        transferred,
        error
    );
}

static bool read_blocks(
    gdox_scsi_transport *transport,
    uint8_t opcode,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};
    size_t transferred;
    const uint64_t expected = (uint64_t)blocks * block_bytes;

    if (output == NULL || blocks == 0U || blocks > maximum_blocks
        || block_bytes == 0U || expected != output_bytes
        || (opcode == 0x28U && blocks > UINT16_MAX)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "invalid bounded MMC read request"
        );
        return false;
    }
    cdb[0] = opcode;
    put_be_u32(cdb + 2U, lba);
    if (opcode == 0x28U) {
        put_be_u16(cdb + 7U, (uint16_t)blocks);
    } else {
        put_be_u32(cdb + 6U, blocks);
    }
    if (!gdox_scsi_command_in(
            transport,
            opcode == 0x28U ? "READ(10)" : "READ(12)",
            cdb,
            opcode == 0x28U ? 10U : sizeof(cdb),
            output,
            output_bytes,
            timeout_ms,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            opcode == 0x28U
                ? "READ(10) returned a short transfer"
                : "READ(12) returned a short transfer"
        );
        return false;
    }
    return true;
}

bool gdox_mmc_read_10(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    return read_blocks(
        transport,
        0x28U,
        lba,
        blocks,
        maximum_blocks,
        block_bytes,
        output,
        output_bytes,
        timeout_ms,
        error
    );
}

bool gdox_mmc_read_12(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    return read_blocks(
        transport,
        0xa8U,
        lba,
        blocks,
        maximum_blocks,
        block_bytes,
        output,
        output_bytes,
        timeout_ms,
        error
    );
}
