#include "gdox/optical.h"
#include "gdox/source.h"
#include "platform/gp08_source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GP08_CAPACITY_ADDRESS UINT32_C(0x80468e)
#define GP08_ZONE0_ADDRESS UINT32_C(0x810600)
#define GP08_ZONE0_LENGTH_ADDRESS UINT32_C(0x81060c)
#define GP08_ZONE1_ADDRESS UINT32_C(0x810644)
#define GP08_ZONE2_ADDRESS UINT32_C(0x810684)
#define GP08_END_CACHE_ADDRESS UINT32_C(0x80aab0)
#define GP08_PFI_END_ADDRESS UINT32_C(0x803b17)
#define GP08_DESCRIPTOR_LBA UINT32_C(0x30620)
#define GP08_STOCK_LAST_LBA UINT32_C(0x1b4f)
#define GP08_LIVE_LAST_LBA UINT32_C(0x3a4d4f)
#define GP08_MAX_LOG_ENTRIES 192U

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
static const uint8_t stock_pfi[4] = {
    0x03U, 0x1aU, 0xafU, 0x5aU,
};
static const uint8_t live_pfi[4] = {
    0x20U, 0x33U, 0xafU, 0x5aU,
};
static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct fake_log_entry {
    uint8_t opcode;
    uint32_t address;
    uint32_t length;
    uint32_t lba;
    uint32_t blocks;
} fake_log_entry;

typedef struct fake_gp08 {
    uint8_t capacity[8];
    uint8_t zone0[16];
    uint8_t zone1[12];
    uint8_t zone2[12];
    uint8_t end_cache[4];
    uint8_t pfi[4];
    uint32_t last_lba;
    fake_log_entry log[GP08_MAX_LOG_ENTRIES];
    size_t log_count;
    uint32_t write_count;
    uint32_t read_capacity_count;
    uint32_t reset_count;
    uint32_t load_start_count;
    uint32_t gesn_count;
    uint32_t sense_count;
    uint32_t transition_gesn_call;
    uint32_t transition_sense_call;
    uint32_t fail_write_number;
    uint32_t prepare_close_calls;
    uint32_t prepare_close_failures;
    bool fail_stock_capacity;
    bool descriptor_has_end_magic;
    bool identity_valid;
    bool fail_next_read;
    bool invalid_command;
    bool closed;
} fake_gp08;

static uint32_t read_be_u24(const uint8_t *input)
{
    return (uint32_t)input[0] << 16U
        | (uint32_t)input[1] << 8U
        | (uint32_t)input[2];
}

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static bool fail(
    gdox_error *error,
    gdox_error_code code,
    const char *message
)
{
    gdox_error_set(error, code, message);
    return false;
}

static bool log_command(
    fake_gp08 *fake,
    uint8_t opcode,
    uint32_t address,
    uint32_t length,
    uint32_t lba,
    uint32_t blocks,
    gdox_error *error
)
{
    fake_log_entry *entry;

    if (fake->log_count >= GP08_MAX_LOG_ENTRIES) {
        return fail(error, GDOX_ERROR_INTERNAL, "fake command log overflow");
    }
    entry = &fake->log[fake->log_count++];
    *entry = (fake_log_entry){opcode, address, length, lba, blocks};
    return true;
}

static bool memory_field(
    fake_gp08 *fake,
    uint32_t address,
    size_t length,
    uint8_t **field
)
{
    if (address == GP08_CAPACITY_ADDRESS
        && length == sizeof(fake->capacity)) {
        *field = fake->capacity;
    } else if (address == GP08_ZONE0_ADDRESS
        && length == sizeof(fake->zone0)) {
        *field = fake->zone0;
    } else if (address == GP08_ZONE0_LENGTH_ADDRESS
        && length == sizeof(live_zone0_length)) {
        *field = fake->zone0 + 12U;
    } else if (address == GP08_ZONE1_ADDRESS
        && length == sizeof(fake->zone1)) {
        *field = fake->zone1;
    } else if (address == GP08_ZONE2_ADDRESS
        && length == sizeof(fake->zone2)) {
        *field = fake->zone2;
    } else if (address == GP08_END_CACHE_ADDRESS
        && length == sizeof(fake->end_cache)) {
        *field = fake->end_cache;
    } else if (address == GP08_PFI_END_ADDRESS
        && length == sizeof(fake->pfi)) {
        *field = fake->pfi;
    } else {
        *field = NULL;
        return false;
    }
    return true;
}

