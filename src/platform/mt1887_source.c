#define _POSIX_C_SOURCE 200809L

#include "gdox/optical.h"

#include "platform/mt1887_source.h"
#include "platform/mt1887_profile.h"
#include "platform/portable_sync.h"
#include "platform/scsi_transport.h"
#include "platform/usb_bot.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_MT_DEFAULT_TIMEOUT_MS UINT32_C(5000)
#define GDOX_MT_PRESENCE_TIMEOUT_MS UINT32_C(1000)
#define GDOX_MT_READ_TIMEOUT_MS UINT32_C(30000)
#define GDOX_MT_MAX_READ_BLOCKS UINT32_C(128)
#define GDOX_MT_MAXIMUM_READ_SPEED UINT16_C(0xffff)
#define GDOX_MT_DIAGNOSTIC_ATTEMPTS UINT32_C(3)
#define GDOX_MT_READY_ATTEMPTS UINT32_C(20)
#define GDOX_MT_INQUIRY_ATTEMPTS UINT32_C(2)
#define GDOX_MT_DESCRIPTOR_LBA UINT32_C(198176)
/*
 * Total recovery time permitted within one source read. Successful reads
 * never consult this budget; it only stops the retry ladder from stalling a
 * caller indefinitely on a failing disc or detached drive.
 */
#define GDOX_MT_RECOVERY_BUDGET_MS UINT32_C(20000)

static const uint8_t stock_capacity[3] = {0x03U, 0x1bU, 0x4fU};
static const uint8_t xgd_capacity[3] = {0x3dU, 0x4dU, 0x4fU};
static const uint8_t stock_geometry[3] = {0x03U, 0x1aU, 0xafU};
static const uint8_t xgd_geometry[3] = {0x20U, 0x33U, 0xafU};
static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct gdox_mt1887_context {
    gdox_scsi_transport transport;
    const gdox_mt1887_profile *profile;
    gdox_mutex mutex;
    gdox_disc_evidence evidence;
    atomic_uint_fast64_t read_commands;
    atomic_uint_fast64_t read_sectors;
    atomic_uint_fast64_t read_bytes;
    atomic_uint_fast64_t last_read_lba;
    atomic_bool abort;
    uint16_t read_speed_kbps;
    uint32_t max_read_blocks;
    uint8_t retries;
    bool restoration_required;
} gdox_mt1887_context;

typedef struct gdox_optical_identity {
    char vendor[9];
    char model[17];
    char revision[5];
} gdox_optical_identity;

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
    gdox_optical_identity *identity,
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
            GDOX_MT_DEFAULT_TIMEOUT_MS,
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
        GDOX_MT_DEFAULT_TIMEOUT_MS,
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
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        transferred,
        error
    );
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

static bool read_capacity(
    gdox_scsi_transport *transport,
    uint32_t *last_lba,
    uint32_t *block_size,
    gdox_error *error
)
{
    static const uint8_t cdb[10] = {0x25U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
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

static bool read12(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};
    size_t transferred;
    const uint64_t expected = (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;

    if (blocks == 0U || blocks > GDOX_MT_MAX_READ_BLOCKS
        || expected != output_bytes) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "invalid bounded READ(12) request");
        return false;
    }
    cdb[0] = 0xa8U;
    put_be_u32(cdb + 2U, lba);
    put_be_u32(cdb + 6U, blocks);
    if (!gdox_scsi_command_in(
            transport,
            "READ(12)",
            cdb,
            sizeof(cdb),
            output,
            output_bytes,
            GDOX_MT_READ_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "READ(12) returned a short transfer");
        return false;
    }
    return true;
}

static bool request_read_speed(
    gdox_scsi_transport *transport,
    uint16_t kilobytes_per_second,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};

    cdb[0] = 0xbbU;
    put_be_u16(cdb + 2U, kilobytes_per_second);
    return gdox_scsi_command_none(
        transport,
        "SET CD SPEED",
        cdb,
        sizeof(cdb),
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        error
    );
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

