#define _POSIX_C_SOURCE 200809L

#include "gdox/optical.h"

#include "platform/gp08_source.h"
#include "platform/portable_sync.h"
#include "platform/scsi_transport.h"
#include "platform/usb_bot.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_GP08_DEFAULT_TIMEOUT_MS UINT32_C(5000)
#define GDOX_GP08_PRESENCE_TIMEOUT_MS UINT32_C(1000)
#define GDOX_GP08_READ_TIMEOUT_MS UINT32_C(30000)
#define GDOX_GP08_MAX_READ_BLOCKS UINT32_C(32)
#define GDOX_GP08_READY_ATTEMPTS UINT32_C(20)
#define GDOX_GP08_INQUIRY_ATTEMPTS UINT32_C(2)
#define GDOX_GP08_DESCRIPTOR_LBA UINT32_C(0x30620)
#define GDOX_GP08_STOCK_LAST_LBA UINT32_C(0x1b4f)
#define GDOX_GP08_LIVE_HOST_LAST_LBA UINT32_C(0x3a4d4f)
#define GDOX_GP08_RECOVERY_BUDGET_MS UINT32_C(20000)

#define GDOX_GP08_CAPACITY_ADDRESS UINT32_C(0x80468e)
#define GDOX_GP08_ZONE0_ADDRESS UINT32_C(0x810600)
#define GDOX_GP08_ZONE0_LENGTH_ADDRESS UINT32_C(0x81060c)
#define GDOX_GP08_ZONE1_ADDRESS UINT32_C(0x810644)
#define GDOX_GP08_ZONE2_ADDRESS UINT32_C(0x810684)
#define GDOX_GP08_END_CACHE_ADDRESS UINT32_C(0x80aab0)
#define GDOX_GP08_PFI_END_ADDRESS UINT32_C(0x803b17)

static const uint8_t stock_capacity[8] = {
    0x00U, 0x00U, 0x1bU, 0x4fU, 0x00U, 0x00U, 0x1bU, 0x4fU,
};
static const uint8_t live_capacity[8] = {
    0x00U, 0x3dU, 0x4dU, 0x4fU, 0x00U, 0x3dU, 0x4dU, 0x4fU,
};
static const uint8_t stock_zone0[16] = {
    0x00U, 0xffU, 0xffU, 0xffU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1aU, 0xb0U,
};
static const uint8_t live_zone0_length[4] = {
    0x00U, 0x1dU, 0x33U, 0xb0U,
};
static const uint8_t stock_zone1[12] = {
    0x00U, 0x00U, 0x1aU, 0xb0U, 0x01U, 0xfcU,
    0xe5U, 0x50U, 0x00U, 0x00U, 0x00U, 0xa0U,
};
static const uint8_t live_zone1[12] = {
    0x00U, 0x1dU, 0x33U, 0xb0U, 0x01U, 0xdfU,
    0xccU, 0x50U, 0x00U, 0x20U, 0x19U, 0xa0U,
};
static const uint8_t stock_zone2[12] = {
    0x00U, 0x00U, 0x1bU, 0x50U, 0x01U, 0xfcU,
    0xe5U, 0xf0U, 0x00U, 0x00U, 0x00U, 0x00U,
};
static const uint8_t live_zone2[12] = {
    0x00U, 0x3dU, 0x4dU, 0x50U, 0x01U, 0xffU,
    0xe5U, 0xf0U, 0x00U, 0x00U, 0x00U, 0x00U,
};
static const uint8_t stock_end_cache[4] = {
    0x00U, 0x03U, 0x1aU, 0xb0U,
};
static const uint8_t live_end_cache[4] = {
    0x00U, 0x20U, 0x33U, 0xb0U,
};
static const uint8_t stock_pfi_prefix[3] = {
    0x03U, 0x1aU, 0xafU,
};
static const uint8_t live_pfi_prefix[3] = {
    0x20U, 0x33U, 0xafU,
};
static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct gdox_gp08_state {
    uint8_t capacity[8];
    uint8_t zone0[16];
    uint8_t zone1[12];
    uint8_t zone2[12];
    uint8_t end_cache[4];
    uint8_t pfi_end[4];
    uint32_t last_lba;
    uint32_t block_size;
} gdox_gp08_state;