static bool fake_command_in(
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
    fake_gp08 *fake = raw_context;

    (void)name;
    (void)timeout_ms;
    gdox_error_clear(error);
    *transferred = 0U;
    if (cdb[0] == 0x12U && cdb_bytes == 6U && output_bytes == 96U) {
        memset(output, 0, output_bytes);
        memcpy(output + 8U, "HL-DT-ST", 8U);
        memcpy(output + 16U, "DVDRAM GP08NU10", 16U);
        memcpy(
            output + 32U,
            fake->identity_valid ? "JE01" : "JE00",
            4U
        );
    } else if (cdb[0] == 0x4aU && cdb_bytes == 10U
        && output_bytes == 8U) {
        ++fake->gesn_count;
        memset(output, 0, output_bytes);
        output[0] = 0U;
        output[1] = 6U;
        output[2] = 0x84U;
        if (fake->gesn_count == fake->transition_gesn_call) {
            output[2] = 0x04U;
            output[4] = 0x01U;
        }
    } else if (cdb[0] == 0x03U && cdb_bytes == 6U
        && output_bytes == 18U) {
        ++fake->sense_count;
        memset(output, 0, output_bytes);
        if (fake->sense_count == fake->transition_sense_call) {
            output[0] = 0x70U;
            output[2] = 0x06U;
            output[12] = 0x28U;
        }
    } else if (cdb[0] == 0xadU && cdb_bytes == 12U
        && output_bytes == 2052U) {
        memset(output, 0, output_bytes);
        if (cdb[7] == 0U) {
            memcpy(output + 17U, stock_pfi, 3U);
        } else if (cdb[7] != 0x04U) {
            fake->invalid_command = true;
            return fail(error, GDOX_ERROR_PROTOCOL, "unexpected DVD structure");
        }
    } else if (cdb[0] == 0x3cU && cdb_bytes == 10U) {
        const uint32_t address = read_be_u24(cdb + 3U);
        const uint32_t length = read_be_u24(cdb + 6U);
        uint8_t *field;

        if (cdb[1] != 0x05U || cdb[2] != 0U || cdb[9] != 0U
            || length != output_bytes
            || !memory_field(fake, address, output_bytes, &field)) {
            fake->invalid_command = true;
            return fail(error, GDOX_ERROR_PROTOCOL, "invalid READ BUFFER CDB");
        }
        if (!log_command(
                fake,
                cdb[0],
                address,
                length,
                0U,
                0U,
                error
            )) {
            return false;
        }
        memcpy(output, field, output_bytes);
    } else if (cdb[0] == 0x25U && cdb_bytes == 10U
        && output_bytes == 8U) {
        memset(output, 0, output_bytes);
        put_be_u32(output, fake->last_lba);
        put_be_u32(output + 4U, GDOX_LOGICAL_SECTOR_BYTES);
        ++fake->read_capacity_count;
    } else if (cdb[0] == 0x28U && cdb_bytes == 10U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        const uint32_t blocks =
            (uint32_t)cdb[7] << 8U | (uint32_t)cdb[8];

        if (blocks == 0U || blocks > 32U
            || output_bytes
                != (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES) {
            fake->invalid_command = true;
            return fail(error, GDOX_ERROR_PROTOCOL, "invalid READ(10) CDB");
        }
        if (!log_command(
                fake,
                cdb[0],
                0U,
                (uint32_t)output_bytes,
                lba,
                blocks,
                error
            )) {
            return false;
        }
        if (fake->fail_next_read) {
            fake->fail_next_read = false;
            return fail(
                error,
                GDOX_ERROR_TRANSPORT,
                "injected GP08 read failure"
            );
        }
        memset(output, 0xa5, output_bytes);
        if (lba == GP08_DESCRIPTOR_LBA && blocks == 1U) {
            memcpy(output, xdvdfs_magic, sizeof(xdvdfs_magic));
            if (fake->descriptor_has_end_magic) {
                memcpy(
                    output + output_bytes - sizeof(xdvdfs_magic),
                    xdvdfs_magic,
                    sizeof(xdvdfs_magic)
                );
            }
        }
    } else {
        fake->invalid_command = true;
        return fail(error, GDOX_ERROR_PROTOCOL, "unexpected data-in command");
    }
    *transferred = output_bytes;
    return true;
}

static bool fake_command_out(
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
    fake_gp08 *fake = raw_context;
    const uint32_t address = read_be_u24(cdb + 3U);
    const uint32_t length = read_be_u24(cdb + 6U);
    uint8_t *field;

    (void)name;
    (void)timeout_ms;
    gdox_error_clear(error);
    *transferred = 0U;
    if (cdb[0] != 0x3bU || cdb_bytes != 10U
        || cdb[1] != 0x05U || cdb[2] != 0U || cdb[9] != 0U
        || length != input_bytes
        || !memory_field(fake, address, input_bytes, &field)) {
        fake->invalid_command = true;
        return fail(error, GDOX_ERROR_PROTOCOL, "invalid WRITE BUFFER CDB");
    }
    if (!log_command(
            fake,
            cdb[0],
            address,
            length,
            0U,
            0U,
            error
        )) {
        return false;
    }
    ++fake->write_count;
    if (fake->write_count == fake->fail_write_number) {
        return fail(error, GDOX_ERROR_TRANSPORT, "injected GP08 write failure");
    }
    if (address == GP08_CAPACITY_ADDRESS
        && memcmp(input, stock_capacity, sizeof(stock_capacity)) == 0
        && fake->fail_stock_capacity) {
        return fail(
            error,
            GDOX_ERROR_TRANSPORT,
            "injected persistent capacity-restore failure"
        );
    }
    memcpy(field, input, input_bytes);
    if (address == GP08_CAPACITY_ADDRESS) {
        fake->last_lba =
            memcmp(input, live_capacity, sizeof(live_capacity)) == 0
                ? GP08_LIVE_LAST_LBA
                : GP08_STOCK_LAST_LBA;
    }
    *transferred = input_bytes;
    return true;
}

static bool fake_command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    fake_gp08 *fake = raw_context;

    (void)name;
    (void)timeout_ms;
    gdox_error_clear(error);
    if (cdb_bytes != 6U
        || (cdb[0] != 0x00U && cdb[0] != 0x1bU)) {
        fake->invalid_command = true;
        return fail(error, GDOX_ERROR_PROTOCOL, "unexpected no-data command");
    }
    if (cdb[0] == 0x1bU && cdb[4] == 0x03U) {
        ++fake->load_start_count;
    }
    return true;
}

