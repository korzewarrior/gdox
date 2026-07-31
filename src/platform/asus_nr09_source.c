#define _POSIX_C_SOURCE 200809L

#include "gdox/optical.h"

#include "platform/asus_nr09_source.h"
#include "platform/portable_sync.h"
#include "platform/scsi_transport.h"
#include "platform/usb_bot.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_ASUS_DEFAULT_TIMEOUT_MS UINT32_C(5000)
#define GDOX_ASUS_PRESENCE_TIMEOUT_MS UINT32_C(1000)
#define GDOX_ASUS_READ_TIMEOUT_MS UINT32_C(30000)
#define GDOX_ASUS_MAX_READ_BLOCKS UINT32_C(32)
#define GDOX_ASUS_READY_ATTEMPTS UINT32_C(20)
#define GDOX_ASUS_INQUIRY_ATTEMPTS UINT32_C(2)
#define GDOX_ASUS_RECOVERY_BUDGET_MS UINT32_C(20000)

typedef enum gdox_asus_field_id {
    GDOX_ASUS_FIELD_DRAM_PFI_END = 0,
    GDOX_ASUS_FIELD_SERVO_PFI,
    GDOX_ASUS_FIELD_ZONE_END,
    GDOX_ASUS_FIELD_ZONE_COMPLEMENT,
    GDOX_ASUS_FIELD_LAYER_END,
    GDOX_ASUS_FIELD_LAYER_END_MIRROR,
    GDOX_ASUS_FIELD_READ_BOUNDARY,
    GDOX_ASUS_FIELD_CAPACITY_PFI,
    GDOX_ASUS_FIELD_COUNT,
} gdox_asus_field_id;

typedef struct gdox_asus_field {
    uint32_t address;
    uint8_t subcommand;
    uint8_t stock[4];
    uint8_t live[4];
} gdox_asus_field;

typedef struct gdox_asus_media_profile {
    uint32_t descriptor_lba;
    uint32_t stock_last_lba;
    uint32_t live_last_lba;
    uint64_t live_sectors;
    uint8_t stock_pfi_prefix[3];
    uint8_t fixed_start_psn[4];
    uint8_t fixed_complemented_start[4];
    gdox_asus_field fields[GDOX_ASUS_FIELD_COUNT];
} gdox_asus_media_profile;

static const gdox_asus_media_profile xgd1_profile = {
    UINT32_C(0x30620),
    UINT32_C(0x1b4f),
    UINT32_C(0x3a4d4f),
    GDOX_XGD1_TOTAL_SECTORS,
    {0x03U, 0x1aU, 0xafU},
    {0x00U, 0x00U, 0x03U, 0x00U},
    {0x10U, 0x1aU, 0x03U, 0x00U},
    {
    {
        UINT32_C(0x888da044),
        0x02U,
        {0xafU, 0x1aU, 0x03U, 0x00U},
        {0xafU, 0x33U, 0x20U, 0x00U},
    },
    {
        UINT32_C(0x1750),
        0x01U,
        {0xafU, 0x1aU, 0x03U, 0x00U},
        {0xafU, 0x33U, 0x20U, 0x00U},
    },
    {
        UINT32_C(0x18f4),
        0x01U,
        {0xb0U, 0x1aU, 0x03U, 0x00U},
        {0xb0U, 0x33U, 0x20U, 0x00U},
    },
    {
        UINT32_C(0x18f8),
        0x01U,
        {0x50U, 0xe5U, 0xfcU, 0x00U},
        {0x50U, 0xccU, 0xdfU, 0x00U},
    },
    {
        UINT32_C(0x1900),
        0x01U,
        {0xafU, 0x1aU, 0x03U, 0x00U},
        {0xafU, 0x33U, 0x20U, 0x00U},
    },
    {
        UINT32_C(0x1908),
        0x01U,
        {0xafU, 0x1aU, 0x03U, 0x00U},
        {0xafU, 0x33U, 0x20U, 0x00U},
    },
    {
        UINT32_C(0x19cc),
        0x01U,
        {0x50U, 0x1bU, 0x03U, 0x00U},
        {0x50U, 0x4dU, 0x3dU, 0x00U},
    },
    {
        UINT32_C(0x1600),
        0x01U,
        {0xafU, 0x1aU, 0x03U, 0x00U},
        {0xafU, 0x33U, 0x20U, 0x00U},
    },
    },
};