typedef struct gdox_gp08_identity {
    char vendor[9];
    char model[17];
    char revision[5];
} gdox_gp08_identity;

typedef struct gdox_gp08_context {
    gdox_scsi_transport transport;
    gdox_mutex mutex;
    gdox_disc_evidence evidence;
    gdox_gp08_state stock;
    atomic_uint_fast64_t read_commands;
    atomic_uint_fast64_t read_sectors;
    atomic_uint_fast64_t read_bytes;
    atomic_uint_fast64_t last_read_lba;
    atomic_bool abort;
    uint8_t retries;
    bool active;
} gdox_gp08_context;

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
    output[1] = (uint8_t)(value & 0xffU);
}

static void put_be_u24(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)((value >> 16U) & 0xffU);
    output[1] = (uint8_t)((value >> 8U) & 0xffU);
    output[2] = (uint8_t)(value & 0xffU);
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)((value >> 16U) & 0xffU);
    output[2] = (uint8_t)((value >> 8U) & 0xffU);
    output[3] = (uint8_t)(value & 0xffU);
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
    while (end > begin && (input[end - 1U] == ' ' || input[end - 1U] == 0U)) {
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
    gdox_gp08_identity *identity,
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
            GDOX_GP08_DEFAULT_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred < 36U) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "INQUIRY returned fewer than 36 bytes");
        return false;
    }
    copy_ascii_field(identity->vendor, sizeof(identity->vendor), response + 8U, 8U);
    copy_ascii_field(identity->model, sizeof(identity->model), response + 16U, 16U);
    copy_ascii_field(identity->revision, sizeof(identity->revision), response + 32U, 4U);
    return true;
}

static bool identity_is_validated(const gdox_gp08_identity *identity)
{
    return strcmp(identity->vendor, GDOX_GP08_SCSI_VENDOR) == 0
        && strcmp(identity->model, GDOX_GP08_SCSI_MODEL) == 0
        && strcmp(identity->revision, GDOX_GP08_SCSI_REVISION) == 0;
}

static bool test_unit_ready_with_timeout(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0U, 0U, 0U, 0U, 0U, 0U};
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
        GDOX_GP08_DEFAULT_TIMEOUT_MS,
        error
    );
}

static bool request_sense(
    gdox_scsi_transport *transport,
    uint8_t output[18],
    size_t *transferred,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x03U, 0U, 0U, 0U, 18U, 0U};
    return gdox_scsi_command_in(
        transport,
        "REQUEST SENSE",
        cdb,
        sizeof(cdb),
        output,
        18U,
        GDOX_GP08_DEFAULT_TIMEOUT_MS,
        transferred,
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
    static const uint8_t cdb[10] = {
        0x25U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
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
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "READ CAPACITY(10) returned a short response");
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
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "DVD structure buffer is too large");
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

static bool read_buffer(
    gdox_scsi_transport *transport,
    uint32_t address,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    uint8_t cdb[10] = {0};
    size_t transferred;

    if (address > UINT32_C(0xffffff)
        || output_bytes == 0U
        || output_bytes > UINT32_C(0xffffff)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "invalid GP08 volatile-memory read");
        return false;
    }
    cdb[0] = 0x3cU;
    cdb[1] = 0x05U;
    put_be_u24(cdb + 3U, address);
    put_be_u24(cdb + 6U, (uint32_t)output_bytes);
    if (!gdox_scsi_command_in(
            transport,
            "GP08 volatile memory read",
            cdb,
            sizeof(cdb),
            output,
            output_bytes,
            GDOX_GP08_DEFAULT_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "GP08 volatile-memory read returned a short transfer");
        return false;
    }
    return true;
}