static bool read_xdata(
    gdox_scsi_transport *transport,
    const atomic_bool *abort,
    uint16_t address,
    uint8_t *value,
    gdox_error *error
)
{
    uint8_t cdb[10] = {
        0xf1U, 0x02U, 0U, 0U, 0U, 0U, 0x01U, 0U, 0U, 0U,
    };
    uint8_t response[4];
    uint32_t attempt;

    cdb[4] = (uint8_t)(address >> 8U);
    cdb[5] = (uint8_t)(address & 0xffU);
    for (attempt = 0U; attempt < GDOX_MT_DIAGNOSTIC_ATTEMPTS; ++attempt) {
        size_t transferred;
        if (ladder_aborted(abort, error)) {
            return false;
        }
        if (gdox_scsi_command_in(
                transport,
                "MT1887 F1 diagnostic XDATA read",
                cdb,
                sizeof(cdb),
                response,
                sizeof(response),
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                &transferred,
                error
            ) && transferred == sizeof(response)) {
            *value = response[3];
            return true;
        }
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "MT1887 diagnostic XDATA read returned a short response"
            );
        }
        if (error->code == GDOX_ERROR_NOT_FOUND) {
            return false;
        }
        if (attempt + 1U < GDOX_MT_DIAGNOSTIC_ATTEMPTS) {
            gdox_error ignored;
            (void)gdox_scsi_transport_reset(transport, &ignored);
            gdox_sleep_ms(UINT32_C(100) * (attempt + 1U));
        }
    }
    return false;
}

static bool write_xdata(
    gdox_scsi_transport *transport,
    uint16_t address,
    uint8_t value,
    gdox_error *error
)
{
    uint8_t cdb[10] = {
        0xf1U, 0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
    cdb[4] = (uint8_t)(address >> 8U);
    cdb[5] = (uint8_t)(address & 0xffU);
    cdb[9] = value;
    return gdox_scsi_command_none(
        transport,
        "MT1887 F1 volatile XDATA write",
        cdb,
        sizeof(cdb),
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        error
    );
}

static bool read_triplet(
    gdox_scsi_transport *transport,
    const atomic_bool *abort,
    const uint16_t addresses[3],
    uint8_t values[3],
    gdox_error *error
)
{
    size_t index;
    for (index = 0U; index < 3U; ++index) {
        if (!read_xdata(transport, abort, addresses[index], &values[index], error)) {
            return false;
        }
    }
    return true;
}

static bool write_triplet(
    gdox_scsi_transport *transport,
    const uint16_t addresses[3],
    const uint8_t values[3],
    bool attempt_all,
    gdox_error *error
)
{
    gdox_error first_error;
    bool failed = false;
    size_t index;

    gdox_error_clear(&first_error);
    for (index = 0U; index < 3U; ++index) {
        gdox_error current;
        if (!write_xdata(transport, addresses[index], values[index], &current)) {
            if (!failed) {
                first_error = current;
                failed = true;
            }
            if (!attempt_all) {
                break;
            }
        }
    }
    if (failed) {
        *error = first_error;
        return false;
    }
    return true;
}

static bool read_state(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const atomic_bool *abort,
    gdox_mt1887_state *state,
    gdox_error *error
)
{
    memset(state, 0, sizeof(*state));
    return read_triplet(
            transport,
            abort,
            profile->capacity_addresses,
            state->capacity,
            error
        )
        && read_triplet(
            transport,
            abort,
            profile->geometry_addresses,
            state->geometry,
            error
        )
        && (!profile->auxiliary_present
            || read_triplet(
                transport,
                abort,
                profile->auxiliary_addresses,
                state->auxiliary,
                error
            ))
        && read_capacity(
            transport,
            &state->last_lba,
            &state->block_size,
            error
        );
}

static bool restore_stock(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const atomic_bool *abort,
    gdox_error *error
)
{
    gdox_error first_error;
    gdox_error current;
    bool failed = false;
    gdox_mt1887_state state;

    gdox_error_clear(&first_error);
    if (!write_triplet(
            transport,
            profile->geometry_addresses,
            stock_geometry,
            true,
            &current
        )) {
        first_error = current;
        failed = true;
    }
    if (!write_triplet(
            transport,
            profile->capacity_addresses,
            stock_capacity,
            true,
            &current
        ) && !failed) {
        first_error = current;
        failed = true;
    }
    if (profile->auxiliary_present
        && !write_triplet(
            transport,
            profile->auxiliary_addresses,
            profile->auxiliary,
            true,
            &current
        ) && !failed) {
        first_error = current;
        failed = true;
    }
    if (failed) {
        *error = first_error;
        return false;
    }
    if (!read_state(transport, profile, abort, &state, error)) {
        return false;
    }
    if (!gdox_mt1887_state_is_stock(profile, &state)) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "stock MT1887 SRAM state did not verify");
        return false;
    }
    return true;
}