static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct gdox_asus_state {
    uint8_t values[GDOX_ASUS_FIELD_COUNT][4];
    uint8_t start_psn[4];
    uint8_t complemented_start[4];
    uint32_t last_lba;
    uint32_t block_size;
} gdox_asus_state;

typedef struct gdox_asus_identity {
    char vendor[9];
    char model[17];
    char revision[5];
} gdox_asus_identity;

typedef struct gdox_asus_context {
    gdox_scsi_transport transport;
    gdox_mutex mutex;
    gdox_disc_evidence evidence;
    gdox_asus_state stock;
    const gdox_asus_media_profile *profile;
    atomic_uint_fast64_t read_commands;
    atomic_uint_fast64_t read_sectors;
    atomic_uint_fast64_t read_bytes;
    atomic_uint_fast64_t last_read_lba;
    atomic_bool abort;
    uint8_t retries;
    bool active;
} gdox_asus_context;

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

static bool inquiry(
    gdox_scsi_transport *transport,
    gdox_asus_identity *identity,
    gdox_error *error
)
{
    uint8_t cdb[6] = {0x12U, 0U, 0U, 0U, 96U, 0U};
    uint8_t response[96];
    size_t transferred;

    if (!gdox_scsi_command_in(
            transport,
            "INQUIRY",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            GDOX_ASUS_DEFAULT_TIMEOUT_MS,
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

static bool identity_is_validated(const gdox_asus_identity *identity)
{
    return strcmp(identity->vendor, GDOX_ASUS_SCSI_VENDOR) == 0
        && strcmp(identity->model, GDOX_ASUS_SCSI_MODEL) == 0
        && strcmp(identity->revision, GDOX_ASUS_SCSI_REVISION) == 0;
}

static bool test_unit_ready_with_timeout(
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

static bool test_unit_ready(gdox_scsi_transport *transport, gdox_error *error)
{
    return test_unit_ready_with_timeout(
        transport,
        GDOX_ASUS_DEFAULT_TIMEOUT_MS,
        error
    );
}

static bool request_sense(
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x03U, 0U, 0U, 0U, 18U, 0U};
    uint8_t response[18];
    size_t transferred;

    return gdox_scsi_command_in(
        transport,
        "REQUEST SENSE",
        cdb,
        sizeof(cdb),
        response,
        sizeof(response),
        GDOX_ASUS_DEFAULT_TIMEOUT_MS,
        &transferred,
        error
    );
}

static bool read_capacity(
    gdox_scsi_transport *transport,
    uint32_t *last_lba,
    uint32_t *block_size,
    gdox_error *error
)
{
    static const uint8_t cdb[10] = {0x25U};
    uint8_t response[8];
    size_t transferred;

    if (!gdox_scsi_command_in(
            transport,
            "READ CAPACITY(10)",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            UINT32_C(10000),
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

static bool read_dvd_structure(
    gdox_scsi_transport *transport,
    uint8_t format,
    uint8_t *output,
    size_t output_bytes,
    size_t *transferred,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};

    if (output_bytes > UINT16_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "DVD structure buffer is too large"
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
        UINT32_C(10000),
        transferred,
        error
    );
}

static bool memory_read(
    gdox_scsi_transport *transport,
    const gdox_asus_field *field,
    uint8_t output[4],
    gdox_error *error
)
{
    uint8_t cdb[12] = {0xf1U};
    size_t transferred;

    put_be_u32(cdb + 2U, field->address);
    put_be_u16(cdb + 7U, 4U);
    cdb[9] = field->subcommand;
    if (!gdox_scsi_command_in(
            transport,
            "ASUS NR09 volatile memory read",
            cdb,
            sizeof(cdb),
            output,
            4U,
            GDOX_ASUS_DEFAULT_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != 4U) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "ASUS NR09 volatile-memory read returned a short transfer"
        );
        return false;
    }
    return true;
}

static bool memory_write(
    gdox_scsi_transport *transport,
    const gdox_asus_field *field,
    const uint8_t input[4],
    gdox_error *error
)
{
    uint8_t cdb[12] = {0xf1U, 0x01U};
    size_t transferred;

    put_be_u32(cdb + 2U, field->address);
    put_be_u16(cdb + 7U, 4U);
    cdb[9] = field->subcommand;
    if (!gdox_scsi_command_out(
            transport,
            "ASUS NR09 volatile memory write",
            cdb,
            sizeof(cdb),
            input,
            4U,
            GDOX_ASUS_DEFAULT_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != 4U) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "ASUS NR09 volatile-memory write returned a short transfer"
        );
        return false;
    }
    return true;
}

static bool read_fixed_memory(
    gdox_scsi_transport *transport,
    uint32_t address,
    uint8_t output[4],
    gdox_error *error
)
{
    const gdox_asus_field field = {
        address,
        0x01U,
        {0},
        {0},
    };
    return memory_read(transport, &field, output, error);
}

static bool read10(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    uint8_t cdb[10] = {0x28U};
    size_t transferred;
    const uint64_t expected =
        (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;

    if (blocks == 0U || blocks > GDOX_ASUS_MAX_READ_BLOCKS
        || expected != output_bytes) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "invalid bounded ASUS READ(10) request"
        );
        return false;
    }
    put_be_u32(cdb + 2U, lba);
    put_be_u16(cdb + 7U, (uint16_t)blocks);
    if (!gdox_scsi_command_in(
            transport,
            "READ(10)",
            cdb,
            sizeof(cdb),
            output,
            output_bytes,
            GDOX_ASUS_READ_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "READ(10) returned a short transfer"
        );
        return false;
    }
    return true;
}

static bool ladder_aborted(const atomic_bool *abort, gdox_error *error)
{
    if (abort != NULL
        && atomic_load_explicit(abort, memory_order_acquire)) {
        gdox_error_set(
            error,
            GDOX_ERROR_CANCELLED,
            "optical session was cancelled"
        );
        return true;
    }
    return false;
}

static bool read_state(
    gdox_asus_context *context,
    const atomic_bool *abort,
    gdox_asus_state *state,
    gdox_error *error
)
{
    size_t index;

    if (ladder_aborted(abort, error)) {
        return false;
    }
    for (index = 0U; index < GDOX_ASUS_FIELD_COUNT; ++index) {
        if (!memory_read(
                &context->transport,
                &context->profile->fields[index],
                state->values[index],
                error
            )) {
            return false;
        }
    }
    return read_fixed_memory(
            &context->transport,
            UINT32_C(0x18fc),
            state->start_psn,
            error
        )
        && read_fixed_memory(
            &context->transport,
            UINT32_C(0x1904),
            state->complemented_start,
            error
        )
        && read_capacity(
            &context->transport,
            &state->last_lba,
            &state->block_size,
            error
        );
}

static bool state_matches(
    const gdox_asus_media_profile *profile,
    const gdox_asus_state *state,
    bool live
)
{
    const uint32_t expected_lba =
        live ? profile->live_last_lba : profile->stock_last_lba;
    size_t index;

    for (index = 0U; index < GDOX_ASUS_FIELD_COUNT; ++index) {
        const uint8_t *expected =
            live
                ? profile->fields[index].live
                : profile->fields[index].stock;
        if (memcmp(
                state->values[index],
                expected,
                sizeof(state->values[index])
            ) != 0) {
            return false;
        }
    }
    return memcmp(
            state->start_psn,
            profile->fixed_start_psn,
            sizeof(state->start_psn)
        ) == 0
        && memcmp(
            state->complemented_start,
            profile->fixed_complemented_start,
            sizeof(state->complemented_start)
        ) == 0
        && state->last_lba == expected_lba
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

static bool state_is_known_partial(
    const gdox_asus_media_profile *profile,
    const gdox_asus_state *state
)
{
    size_t index;

    for (index = 0U; index < GDOX_ASUS_FIELD_COUNT; ++index) {
        if (memcmp(
                state->values[index],
                profile->fields[index].stock,
                sizeof(state->values[index])
            ) != 0
            && memcmp(
                state->values[index],
                profile->fields[index].live,
                sizeof(state->values[index])
            ) != 0) {
            return false;
        }
    }
    return memcmp(
            state->start_psn,
            profile->fixed_start_psn,
            sizeof(state->start_psn)
        ) == 0
        && memcmp(
            state->complemented_start,
            profile->fixed_complemented_start,
            sizeof(state->complemented_start)
        ) == 0
        && (state->last_lba == profile->stock_last_lba
            || state->last_lba == profile->live_last_lba)
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

static bool apply_live(gdox_asus_context *context, gdox_error *error)
{
    gdox_asus_state observed;
    size_t index;

    for (index = 0U; index < GDOX_ASUS_FIELD_COUNT; ++index) {
        if (!memory_write(
                &context->transport,
                &context->profile->fields[index],
                context->profile->fields[index].live,
                error
            )) {
            return false;
        }
    }
    if (!read_state(context, &context->abort, &observed, error)) {
        return false;
    }
    if (!state_matches(context->profile, &observed, true)) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "live ASUS NR09 volatile state did not verify"
        );
        return false;
    }
    return true;
}

static bool restore_stock(gdox_asus_context *context, gdox_error *error)
{
    static const gdox_asus_field_id order[] = {
        GDOX_ASUS_FIELD_CAPACITY_PFI,
        GDOX_ASUS_FIELD_DRAM_PFI_END,
        GDOX_ASUS_FIELD_SERVO_PFI,
        GDOX_ASUS_FIELD_ZONE_END,
        GDOX_ASUS_FIELD_ZONE_COMPLEMENT,
        GDOX_ASUS_FIELD_LAYER_END,
        GDOX_ASUS_FIELD_LAYER_END_MIRROR,
        GDOX_ASUS_FIELD_READ_BOUNDARY,
    };
    gdox_error first_error;
    gdox_error current;
    gdox_asus_state observed;
    bool command_failed = false;
    size_t index;

    gdox_error_clear(&first_error);
    for (index = 0U; index < sizeof(order) / sizeof(order[0]); ++index) {
        const gdox_asus_field_id field = order[index];
        if (!memory_write(
                &context->transport,
                &context->profile->fields[field],
                context->stock.values[field],
                &current
            ) && !command_failed) {
            first_error = current;
            command_failed = true;
        }
    }
    if (read_state(context, NULL, &observed, &current)
        && memcmp(&observed, &context->stock, sizeof(observed)) == 0) {
        return true;
    }
    if (command_failed) {
        *error = first_error;
    } else if (gdox_error_is_set(&current)) {
        *error = current;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "stock ASUS NR09 volatile state did not verify"
        );
    }
    return false;
}

static bool restore_stock_after_streaming(
    gdox_asus_context *context,
    gdox_error *error
)
{
    gdox_error last;
    uint32_t attempt;

    if (restore_stock(context, &last)) {
        return true;
    }
    for (attempt = 0U; attempt < 2U; ++attempt) {
        gdox_error ignored;

        (void)gdox_scsi_transport_reset(&context->transport, &ignored);
        gdox_sleep_ms(UINT32_C(100) * (attempt + 1U));
        if (restore_stock(context, &last)) {
            return true;
        }
    }
    *error = last;
    return false;
}

static bool ensure_live(gdox_asus_context *context, gdox_error *error)
{
    gdox_asus_state observed;

    if (!read_state(context, &context->abort, &observed, error)) {
        return false;
    }
    if (state_matches(context->profile, &observed, true)) {
        return true;
    }
    if (!state_is_known_partial(context->profile, &observed)) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "refusing ASUS recovery because volatile memory has an unknown state"
        );
        return false;
    }
    if (!state_matches(context->profile, &observed, false)
        && !restore_stock(context, error)) {
        return false;
    }
    return apply_live(context, error);
}