static bool write_buffer(
    gdox_scsi_transport *transport,
    uint32_t address,
    const uint8_t *input,
    size_t input_bytes,
    gdox_error *error
)
{
    uint8_t cdb[10] = {0};
    size_t transferred;

    if (address > UINT32_C(0xffffff)
        || input == NULL
        || input_bytes == 0U
        || input_bytes > UINT32_C(0xffffff)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "invalid GP08 volatile-memory write");
        return false;
    }
    cdb[0] = 0x3bU;
    cdb[1] = 0x05U;
    put_be_u24(cdb + 3U, address);
    put_be_u24(cdb + 6U, (uint32_t)input_bytes);
    if (!gdox_scsi_command_out(
            transport,
            "GP08 volatile memory write",
            cdb,
            sizeof(cdb),
            input,
            input_bytes,
            GDOX_GP08_DEFAULT_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != input_bytes) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "GP08 volatile-memory write returned a short transfer");
        return false;
    }
    return true;
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
    uint8_t cdb[10] = {0};
    size_t transferred;
    const uint64_t expected = (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;

    if (blocks == 0U || blocks > GDOX_GP08_MAX_READ_BLOCKS
        || expected != output_bytes) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "invalid bounded GP08 READ(10) request");
        return false;
    }
    cdb[0] = 0x28U;
    put_be_u32(cdb + 2U, lba);
    put_be_u16(cdb + 7U, (uint16_t)blocks);
    if (!gdox_scsi_command_in(
            transport,
            "READ(10)",
            cdb,
            sizeof(cdb),
            output,
            output_bytes,
            GDOX_GP08_READ_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "READ(10) returned a short transfer");
        return false;
    }
    return true;
}

static bool ladder_aborted(const atomic_bool *abort, gdox_error *error)
{
    if (abort != NULL
        && atomic_load_explicit(abort, memory_order_acquire)) {
        gdox_error_set(error, GDOX_ERROR_CANCELLED, "optical session was cancelled");
        return true;
    }
    return false;
}

static bool read_state(
    gdox_gp08_context *context,
    const atomic_bool *abort,
    gdox_gp08_state *state,
    gdox_error *error
)
{
    if (ladder_aborted(abort, error)) {
        return false;
    }
    return read_buffer(
            &context->transport,
            GDOX_GP08_CAPACITY_ADDRESS,
            state->capacity,
            sizeof(state->capacity),
            error
        )
        && read_buffer(
            &context->transport,
            GDOX_GP08_ZONE0_ADDRESS,
            state->zone0,
            sizeof(state->zone0),
            error
        )
        && read_buffer(
            &context->transport,
            GDOX_GP08_ZONE1_ADDRESS,
            state->zone1,
            sizeof(state->zone1),
            error
        )
        && read_buffer(
            &context->transport,
            GDOX_GP08_ZONE2_ADDRESS,
            state->zone2,
            sizeof(state->zone2),
            error
        )
        && read_buffer(
            &context->transport,
            GDOX_GP08_END_CACHE_ADDRESS,
            state->end_cache,
            sizeof(state->end_cache),
            error
        )
        && read_buffer(
            &context->transport,
            GDOX_GP08_PFI_END_ADDRESS,
            state->pfi_end,
            sizeof(state->pfi_end),
            error
        )
        && read_capacity(
            &context->transport,
            &state->last_lba,
            &state->block_size,
            error
        );
}

static void make_live_zone0(uint8_t output[16])
{
    memcpy(output, stock_zone0, sizeof(stock_zone0));
    memcpy(output + 12U, live_zone0_length, sizeof(live_zone0_length));
}

static void make_live_pfi(
    const gdox_gp08_state *stock,
    uint8_t output[4]
)
{
    memcpy(output, stock->pfi_end, sizeof(stock->pfi_end));
    memcpy(output, live_pfi_prefix, sizeof(live_pfi_prefix));
}