static bool restore_stock_after_streaming(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    gdox_error *error
)
{
    gdox_error last;
    uint32_t attempt;

    if (restore_stock(transport, profile, NULL, &last)) {
        return true;
    }
    for (attempt = 0U; attempt < 2U; ++attempt) {
        gdox_error ignored;

        (void)gdox_scsi_transport_reset(transport, &ignored);
        gdox_sleep_ms(UINT32_C(100) * (attempt + 1U));
        if (restore_stock(transport, profile, NULL, &last)) {
            return true;
        }
    }
    gdox_error_set(
        error,
        GDOX_ERROR_TRANSPORT,
        "could not restore optical SRAM; power-cycle the drive"
    );
    return false;
}

static bool recover_known_partial(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const atomic_bool *abort,
    bool *restoration_required,
    gdox_error *error
)
{
    gdox_mt1887_state state;

    if (!read_state(transport, profile, abort, &state, error)) {
        return false;
    }
    if (!gdox_mt1887_state_is_known(profile, &state)) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "refusing MT1887 session because volatile SRAM has an unknown state"
        );
        return false;
    }
    if (state.block_size != GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "refusing MT1887 recovery because the logical block size is not 2048 bytes"
        );
        return false;
    }
    if (!gdox_mt1887_state_is_stock(profile, &state)) {
        if (restoration_required != NULL) {
            *restoration_required = true;
        }
        return restore_stock(transport, profile, abort, error);
    }
    return true;
}

static bool apply_xgd(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const atomic_bool *abort,
    gdox_error *error
)
{
    gdox_mt1887_state state;

    if (!write_triplet(
            transport,
            profile->capacity_addresses,
            xgd_capacity,
            false,
            error
        )
        || !write_triplet(
            transport,
            profile->geometry_addresses,
            xgd_geometry,
            false,
            error
        )
        || !read_state(transport, profile, abort, &state, error)) {
        return false;
    }
    if (!gdox_mt1887_state_is_live(profile, &state)) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "live MT1887 SRAM state did not verify");
        return false;
    }
    return true;
}

static bool ensure_xgd(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const atomic_bool *abort,
    gdox_error *error
)
{
    gdox_mt1887_state state;
    const bool state_read =
        read_state(transport, profile, abort, &state, error);

    if (state_read) {
        if (gdox_mt1887_state_is_live(profile, &state)) {
            return true;
        }
    } else {
        if (error->code == GDOX_ERROR_CANCELLED
            || error->code == GDOX_ERROR_NOT_FOUND) {
            return false;
        }
    }
    if (!recover_known_partial(transport, profile, abort, NULL, error)) {
        return false;
    }
    return apply_xgd(transport, profile, abort, error);
}

static bool recover_optical(
    gdox_scsi_transport *transport,
    const atomic_bool *abort,
    gdox_error *error
)
{
    static const uint8_t load[6] = {0x1bU, 0U, 0U, 0U, 0x03U, 0U};
    uint32_t attempt;
    gdox_error last;

    gdox_error_clear(error);
    gdox_error_clear(&last);
    if (ladder_aborted(abort, error)) {
        return false;
    }
    (void)gdox_scsi_transport_reset(transport, &last);
    {
        uint8_t sense[18];
        size_t transferred;
        (void)request_sense(transport, sense, &transferred, &last);
    }
    (void)gdox_scsi_command_none(
        transport,
        "START STOP UNIT (load/start)",
        load,
        sizeof(load),
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        &last
    );
    for (attempt = 0U; attempt < GDOX_MT_READY_ATTEMPTS; ++attempt) {
        if (ladder_aborted(abort, error)) {
            return false;
        }
        if (test_unit_ready(transport, &last)) {
            return true;
        }
        if (last.code == GDOX_ERROR_NOT_FOUND) {
            break;
        }
        if (attempt == 7U || attempt == 15U) {
            (void)gdox_scsi_command_none(
                transport,
                "START STOP UNIT (load/start)",
                load,
                sizeof(load),
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                &last
            );
        }
        gdox_sleep_ms(UINT32_C(250));
    }
    *error = last;
    return false;
}