static bool recover_optical(
    gdox_asus_context *context,
    gdox_error *error
)
{
    uint32_t attempt;
    gdox_error last;

    gdox_error_clear(&last);
    if (ladder_aborted(&context->abort, error)) {
        return false;
    }
    (void)gdox_scsi_transport_reset(&context->transport, &last);
    (void)request_sense(&context->transport, &last);
    for (attempt = 0U; attempt < GDOX_ASUS_READY_ATTEMPTS; ++attempt) {
        if (ladder_aborted(&context->abort, error)) {
            return false;
        }
        if (test_unit_ready(&context->transport, &last)) {
            return ensure_live(context, error);
        }
        if (last.code == GDOX_ERROR_NOT_FOUND) {
            break;
        }
        gdox_sleep_ms(UINT32_C(250));
    }
    *error = last;
    return false;
}

static bool read_range_with_recovery(
    gdox_asus_context *context,
    uint64_t recovery_deadline_ms,
    uint32_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    uint32_t attempt;
    gdox_error last;

    gdox_error_clear(&last);
    for (attempt = 0U; attempt <= context->retries; ++attempt) {
        if (ladder_aborted(&context->abort, error)) {
            return false;
        }
        if (read10(
                &context->transport,
                lba,
                blocks,
                output,
                output_bytes,
                &last
            )) {
            (void)atomic_fetch_add_explicit(
                &context->read_commands,
                UINT64_C(1),
                memory_order_relaxed
            );
            (void)atomic_fetch_add_explicit(
                &context->read_sectors,
                (uint64_t)blocks,
                memory_order_relaxed
            );
            (void)atomic_fetch_add_explicit(
                &context->read_bytes,
                (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &context->last_read_lba,
                (uint64_t)lba + blocks - 1U,
                memory_order_relaxed
            );
            return true;
        }
        if (last.code == GDOX_ERROR_NOT_FOUND
            || last.code == GDOX_ERROR_CANCELLED
            || gdox_monotonic_ms() >= recovery_deadline_ms) {
            break;
        }
        if (attempt < context->retries
            && !recover_optical(context, &last)
            && (last.code == GDOX_ERROR_NOT_FOUND
                || last.code == GDOX_ERROR_CANCELLED)) {
            break;
        }
    }
    *error = last;
    return false;
}

static void add_sector_context(gdox_error *error, uint32_t lba)
{
    const gdox_error cause = *error;
    char message[GDOX_ERROR_MESSAGE_CAPACITY];

    (void)snprintf(
        message,
        sizeof(message),
        "could not read Xbox disc sector %u: %.280s",
        lba,
        cause.message
    );
    gdox_error_set(error, cause.code, message);
}

static bool read_chunk(
    gdox_asus_context *context,
    uint64_t recovery_deadline_ms,
    uint32_t lba,
    uint32_t blocks,
    uint8_t *output,
    gdox_error *error
)
{
    const size_t bytes = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
    uint32_t index;

    if (read_range_with_recovery(
            context,
            recovery_deadline_ms,
            lba,
            blocks,
            output,
            bytes,
            error
        )) {
        return true;
    }
    if (blocks == 1U
        || error->code == GDOX_ERROR_NOT_FOUND
        || error->code == GDOX_ERROR_CANCELLED) {
        add_sector_context(error, lba);
        return false;
    }
    for (index = 0U; index < blocks; ++index) {
        if (!read_range_with_recovery(
                context,
                recovery_deadline_ms,
                lba + index,
                1U,
                output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES,
                GDOX_LOGICAL_SECTOR_BYTES,
                error
            )) {
            add_sector_context(error, lba + index);
            return false;
        }
    }
    return true;
}

static uint64_t asus_sector_count(const void *raw_context)
{
    const gdox_asus_context *context = raw_context;
    return context->profile->live_sectors;
}

static bool asus_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_asus_context *context = raw_context;
    uint32_t remaining = blocks;
    uint32_t current_lba = (uint32_t)lba;
    size_t output_offset = 0U;
    uint64_t recovery_deadline_ms;
    bool success = true;

    (void)output_bytes;
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not lock ASUS optical transport"
        );
        return false;
    }
    recovery_deadline_ms =
        gdox_monotonic_ms() + GDOX_ASUS_RECOVERY_BUDGET_MS;
    while (remaining != 0U) {
        const uint32_t chunk =
            remaining < GDOX_ASUS_MAX_READ_BLOCKS
                ? remaining
                : GDOX_ASUS_MAX_READ_BLOCKS;
        if (!read_chunk(
                context,
                recovery_deadline_ms,
                current_lba,
                chunk,
                output + output_offset,
                error
            )) {
            success = false;
            break;
        }
        current_lba += chunk;
        remaining -= chunk;
        output_offset +=
            (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES;
    }
    gdox_mutex_unlock(&context->mutex);
    return success;
}