static bool fake_reset(void *raw_context, gdox_error *error)
{
    fake_gp08 *fake = raw_context;
    ++fake->reset_count;
    gdox_error_clear(error);
    return true;
}

static bool fake_close(void *raw_context, gdox_error *error)
{
    fake_gp08 *fake = raw_context;
    fake->closed = true;
    gdox_error_clear(error);
    return true;
}

static bool fake_prepare_close(void *raw_context, gdox_error *error)
{
    fake_gp08 *fake = raw_context;
    ++fake->prepare_close_calls;
    if (fake->prepare_close_failures != 0U) {
        --fake->prepare_close_failures;
        return fail(
            error,
            GDOX_ERROR_TRANSPORT,
            "injected transport close preparation failure"
        );
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_scsi_transport_ops fake_ops = {
    fake_command_in,
    fake_command_out,
    fake_command_none,
    fake_reset,
    fake_close,
    fake_prepare_close,
    NULL,
};

static bool fake_open(
    void *raw_context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_error_clear(error);
    transport->context = raw_context;
    transport->ops = &fake_ops;
    return true;
}

static void fake_initialize(fake_gp08 *fake)
{
    memset(fake, 0, sizeof(*fake));
    memcpy(fake->capacity, stock_capacity, sizeof(fake->capacity));
    memcpy(fake->zone0, stock_zone0, sizeof(fake->zone0));
    memcpy(fake->zone1, stock_zone1, sizeof(fake->zone1));
    memcpy(fake->zone2, stock_zone2, sizeof(fake->zone2));
    memcpy(fake->end_cache, stock_end_cache, sizeof(fake->end_cache));
    memcpy(fake->pfi, stock_pfi, sizeof(fake->pfi));
    fake->last_lba = GP08_STOCK_LAST_LBA;
    fake->descriptor_has_end_magic = true;
    fake->identity_valid = true;
}

static bool fake_is_stock(const fake_gp08 *fake)
{
    return memcmp(fake->capacity, stock_capacity, sizeof(stock_capacity)) == 0
        && memcmp(fake->zone0, stock_zone0, sizeof(stock_zone0)) == 0
        && memcmp(fake->zone1, stock_zone1, sizeof(stock_zone1)) == 0
        && memcmp(fake->zone2, stock_zone2, sizeof(stock_zone2)) == 0
        && memcmp(
            fake->end_cache,
            stock_end_cache,
            sizeof(stock_end_cache)
        ) == 0
        && memcmp(fake->pfi, stock_pfi, sizeof(stock_pfi)) == 0
        && fake->last_lba == GP08_STOCK_LAST_LBA;
}

static bool fake_is_live(const fake_gp08 *fake)
{
    return memcmp(fake->capacity, live_capacity, sizeof(live_capacity)) == 0
        && memcmp(
            fake->zone0 + 12U,
            live_zone0_length,
            sizeof(live_zone0_length)
        ) == 0
        && memcmp(fake->zone1, live_zone1, sizeof(live_zone1)) == 0
        && memcmp(fake->zone2, live_zone2, sizeof(live_zone2)) == 0
        && memcmp(
            fake->end_cache,
            live_end_cache,
            sizeof(live_end_cache)
        ) == 0
        && memcmp(fake->pfi, live_pfi, sizeof(live_pfi)) == 0
        && fake->last_lba == GP08_LIVE_LAST_LBA;
}

static bool log_tail_matches(
    const fake_gp08 *fake,
    const uint32_t *addresses,
    const uint32_t *lengths,
    size_t entries
)
{
    size_t remaining = entries;
    size_t index = fake->log_count;

    while (index > 0U && remaining > 0U) {
        const fake_log_entry *entry = &fake->log[--index];
        if (entry->opcode == 0x3bU) {
            --remaining;
            if (entry->address != addresses[remaining]
                || entry->length != lengths[remaining]) {
                return false;
            }
        }
    }
    return remaining == 0U;
}

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "%s:%d: check failed: %s\n",                                    \
                __FILE__,                                                       \
                __LINE__,                                                       \
                #expression                                                     \
            );                                                                  \
            return false;                                                       \
        }                                                                       \
    } while (false)