static bool read_range_with_recovery(
    gdox_mt1887_context *context,
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
        if (read12(
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
            if (attempt != 0U) {
                (void)fprintf(
                    stderr,
                    "GDOX: recovered optical read at physical sector %u"
                    " after %u retry\n",
                    lba,
                    attempt
                );
                (void)fflush(stderr);
            }
            return true;
        }
        if (last.code == GDOX_ERROR_NOT_FOUND
            || last.code == GDOX_ERROR_CANCELLED) {
            break;
        }
        if (gdox_monotonic_ms() >= recovery_deadline_ms) {
            break;
        }
        if (attempt < context->retries) {
            (void)fprintf(
                stderr,
                "GDOX: retrying optical read at physical sector %u"
                " (%u blocks): %s\n",
                lba,
                blocks,
                last.message
            );
            (void)fflush(stderr);
            gdox_error recovery;
            gdox_error_clear(&recovery);
            if (!recover_optical(&context->transport, &context->abort, &recovery)
                || !ensure_xgd(
                    &context->transport,
                    context->profile,
                    &context->abort,
                    &recovery
                )) {
                last = recovery;
                if (recovery.code == GDOX_ERROR_NOT_FOUND
                    || recovery.code == GDOX_ERROR_CANCELLED) {
                    break;
                }
            } else {
                (void)request_read_speed(
                    &context->transport,
                    context->read_speed_kbps,
                    &recovery
                );
            }
            gdox_sleep_ms(UINT32_C(150) * (attempt + 1U));
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
    gdox_mt1887_context *context,
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

static uint64_t mt_sector_count(const void *context)
{
    (void)context;
    return GDOX_XGD1_TOTAL_SECTORS;
}

static bool mt_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_mt1887_context *context = raw_context;
    uint32_t remaining = blocks;
    uint32_t current_lba = (uint32_t)lba;
    size_t output_offset = 0U;
    uint64_t recovery_deadline_ms;
    bool success = true;

    (void)output_bytes;
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not lock optical transport");
        return false;
    }
    recovery_deadline_ms = gdox_monotonic_ms() + GDOX_MT_RECOVERY_BUDGET_MS;
    while (remaining != 0U) {
        const uint32_t chunk =
            remaining < context->max_read_blocks
                ? remaining
                : context->max_read_blocks;
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

static bool mt_media_present(const void *raw_context)
{
    gdox_mt1887_context *context = (gdox_mt1887_context *)raw_context;
    gdox_error error;
    bool present;

    if (!gdox_mutex_lock(&context->mutex)) {
        return false;
    }
    present = test_unit_ready_with_timeout(
        &context->transport,
        GDOX_MT_PRESENCE_TIMEOUT_MS,
        &error
    );
    gdox_mutex_unlock(&context->mutex);
    return present;
}

static bool mt_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const gdox_mt1887_context *context = raw_context;
    if (output == NULL) {
        return false;
    }
    *output = context->evidence;
    return context->evidence.pfi_present
        || context->evidence.dmi_present
        || context->evidence.security_sector_present;
}

static bool mt_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const gdox_mt1887_context *context = raw_context;
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

static bool mt_close(void *raw_context, gdox_error *error)
{
    gdox_mt1887_context *context = raw_context;
    gdox_error restore_error;
    gdox_error close_error;
    bool restored = true;
    bool closed;

    gdox_error_clear(&restore_error);
    if (gdox_mutex_lock(&context->mutex)) {
        if (context->restoration_required) {
            restored = restore_stock_after_streaming(
                &context->transport,
                context->profile,
                &restore_error
            );
            if (restored) {
                context->restoration_required = false;
            }
        }
        gdox_mutex_unlock(&context->mutex);
    } else {
        restored = false;
        gdox_error_set(&restore_error, GDOX_ERROR_INTERNAL, "could not lock optical transport during close");
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

static void mt_abort(void *raw_context)
{
    gdox_mt1887_context *context = raw_context;
    atomic_store_explicit(&context->abort, true, memory_order_release);
}

static const gdox_sector_source_ops mt_source_ops = {
    mt_sector_count,
    mt_read,
    mt_media_present,
    mt_close,
    mt_evidence,
    mt_physical_read_stats,
    mt_abort,
};

static bool open_discovered_gp63(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)context;
    return gdox_usb_bot_open(
        GDOX_USB_BOT_GP63,
        transport,
        error
    );
}

static bool open_discovered_gp65(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)context;
    return gdox_usb_bot_open(
        GDOX_USB_BOT_GP65,
        transport,
        error
    );
}

static bool open_validated_transport(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    gdox_usb_bot_identity expected_identity,
    gdox_scsi_transport *transport,
    gdox_optical_identity *identity,
    const gdox_mt1887_profile **profile,
    gdox_error *error
)
{
    uint32_t attempt;

    for (attempt = 0U; attempt < GDOX_MT_INQUIRY_ATTEMPTS; ++attempt) {
        if (!opener(opener_context, transport, error)) {
            return false;
        }
        if (inquiry(transport, identity, error)) {
            break;
        }
        gdox_scsi_transport_destroy(transport);
        if (error->code != GDOX_ERROR_TRANSPORT
            || attempt + 1U == GDOX_MT_INQUIRY_ATTEMPTS) {
            return false;
        }
        gdox_sleep_ms(UINT32_C(100));
    }
    *profile = gdox_mt1887_profile_find(
        expected_identity,
        identity->vendor,
        identity->model,
        identity->revision
    );
    if (*profile == NULL) {
        gdox_scsi_transport_destroy(transport);
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "USB device does not match the selected validated optical profile"
        );
        return false;
    }
    return true;
}

bool gdox_optical_observe_gp63(
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
    return gdox_usb_bot_observe(
        GDOX_USB_BOT_GP63,
        &presence->drive_present,
        &presence->media_status_known,
        &presence->media_present,
        error
    );
}

bool gdox_optical_gp63_connected(
    bool *connected,
    gdox_error *error
)
{
    return gdox_usb_bot_present(
        GDOX_USB_BOT_GP63,
        connected,
        error
    );
}

bool gdox_optical_observe_gp65(
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
    return gdox_usb_bot_observe(
        GDOX_USB_BOT_GP65,
        &presence->drive_present,
        &presence->media_status_known,
        &presence->media_present,
        error
    );
}

bool gdox_optical_gp65_connected(
    bool *connected,
    gdox_error *error
)
{
    return gdox_usb_bot_present(
        GDOX_USB_BOT_GP65,
        connected,
        error
    );
}

bool gdox_mt1887_source_open(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    gdox_usb_bot_identity expected_identity,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    gdox_mt1887_context *context;
    gdox_optical_identity identity;
    uint32_t attempts;
    uint32_t attempt;
    uint8_t pfi[2052];
    uint8_t dmi[2052];
    size_t transferred;
    gdox_error operation_error;
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES];

    gdox_error_clear(error);
    if (opener == NULL
        || (expected_identity != GDOX_USB_BOT_GP63
            && expected_identity != GDOX_USB_BOT_GP65)
        || source == NULL || gdox_source_is_valid(source)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a transport opener and empty source output are required"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate MT1887 source");
        return false;
    }
    atomic_init(&context->read_commands, UINT64_C(0));
    atomic_init(&context->read_sectors, UINT64_C(0));
    atomic_init(&context->read_bytes, UINT64_C(0));
    atomic_init(&context->last_read_lba, UINT64_C(0));
    atomic_init(&context->abort, false);
    context->read_speed_kbps = read_speed_kbps;
    if (!gdox_mutex_init(&context->mutex)) {
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize MT1887 source lock");
        return false;
    }
    if (!open_validated_transport(
            opener,
            opener_context,
            expected_identity,
            &context->transport,
            &identity,
            &context->profile,
            error
        )) {
        gdox_mutex_destroy(&context->mutex);
        free(context);
        return false;
    }