static bool asus_media_present(const void *raw_context)
{
    gdox_asus_context *context = (gdox_asus_context *)raw_context;
    gdox_error error;
    bool present;

    if (!gdox_mutex_lock(&context->mutex)) {
        return false;
    }
    present = test_unit_ready_with_timeout(
        &context->transport,
        GDOX_ASUS_PRESENCE_TIMEOUT_MS,
        &error
    );
    gdox_mutex_unlock(&context->mutex);
    return present;
}

static bool asus_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const gdox_asus_context *context = raw_context;

    if (output == NULL) {
        return false;
    }
    *output = context->evidence;
    return context->evidence.pfi_present
        || context->evidence.dmi_present
        || context->evidence.security_sector_present;
}

static bool asus_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const gdox_asus_context *context = raw_context;

    if (output == NULL) {
        return false;
    }
    output->commands = atomic_load_explicit(
        &context->read_commands,
        memory_order_relaxed
    );
    output->sectors = atomic_load_explicit(
        &context->read_sectors,
        memory_order_relaxed
    );
    output->bytes = atomic_load_explicit(
        &context->read_bytes,
        memory_order_relaxed
    );
    output->last_lba = atomic_load_explicit(
        &context->last_read_lba,
        memory_order_relaxed
    );
    return true;
}