static bool state_is_stock(const gdox_gp08_state *state)
{
    return memcmp(state->capacity, stock_capacity, sizeof(stock_capacity)) == 0
        && memcmp(state->zone0, stock_zone0, sizeof(stock_zone0)) == 0
        && memcmp(state->zone1, stock_zone1, sizeof(stock_zone1)) == 0
        && memcmp(state->zone2, stock_zone2, sizeof(stock_zone2)) == 0
        && memcmp(state->end_cache, stock_end_cache, sizeof(stock_end_cache)) == 0
        && memcmp(state->pfi_end, stock_pfi_prefix, sizeof(stock_pfi_prefix)) == 0
        && state->last_lba == GDOX_GP08_STOCK_LAST_LBA
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

static bool state_is_live(
    const gdox_gp08_state *state,
    const gdox_gp08_state *stock
)
{
    uint8_t zone0[16];
    uint8_t pfi_end[4];

    make_live_zone0(zone0);
    make_live_pfi(stock, pfi_end);
    return memcmp(state->capacity, live_capacity, sizeof(live_capacity)) == 0
        && memcmp(state->zone0, zone0, sizeof(zone0)) == 0
        && memcmp(state->zone1, live_zone1, sizeof(live_zone1)) == 0
        && memcmp(state->zone2, live_zone2, sizeof(live_zone2)) == 0
        && memcmp(state->end_cache, live_end_cache, sizeof(live_end_cache)) == 0
        && memcmp(state->pfi_end, pfi_end, sizeof(pfi_end)) == 0
        && state->last_lba == GDOX_GP08_LIVE_HOST_LAST_LBA
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

static bool field_is_stock_or_live(
    const uint8_t *field,
    const uint8_t *stock,
    const uint8_t *live,
    size_t bytes
)
{
    return memcmp(field, stock, bytes) == 0
        || memcmp(field, live, bytes) == 0;
}

static bool state_is_known_partial(
    const gdox_gp08_state *state,
    const gdox_gp08_state *stock
)
{
    uint8_t zone0[16];
    uint8_t pfi_end[4];

    make_live_zone0(zone0);
    make_live_pfi(stock, pfi_end);
    return field_is_stock_or_live(
            state->capacity,
            stock_capacity,
            live_capacity,
            sizeof(state->capacity)
        )
        && field_is_stock_or_live(
            state->zone0,
            stock_zone0,
            zone0,
            sizeof(state->zone0)
        )
        && field_is_stock_or_live(
            state->zone1,
            stock_zone1,
            live_zone1,
            sizeof(state->zone1)
        )
        && field_is_stock_or_live(
            state->zone2,
            stock_zone2,
            live_zone2,
            sizeof(state->zone2)
        )
        && field_is_stock_or_live(
            state->end_cache,
            stock_end_cache,
            live_end_cache,
            sizeof(state->end_cache)
        )
        && field_is_stock_or_live(
            state->pfi_end,
            stock->pfi_end,
            pfi_end,
            sizeof(state->pfi_end)
        )
        && (state->last_lba == GDOX_GP08_STOCK_LAST_LBA
            || state->last_lba == GDOX_GP08_LIVE_HOST_LAST_LBA)
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

static bool apply_live(gdox_gp08_context *context, gdox_error *error)
{
    gdox_gp08_state observed;
    uint8_t pfi_end[4];

    make_live_pfi(&context->stock, pfi_end);
    if (!write_buffer(
            &context->transport,
            GDOX_GP08_ZONE0_LENGTH_ADDRESS,
            live_zone0_length,
            sizeof(live_zone0_length),
            error
        )
        || !write_buffer(
            &context->transport,
            GDOX_GP08_ZONE1_ADDRESS,
            live_zone1,
            sizeof(live_zone1),
            error
        )
        || !write_buffer(
            &context->transport,
            GDOX_GP08_ZONE2_ADDRESS,
            live_zone2,
            sizeof(live_zone2),
            error
        )
        || !write_buffer(
            &context->transport,
            GDOX_GP08_END_CACHE_ADDRESS,
            live_end_cache,
            sizeof(live_end_cache),
            error
        )
        || !write_buffer(
            &context->transport,
            GDOX_GP08_PFI_END_ADDRESS,
            pfi_end,
            sizeof(pfi_end),
            error
        )
        || !write_buffer(
            &context->transport,
            GDOX_GP08_CAPACITY_ADDRESS,
            live_capacity,
            sizeof(live_capacity),
            error
        )
        || !read_state(context, &context->abort, &observed, error)) {
        return false;
    }
    if (!state_is_live(&observed, &context->stock)) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "live GP08 volatile state did not verify");
        return false;
    }
    return true;
}

static bool restore_stock(gdox_gp08_context *context, gdox_error *error)
{
    gdox_error first_error;
    gdox_error current;
    gdox_gp08_state observed;
    bool command_failed = false;

    gdox_error_clear(&first_error);
#define GDOX_GP08_RESTORE(address, field)                                      \
    do {                                                                       \
        if (!write_buffer(                                                     \
                &context->transport,                                           \
                (address),                                                     \
                context->stock.field,                                          \
                sizeof(context->stock.field),                                  \
                &current                                                       \
            ) && !command_failed) {                                             \
            first_error = current;                                             \
            command_failed = true;                                             \
        }                                                                      \
    } while (false)

    GDOX_GP08_RESTORE(GDOX_GP08_CAPACITY_ADDRESS, capacity);
    GDOX_GP08_RESTORE(GDOX_GP08_END_CACHE_ADDRESS, end_cache);
    GDOX_GP08_RESTORE(GDOX_GP08_PFI_END_ADDRESS, pfi_end);
    GDOX_GP08_RESTORE(GDOX_GP08_ZONE2_ADDRESS, zone2);
    GDOX_GP08_RESTORE(GDOX_GP08_ZONE1_ADDRESS, zone1);
    if (!write_buffer(
            &context->transport,
            GDOX_GP08_ZONE0_LENGTH_ADDRESS,
            context->stock.zone0 + 12U,
            4U,
            &current
        ) && !command_failed) {
        first_error = current;
        command_failed = true;
    }
#undef GDOX_GP08_RESTORE

    if (read_state(context, NULL, &observed, &current)
        && memcmp(&observed, &context->stock, sizeof(observed)) == 0) {
        return true;
    }
    if (command_failed) {
        *error = first_error;
    } else if (gdox_error_is_set(&current)) {
        *error = current;
    } else {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "stock GP08 volatile state did not verify");
    }
    return false;
}