#if defined(_WIN32)
    context->max_read_blocks =
        gdox_mt1887_max_read_blocks(context->profile, true);
#else
    context->max_read_blocks =
        gdox_mt1887_max_read_blocks(context->profile, false);
#endif
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
        gdox_scsi_transport_destroy(&context->transport);
        gdox_mutex_destroy(&context->mutex);
        free(context);
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
        || memcmp(pfi + 17U, stock_geometry, sizeof(stock_geometry)) != 0) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "disc does not have the expected original-Xbox decoy geometry"
            );
        }
        gdox_scsi_transport_destroy(&context->transport);
        gdox_mutex_destroy(&context->mutex);
        free(context);
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
    if (!recover_known_partial(
            &context->transport,
            context->profile,
            &context->abort,
            &context->restoration_required,
            error
        )) {
        if (context->restoration_required) {
            operation_error = *error;
            if (restore_stock_after_streaming(
                    &context->transport,
                    context->profile,
                    error
                )) {
                *error = operation_error;
            }
        }
        gdox_scsi_transport_destroy(&context->transport);
        gdox_mutex_destroy(&context->mutex);
        free(context);
        return false;
    }
    context->restoration_required = true;
    if (!apply_xgd(
            &context->transport,
            context->profile,
            &context->abort,
            error
        )) {
        operation_error = *error;
        if (!restore_stock_after_streaming(
                &context->transport,
                context->profile,
                error
            )) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "live-XGD initialization failed and SRAM restoration also failed; power-cycle the drive"
            );
        } else {
            *error = operation_error;
        }
        gdox_scsi_transport_destroy(&context->transport);
        gdox_mutex_destroy(&context->mutex);
        free(context);
        return false;
    }
    /*
     * SET CD SPEED applies to DVD reads as well. The platform adapter chooses
     * a rate appropriate to its power and throughput constraints. A drive may
     * legally reject this optional command, so streaming remains available at
     * its current speed in that case.
     */
    if (request_read_speed(
            &context->transport,
            context->read_speed_kbps,
            &operation_error
        )) {
        (void)fprintf(
            stderr,
            "GDOX: requested optical read speed %u KB/s\n",
            (unsigned int)context->read_speed_kbps
        );
        (void)fflush(stderr);
    } else {
        (void)fprintf(
            stderr,
            "GDOX: drive retained its current read speed: %s\n",
            operation_error.message
        );
        (void)fflush(stderr);
    }
    if (!read12(
            &context->transport,
            GDOX_MT_DESCRIPTOR_LBA,
            1U,
            descriptor,
            sizeof(descriptor),
            error
        )
        || memcmp(descriptor, xdvdfs_magic, sizeof(xdvdfs_magic)) != 0) {
        operation_error = *error;
        if (!gdox_error_is_set(&operation_error)) {
            gdox_error_set(
                &operation_error,
                GDOX_ERROR_NOT_FOUND,
                "live XGD view did not contain an XDVDFS descriptor"
            );
        }
        if (!restore_stock_after_streaming(
                &context->transport,
                context->profile,
                error
            )) {
            gdox_scsi_transport_destroy(&context->transport);
            gdox_mutex_destroy(&context->mutex);
            free(context);
            return false;
        }
        context->restoration_required = false;
        gdox_scsi_transport_destroy(&context->transport);
        gdox_mutex_destroy(&context->mutex);
        free(context);
        *error = operation_error;
        return false;
    }
    context->retries = read_retries;
    source->context = context;
    source->ops = &mt_source_ops;
    return true;
}