static bool asus_close(void *raw_context, gdox_error *error)
{
    gdox_asus_context *context = raw_context;
    gdox_error restore_error;
    gdox_error close_error;
    bool restored = true;
    bool closed;

    gdox_error_clear(&restore_error);
    if (gdox_mutex_lock(&context->mutex)) {
        if (context->active) {
            restored = restore_stock_after_streaming(
                context,
                &restore_error
            );
            if (restored) {
                context->active = false;
            }
        }
        gdox_mutex_unlock(&context->mutex);
    } else {
        restored = false;
        gdox_error_set(
            &restore_error,
            GDOX_ERROR_INTERNAL,
            "could not lock ASUS transport during close"
        );
    }
    gdox_mutex_destroy(&context->mutex);
    closed = gdox_scsi_transport_close(
        &context->transport,
        &close_error
    );
    free(context);
    if (!restored) {
        *error = restore_error;
        return false;
    }
    if (!closed) {
        *error = close_error;
        return false;
    }
    return true;
}

static void asus_abort(void *raw_context)
{
    gdox_asus_context *context = raw_context;
    atomic_store_explicit(&context->abort, true, memory_order_release);
}

static const gdox_sector_source_ops asus_source_ops = {
    asus_sector_count,
    asus_read,
    asus_media_present,
    asus_close,
    asus_evidence,
    asus_physical_read_stats,
    asus_abort,
};