static bool restore_stock_after_streaming(
    gdox_gp08_context *context,
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

static bool ensure_live(gdox_gp08_context *context, gdox_error *error)
{
    gdox_gp08_state observed;

    if (!read_state(context, &context->abort, &observed, error)) {
        return false;
    }
    if (state_is_live(&observed, &context->stock)) {
        return true;
    }
    if (!state_is_known_partial(&observed, &context->stock)) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "refusing GP08 recovery because volatile memory has an unknown state"
        );
        return false;
    }
    if (!state_is_stock(&observed)
        && !restore_stock(context, error)) {
        return false;
    }
    return apply_live(context, error);
}

static bool recover_optical(gdox_gp08_context *context, gdox_error *error)
{
    static const uint8_t load[6] = {0x1bU, 0U, 0U, 0U, 0x03U, 0U};
    uint32_t attempt;
    gdox_error last;

    gdox_error_clear(&last);
    if (ladder_aborted(&context->abort, error)) {
        return false;
    }
    (void)gdox_scsi_transport_reset(&context->transport, &last);
    {
        uint8_t sense[18];
        size_t transferred;
        (void)request_sense(
            &context->transport,
            sense,
            &transferred,
            &last
        );
    }
    (void)gdox_scsi_command_none(
        &context->transport,
        "START STOP UNIT (load/start)",
        load,
        sizeof(load),
        GDOX_GP08_DEFAULT_TIMEOUT_MS,
        &last
    );
    for (attempt = 0U; attempt < GDOX_GP08_READY_ATTEMPTS; ++attempt) {
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
    gdox_gp08_context *context,
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
                (uint64_t)lba + (uint64_t)blocks - 1U,
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
    gdox_gp08_context *context,
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

static uint64_t gp08_sector_count(const void *raw_context)
{
    (void)raw_context;
    return GDOX_XGD1_TOTAL_SECTORS;
}

static bool gp08_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_gp08_context *context = raw_context;
    uint32_t remaining = blocks;
    uint32_t current_lba = (uint32_t)lba;
    size_t output_offset = 0U;
    uint64_t recovery_deadline_ms;
    bool success = true;

    (void)output_bytes;
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not lock GP08 optical transport");
        return false;
    }
    recovery_deadline_ms =
        gdox_monotonic_ms() + GDOX_GP08_RECOVERY_BUDGET_MS;
    while (remaining != 0U) {
        const uint32_t chunk =
            remaining < GDOX_GP08_MAX_READ_BLOCKS
                ? remaining
                : GDOX_GP08_MAX_READ_BLOCKS;
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
        output_offset += (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES;
    }
    gdox_mutex_unlock(&context->mutex);
    return success;
}

static bool gp08_media_present(const void *raw_context)
{
    gdox_gp08_context *context = (gdox_gp08_context *)raw_context;
    gdox_error error;
    bool present;

    if (!gdox_mutex_lock(&context->mutex)) {
        return false;
    }
    present = test_unit_ready_with_timeout(
        &context->transport,
        GDOX_GP08_PRESENCE_TIMEOUT_MS,
        &error
    );
    gdox_mutex_unlock(&context->mutex);
    return present;
}

static bool gp08_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const gdox_gp08_context *context = raw_context;
    if (output == NULL) {
        return false;
    }
    *output = context->evidence;
    return context->evidence.pfi_present
        || context->evidence.dmi_present
        || context->evidence.security_sector_present;
}