bool gdox_optical_open_gp63(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_mt1887_source_open(
        open_discovered_gp63,
        NULL,
        GDOX_USB_BOT_GP63,
        GDOX_MT_MAXIMUM_READ_SPEED,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

bool gdox_optical_open_gp65(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_mt1887_source_open(
        open_discovered_gp65,
        NULL,
        GDOX_USB_BOT_GP65,
        GDOX_MT_MAXIMUM_READ_SPEED,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

static bool eject_profile(
    gdox_mt1887_transport_opener opener,
    gdox_usb_bot_identity expected_identity,
    gdox_error *error
)
{
    gdox_scsi_transport transport = {0};
    gdox_optical_identity identity;
    const gdox_mt1887_profile *profile;
    static const uint8_t allow_removal[6] = {0x1eU, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t eject[6] = {0x1bU, 0U, 0U, 0U, 0x02U, 0U};
    gdox_error ignored;
    gdox_error close_error;
    bool success;

    if (!open_validated_transport(
            opener,
            NULL,
            expected_identity,
            &transport,
            &identity,
            &profile,
            error
        )) {
        return false;
    }
    (void)gdox_scsi_command_none(
        &transport,
        "PREVENT ALLOW MEDIUM REMOVAL (allow)",
        allow_removal,
        sizeof(allow_removal),
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        &ignored
    );
    success = gdox_scsi_command_none(
        &transport,
        "START STOP UNIT (eject)",
        eject,
        sizeof(eject),
        GDOX_MT_READ_TIMEOUT_MS,
        error
    );
    if (!gdox_scsi_transport_close(&transport, &close_error) && success) {
        *error = close_error;
        success = false;
    }
    return success;
}

bool gdox_optical_eject_gp63(gdox_error *error)
{
    return eject_profile(
        open_discovered_gp63,
        GDOX_USB_BOT_GP63,
        error
    );
}

bool gdox_optical_eject_gp65(gdox_error *error)
{
    return eject_profile(
        open_discovered_gp65,
        GDOX_USB_BOT_GP65,
        error
    );
}