static bool open_discovered_asus(
    void *raw_context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)raw_context;
    return gdox_usb_bot_open(
        GDOX_USB_BOT_ASUS_NR09,
        transport,
        error
    );
}

static bool open_validated_transport(
    gdox_asus_nr09_transport_opener opener,
    void *opener_context,
    gdox_scsi_transport *transport,
    gdox_asus_identity *identity,
    gdox_error *error
)
{
    uint32_t attempt;

    for (attempt = 0U;
         attempt < GDOX_ASUS_INQUIRY_ATTEMPTS;
         ++attempt) {
        if (!opener(opener_context, transport, error)) {
            return false;
        }
        if (inquiry(transport, identity, error)) {
            break;
        }
        gdox_scsi_transport_destroy(transport);
        if (error->code != GDOX_ERROR_TRANSPORT
            || attempt + 1U == GDOX_ASUS_INQUIRY_ATTEMPTS) {
            return false;
        }
        gdox_sleep_ms(UINT32_C(100));
    }
    if (!identity_is_validated(identity)) {
        gdox_scsi_transport_destroy(transport);
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "USB device is not the validated ASUS SDRW-08D1S-U A202 mechanism"
        );
        return false;
    }
    return true;
}

static void destroy_failed_open(gdox_asus_context *context)
{
    gdox_scsi_transport_destroy(&context->transport);
    gdox_mutex_destroy(&context->mutex);
    free(context);
}