static bool gp08_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const gdox_gp08_context *context = raw_context;
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

static bool gp08_close(void *raw_context, gdox_error *error)
{
    gdox_gp08_context *context = raw_context;
    gdox_error restore_error;
    gdox_error close_error;
    bool restored = true;
    bool closed;

    gdox_error_clear(&restore_error);
    if (gdox_mutex_lock(&context->mutex)) {
        if (context->active) {
            restored = restore_stock_after_streaming(context, &restore_error);
            if (restored) {
                context->active = false;
            }
        }
        gdox_mutex_unlock(&context->mutex);
    } else {
        restored = false;
        gdox_error_set(&restore_error, GDOX_ERROR_INTERNAL, "could not lock GP08 transport during close");
    }
    gdox_mutex_destroy(&context->mutex);
    closed = gdox_scsi_transport_close(&context->transport, &close_error);
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

static void gp08_abort(void *raw_context)
{
    gdox_gp08_context *context = raw_context;
    atomic_store_explicit(&context->abort, true, memory_order_release);
}

static const gdox_sector_source_ops gp08_source_ops = {
    gp08_sector_count,
    gp08_read,
    gp08_media_present,
    gp08_close,
    gp08_evidence,
    gp08_physical_read_stats,
    gp08_abort,
};

static bool open_discovered_gp08(
    void *raw_context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)raw_context;
    return gdox_usb_bot_open(
        GDOX_GP08_USB_VENDOR_ID,
        GDOX_GP08_USB_PRODUCT_ID,
        transport,
        error
    );
}

static bool open_validated_transport(
    gdox_gp08_transport_opener opener,
    void *opener_context,
    gdox_scsi_transport *transport,
    gdox_gp08_identity *identity,
    gdox_error *error
)
{
    uint32_t attempt;

    for (attempt = 0U; attempt < GDOX_GP08_INQUIRY_ATTEMPTS; ++attempt) {
        if (!opener(opener_context, transport, error)) {
            return false;
        }
        if (inquiry(transport, identity, error)) {
            break;
        }
        gdox_scsi_transport_destroy(transport);
        if (error->code != GDOX_ERROR_TRANSPORT
            || attempt + 1U == GDOX_GP08_INQUIRY_ATTEMPTS) {
            return false;
        }
        gdox_sleep_ms(UINT32_C(100));
    }
    if (!identity_is_validated(identity)) {
        gdox_scsi_transport_destroy(transport);
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "USB device is not the validated HL-DT-ST GP08NU10 JE01 mechanism"
        );
        return false;
    }
    return true;
}

static void destroy_failed_open(gdox_gp08_context *context)
{
    gdox_scsi_transport_destroy(&context->transport);
    gdox_mutex_destroy(&context->mutex);
    free(context);
}