static bool test_success_and_chunking(void)
{
    static const uint32_t activation_addresses[6] = {
        GP08_ZONE0_LENGTH_ADDRESS,
        GP08_ZONE1_ADDRESS,
        GP08_ZONE2_ADDRESS,
        GP08_END_CACHE_ADDRESS,
        GP08_PFI_END_ADDRESS,
        GP08_CAPACITY_ADDRESS,
    };
    static const uint32_t activation_lengths[6] = {
        4U, 12U, 12U, 4U, 4U, 8U,
    };
    static const uint32_t restore_addresses[6] = {
        GP08_CAPACITY_ADDRESS,
        GP08_END_CACHE_ADDRESS,
        GP08_PFI_END_ADDRESS,
        GP08_ZONE2_ADDRESS,
        GP08_ZONE1_ADDRESS,
        GP08_ZONE0_LENGTH_ADDRESS,
    };
    static const uint32_t restore_lengths[6] = {
        8U, 4U, 4U, 12U, 12U, 4U,
    };
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_physical_read_stats stats;
    gdox_error error;
    uint8_t output[70U * GDOX_LOGICAL_SECTOR_BYTES];
    size_t read10_index[4];
    size_t read10_count = 0U;
    size_t index;

    fake_initialize(&fake);
    CHECK(gdox_gp08_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(!fake.invalid_command);
    CHECK(fake_is_live(&fake));
    CHECK(fake.log_count >= 13U);
    for (index = 0U; index < 6U; ++index) {
        const fake_log_entry *entry = &fake.log[6U + index];
        CHECK(entry->opcode == 0x3bU);
        CHECK(entry->address == activation_addresses[index]);
        CHECK(entry->length == activation_lengths[index]);
    }
    CHECK(gdox_source_read(
        &source,
        0U,
        70U,
        output,
        sizeof(output),
        &error
    ));
    for (index = 0U; index < fake.log_count; ++index) {
        if (fake.log[index].opcode == 0x28U) {
            CHECK(read10_count < 4U);
            read10_index[read10_count++] = index;
        }
    }
    CHECK(read10_count == 4U);
    CHECK(fake.log[read10_index[0]].lba == GP08_DESCRIPTOR_LBA);
    CHECK(fake.log[read10_index[0]].blocks == 1U);
    CHECK(fake.log[read10_index[1]].lba == 0U);
    CHECK(fake.log[read10_index[1]].blocks == 32U);
    CHECK(fake.log[read10_index[2]].lba == 32U);
    CHECK(fake.log[read10_index[2]].blocks == 32U);
    CHECK(fake.log[read10_index[3]].lba == 64U);
    CHECK(fake.log[read10_index[3]].blocks == 6U);
    CHECK(gdox_source_physical_read_stats(&source, &stats));
    CHECK(stats.commands == UINT64_C(3));
    CHECK(stats.sectors == UINT64_C(70));
    CHECK(
        stats.bytes
        == UINT64_C(70) * GDOX_LOGICAL_SECTOR_BYTES
    );
    CHECK(stats.last_lba == UINT64_C(69));
    CHECK(gdox_source_close(&source, &error));
    CHECK(fake.closed);
    CHECK(fake_is_stock(&fake));
    CHECK(log_tail_matches(
        &fake,
        restore_addresses,
        restore_lengths,
        6U
    ));
    CHECK(!fake.invalid_command);
    return true;
}

static bool test_activation_failures_restore(void)
{
    static const uint32_t restore_addresses[6] = {
        GP08_CAPACITY_ADDRESS,
        GP08_END_CACHE_ADDRESS,
        GP08_PFI_END_ADDRESS,
        GP08_ZONE2_ADDRESS,
        GP08_ZONE1_ADDRESS,
        GP08_ZONE0_LENGTH_ADDRESS,
    };
    static const uint32_t restore_lengths[6] = {
        8U, 4U, 4U, 12U, 12U, 4U,
    };
    uint32_t stage;

    for (stage = 1U; stage <= 6U; ++stage) {
        fake_gp08 fake;
        gdox_sector_source source = {0};
        gdox_error error;

        fake_initialize(&fake);
        fake.fail_write_number = stage;
        CHECK(!gdox_gp08_source_open(
            fake_open,
            &fake,
            0U,
            0U,
            &source,
            &error
        ));
        CHECK(error.code == GDOX_ERROR_TRANSPORT);
        CHECK(!gdox_source_is_valid(&source));
        CHECK(fake.closed);
        CHECK(fake_is_stock(&fake));
        CHECK(fake.read_capacity_count >= 2U);
        CHECK(log_tail_matches(
            &fake,
            restore_addresses,
            restore_lengths,
            6U
        ));
        CHECK(!fake.invalid_command);
    }
    return true;
}

static bool test_identity_and_stock_must_match(void)
{
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_error error;

    fake_initialize(&fake);
    fake.identity_valid = false;
    CHECK(!gdox_gp08_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
    CHECK(fake_is_stock(&fake));

    fake_initialize(&fake);
    fake.zone1[0] = 0x01U;
    CHECK(!gdox_gp08_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
    CHECK(!fake.invalid_command);
    return true;
}

static bool test_descriptor_rejection_restores(void)
{
    static const uint32_t restore_addresses[6] = {
        GP08_CAPACITY_ADDRESS,
        GP08_END_CACHE_ADDRESS,
        GP08_PFI_END_ADDRESS,
        GP08_ZONE2_ADDRESS,
        GP08_ZONE1_ADDRESS,
        GP08_ZONE0_LENGTH_ADDRESS,
    };
    static const uint32_t restore_lengths[6] = {
        8U, 4U, 4U, 12U, 12U, 4U,
    };
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_error error;

    fake_initialize(&fake);
    fake.descriptor_has_end_magic = false;
    CHECK(!gdox_gp08_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    CHECK(strstr(error.message, "complete XDVDFS descriptor") != NULL);
    CHECK(fake.closed);
    CHECK(fake_is_stock(&fake));
    CHECK(log_tail_matches(
        &fake,
        restore_addresses,
        restore_lengths,
        6U
    ));
    CHECK(!fake.invalid_command);
    return true;
}

static bool test_failed_open_retains_transport_for_close_retry(void)
{
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_error error;

    fake_initialize(&fake);
    fake.descriptor_has_end_magic = false;
    fake.prepare_close_failures = 1U;
    CHECK(!gdox_gp08_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_TRANSPORT);
    CHECK(gdox_source_is_valid(&source));
    CHECK(fake_is_stock(&fake));
    CHECK(!fake.closed);
    CHECK(fake.prepare_close_calls == 1U);

    CHECK(gdox_source_close(&source, &error));
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.closed);
    CHECK(fake.prepare_close_calls == 3U);
    return true;
}

static bool test_persistent_restore_failure_is_explicit(void)
{
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_error error;

    fake_initialize(&fake);
    fake.descriptor_has_end_magic = false;
    fake.fail_stock_capacity = true;
    CHECK(!gdox_gp08_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_TRANSPORT);
    CHECK(strstr(error.message, "power-cycle the drive") != NULL);
    CHECK(gdox_source_is_valid(&source));
    CHECK(!fake.closed);
    CHECK(fake.reset_count == 4U);
    CHECK(!fake_is_stock(&fake));
    CHECK(!fake.invalid_command);
    fake.fail_stock_capacity = false;
    CHECK(gdox_source_close(&source, &error));
    CHECK(fake.closed);
    CHECK(fake_is_stock(&fake));
    return true;
}

static bool test_recovery_stops_before_reset_on_eject_event(void)
{
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    uint32_t reset_count;
    uint32_t write_count;

    fake_initialize(&fake);
    CHECK(gdox_gp08_source_open(
        fake_open,
        &fake,
        1U,
        0U,
        &source,
        &error
    ));
    reset_count = fake.reset_count;
    write_count = fake.write_count;
    fake.fail_next_read = true;
    fake.transition_gesn_call = fake.gesn_count + 1U;
    CHECK(!gdox_source_read(
        &source,
        0U,
        1U,
        output,
        sizeof(output),
        &error
    ));
    CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    CHECK(strstr(error.message, "physical eject requested") != NULL);
    CHECK(fake.reset_count == reset_count);
    CHECK(fake.load_start_count == 0U);
    CHECK(fake.write_count == write_count);
    CHECK(gdox_source_close(&source, &error));
    return true;
}

static bool test_recovery_stops_before_load_on_post_reset_sense(void)
{
    fake_gp08 fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    uint32_t reset_count;
    uint32_t write_count;

    fake_initialize(&fake);
    CHECK(gdox_gp08_source_open(
        fake_open,
        &fake,
        1U,
        0U,
        &source,
        &error
    ));
    reset_count = fake.reset_count;
    write_count = fake.write_count;
    fake.fail_next_read = true;
    fake.transition_sense_call = fake.sense_count + 2U;
    CHECK(!gdox_source_read(
        &source,
        0U,
        1U,
        output,
        sizeof(output),
        &error
    ));
    CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    CHECK(strstr(error.message, "physical media changed") != NULL);
    CHECK(fake.reset_count == reset_count + 1U);
    CHECK(fake.load_start_count == 0U);
    CHECK(fake.write_count == write_count);
    CHECK(gdox_source_close(&source, &error));
    return true;
}

int main(void)
{
    if (!test_success_and_chunking()
        || !test_activation_failures_restore()
        || !test_identity_and_stock_must_match()
        || !test_descriptor_rejection_restores()
        || !test_failed_open_retains_transport_for_close_retry()
        || !test_persistent_restore_failure_is_explicit()
        || !test_recovery_stops_before_reset_on_eject_event()
        || !test_recovery_stops_before_load_on_post_reset_sense()) {
        return 1;
    }
    (void)puts("GP08 adapter tests passed");
    return 0;
}