static bool source_open_profile(
    const gdox_asus_media_profile *profile,
    gdox_asus_nr09_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    gdox_asus_context *context;
    gdox_asus_identity identity;
    uint32_t attempts;
    uint32_t attempt;
    uint8_t pfi[2052];
    uint8_t dmi[2052];
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES];
    size_t transferred;
    gdox_error operation_error;

    gdox_error_clear(error);
    if (profile == NULL || opener == NULL || source == NULL
        || gdox_source_is_valid(source)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an ASUS transport opener and empty source output are required"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate ASUS source"
        );
        return false;
    }
    atomic_init(&context->read_commands, UINT64_C(0));
    atomic_init(&context->read_sectors, UINT64_C(0));
    atomic_init(&context->read_bytes, UINT64_C(0));
    atomic_init(&context->last_read_lba, UINT64_C(0));
    atomic_init(&context->abort, false);
    context->profile = profile;
    if (!gdox_mutex_init(&context->mutex)) {
        free(context);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not initialize ASUS source lock"
        );
        return false;
    }
    if (!open_validated_transport(
            opener,
            opener_context,
            &context->transport,
            &identity,
            error
        )) {
        gdox_mutex_destroy(&context->mutex);
        free(context);
        return false;
    }
    gdox_error_clear(&operation_error);
    attempts = ready_timeout_ms / UINT32_C(500) + 1U;
    for (attempt = 0U; attempt < attempts; ++attempt) {
        if (test_unit_ready(&context->transport, &operation_error)) {
            break;
        }
        if (attempt + 1U < attempts) {
            gdox_sleep_ms(UINT32_C(500));
        }
    }
    if (attempt == attempts) {
        *error = operation_error;
        destroy_failed_open(context);
        return false;
    }
    if (!read_dvd_structure(
            &context->transport,
            0U,
            pfi,
            sizeof(pfi),
            &transferred,
            error
        )
        || transferred != sizeof(pfi)
        || memcmp(
            pfi + 17U,
            profile->stock_pfi_prefix,
            sizeof(profile->stock_pfi_prefix)
        ) != 0) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "disc does not match the selected ASUS media profile"
            );
        }
        destroy_failed_open(context);
        return false;
    }
    context->evidence.pfi_present = true;
    memcpy(
        context->evidence.pfi,
        pfi + 4U,
        GDOX_DISC_STRUCTURE_BYTES
    );
    if (read_dvd_structure(
            &context->transport,
            0x04U,
            dmi,
            sizeof(dmi),
            &transferred,
            &operation_error
        )
        && transferred == sizeof(dmi)) {
        context->evidence.dmi_present = true;
        memcpy(
            context->evidence.dmi,
            dmi + 4U,
            GDOX_DISC_STRUCTURE_BYTES
        );
    } else {
        (void)snprintf(
            context->evidence.note,
            sizeof(context->evidence.note),
            "%s",
            "DMI evidence was unavailable; full-disc output cannot claim complete evidence."
        );
    }
    if (context->evidence.note[0] == '\0') {
        (void)snprintf(
            context->evidence.note,
            sizeof(context->evidence.note),
            "%s",
            "This drive does not expose decrypted security-sector evidence."
        );
    }
    if (!read_state(context, &context->abort, &context->stock, error)
        || !state_matches(profile, &context->stock, false)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "refusing ASUS session because stock volatile state does not match"
            );
        }
        destroy_failed_open(context);
        return false;
    }
    if (!apply_live(context, error)) {
        operation_error = *error;
        if (!restore_stock_after_streaming(context, error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "ASUS initialization failed and volatile-state restoration also failed; power-cycle the drive"
            );
        } else {
            *error = operation_error;
        }
        destroy_failed_open(context);
        return false;
    }
    context->active = true;
    if (!read10(
            &context->transport,
            profile->descriptor_lba,
            1U,
            descriptor,
            sizeof(descriptor),
            error
        )
        || memcmp(
            descriptor,
            xdvdfs_magic,
            sizeof(xdvdfs_magic)
        ) != 0
        || memcmp(
            descriptor + sizeof(descriptor) - sizeof(xdvdfs_magic),
            xdvdfs_magic,
            sizeof(xdvdfs_magic)
        ) != 0) {
        operation_error = *error;
        if (!gdox_error_is_set(&operation_error)) {
            gdox_error_set(
                &operation_error,
                GDOX_ERROR_NOT_FOUND,
                "live ASUS view did not contain a complete XDVDFS descriptor"
            );
        }
        if (!restore_stock_after_streaming(context, error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "ASUS disc validation failed and volatile-state restoration also failed; power-cycle the drive"
            );
        } else {
            context->active = false;
            *error = operation_error;
        }
        destroy_failed_open(context);
        return false;
    }
    context->retries = read_retries;
    source->context = context;
    source->ops = &asus_source_ops;
    return true;
}

bool gdox_asus_nr09_source_open(
    gdox_asus_nr09_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return source_open_profile(
        &xgd1_profile,
        opener,
        opener_context,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

bool gdox_optical_observe_asus_nr09(
    gdox_optical_presence *presence,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (presence == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    presence->drive = GDOX_OPTICAL_DRIVE_NONE;
    return gdox_usb_bot_observe(
        GDOX_USB_BOT_ASUS_NR09,
        &presence->drive_present,
        &presence->media_status_known,
        &presence->media_present,
        error
    );
}

bool gdox_optical_asus_nr09_connected(
    bool *connected,
    gdox_error *error
)
{
    return gdox_usb_bot_present(
        GDOX_USB_BOT_ASUS_NR09,
        connected,
        error
    );
}

bool gdox_optical_open_asus_nr09(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_asus_nr09_source_open(
        open_discovered_asus,
        NULL,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}