bool gdox_gp08_source_open(
    gdox_gp08_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    gdox_gp08_context *context;
    gdox_gp08_identity identity;
    uint32_t attempts;
    uint32_t attempt;
    uint8_t pfi[2052];
    uint8_t dmi[2052];
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES];
    size_t transferred;
    gdox_error operation_error;

    gdox_error_clear(error);
    if (opener == NULL || source == NULL || gdox_source_is_valid(source)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a GP08 transport opener and empty source output are required"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate GP08 source");
        return false;
    }
    atomic_init(&context->read_commands, UINT64_C(0));
    atomic_init(&context->read_sectors, UINT64_C(0));
    atomic_init(&context->read_bytes, UINT64_C(0));
    atomic_init(&context->last_read_lba, UINT64_C(0));
    atomic_init(&context->abort, false);
    if (!gdox_mutex_init(&context->mutex)) {
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize GP08 source lock");
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
        || memcmp(pfi + 17U, stock_pfi_prefix, sizeof(stock_pfi_prefix)) != 0) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "disc does not have the expected original-Xbox decoy geometry"
            );
        }
        destroy_failed_open(context);
        return false;
    }
    context->evidence.pfi_present = true;
    memcpy(context->evidence.pfi, pfi + 4U, GDOX_DISC_STRUCTURE_BYTES);
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
        memcpy(context->evidence.dmi, dmi + 4U, GDOX_DISC_STRUCTURE_BYTES);
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
        || !state_is_stock(&context->stock)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "refusing GP08 session because stock volatile state does not match"
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
                "GP08 initialization failed and volatile-state restoration also failed; power-cycle the drive"
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
            GDOX_GP08_DESCRIPTOR_LBA,
            1U,
            descriptor,
            sizeof(descriptor),
            error
        )
        || memcmp(descriptor, xdvdfs_magic, sizeof(xdvdfs_magic)) != 0
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
                "live GP08 view did not contain a complete XDVDFS descriptor"
            );
        }
        if (!restore_stock_after_streaming(context, error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "GP08 disc validation failed and volatile-state restoration also failed; power-cycle the drive"
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
    source->ops = &gp08_source_ops;
    return true;
}

bool gdox_optical_observe_gp08(
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
        GDOX_GP08_USB_VENDOR_ID,
        GDOX_GP08_USB_PRODUCT_ID,
        &presence->drive_present,
        &presence->media_status_known,
        &presence->media_present,
        error
    );
}

bool gdox_optical_gp08_connected(
    bool *connected,
    gdox_error *error
)
{
    return gdox_usb_bot_present(
        GDOX_GP08_USB_VENDOR_ID,
        GDOX_GP08_USB_PRODUCT_ID,
        connected,
        error
    );
}

bool gdox_optical_open_gp08(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_gp08_source_open(
        open_discovered_gp08,
        NULL,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

bool gdox_optical_eject_gp08(gdox_error *error)
{
    gdox_scsi_transport transport = {0};
    gdox_gp08_identity identity;
    static const uint8_t allow_removal[6] = {0x1eU, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t eject[6] = {0x1bU, 0U, 0U, 0U, 0x02U, 0U};
    gdox_error ignored;
    gdox_error close_error;
    bool success;

    if (!open_validated_transport(
            open_discovered_gp08,
            NULL,
            &transport,
            &identity,
            error
        )) {
        return false;
    }
    (void)gdox_scsi_command_none(
        &transport,
        "PREVENT ALLOW MEDIUM REMOVAL (allow)",
        allow_removal,
        sizeof(allow_removal),
        GDOX_GP08_DEFAULT_TIMEOUT_MS,
        &ignored
    );
    success = gdox_scsi_command_none(
        &transport,
        "START STOP UNIT (eject)",
        eject,
        sizeof(eject),
        GDOX_GP08_READ_TIMEOUT_MS,
        error
    );
    if (!gdox_scsi_transport_close(&transport, &close_error) && success) {
        *error = close_error;
        success = false;
    }
    return success;
}
