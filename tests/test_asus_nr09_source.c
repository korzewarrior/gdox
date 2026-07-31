#include "gdox/optical.h"
#include "gdox/source.h"
#include "platform/asus_nr09_source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASUS_FIELD_COUNT 8U
#define ASUS_DESCRIPTOR_LBA UINT32_C(0x30620)
#define ASUS_STOCK_LAST_LBA UINT32_C(0x1b4f)
#define ASUS_LIVE_LAST_LBA UINT32_C(0x3a4d4f)
#define ASUS_MAX_LOG_ENTRIES 128U

typedef struct expected_field {
    uint32_t address;
    uint8_t subcommand;
    uint8_t stock[4];
    uint8_t live[4];
} expected_field;

static const expected_field expected_fields[ASUS_FIELD_COUNT] = {
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
};


static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct fake_log_entry {
    uint8_t opcode;
    uint32_t address;
    uint8_t subcommand;
    uint32_t lba;
    uint32_t blocks;
    bool write;
} fake_log_entry;

typedef struct fake_asus {
    const expected_field *profile_fields;
    uint8_t pfi_prefix[3];
    uint8_t expected_start_psn[4];
    uint8_t expected_complemented_start[4];
    uint32_t descriptor_lba;
    uint32_t stock_last_lba;
    uint32_t live_last_lba;
    uint8_t values[ASUS_FIELD_COUNT][4];
    uint8_t start_psn[4];
    uint8_t complemented_start[4];
    uint32_t last_lba;
    fake_log_entry log[ASUS_MAX_LOG_ENTRIES];
    size_t log_count;
    uint32_t write_count;
    uint32_t fail_write_number;
    bool identity_valid;
    bool descriptor_has_end_magic;
    bool invalid_command;
    bool closed;
} fake_asus;

static uint16_t read_be_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] << 8U | input[1]);
}

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static void put_be_u32(uint8_t output[4], uint32_t value)
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
    fake_asus *fake,
    uint8_t opcode,
    uint32_t address,
    uint8_t subcommand,
    uint32_t lba,
    uint32_t blocks,
    bool write,
    gdox_error *error
)
{
    if (fake->log_count >= ASUS_MAX_LOG_ENTRIES) {
        return fail(
            error,
            GDOX_ERROR_INTERNAL,
            "fake command log overflow"
        );
    }
    fake->log[fake->log_count++] = (fake_log_entry){
        opcode,
        address,
        subcommand,
        lba,
        blocks,
        write,
    };
    return true;
}

static bool memory_field(
    fake_asus *fake,
    uint32_t address,
    uint8_t subcommand,
    uint8_t **field
)
{
    size_t index;

    for (index = 0U; index < ASUS_FIELD_COUNT; ++index) {
        if (fake->profile_fields[index].address == address
            && fake->profile_fields[index].subcommand == subcommand) {
            *field = fake->values[index];
            return true;
        }
    }
    if (address == UINT32_C(0x18fc) && subcommand == 0x01U) {
        *field = fake->start_psn;
        return true;
    }
    if (address == UINT32_C(0x1904) && subcommand == 0x01U) {
        *field = fake->complemented_start;
        return true;
    }
    *field = NULL;
    return false;
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
    fake_asus *fake = raw_context;

    (void)name;
    (void)timeout_ms;
    gdox_error_clear(error);
    *transferred = 0U;
    if (cdb[0] == 0x12U && cdb_bytes == 6U
        && output_bytes == 96U) {
        memset(output, 0, output_bytes);
        memcpy(output + 8U, "ASUS    ", 8U); /* NOLINT: fixed-width INQUIRY field */
        memcpy( /* NOLINT: fixed-width INQUIRY field */
            output + 16U,
            "SDRW-08D1S-U    ",
            16U
        );
        memcpy( /* NOLINT: fixed-width INQUIRY field */
            output + 32U,
            fake->identity_valid ? "A202" : "A201",
            4U
        );
    } else if (cdb[0] == 0xadU && cdb_bytes == 12U
        && output_bytes == 2052U) {
        memset(output, 0, output_bytes);
        if (cdb[7] == 0U) {
            memcpy(output + 17U, fake->pfi_prefix, 3U);
        } else if (cdb[7] != 0x04U) {
            fake->invalid_command = true;
            return fail(
                error,
                GDOX_ERROR_PROTOCOL,
                "unexpected DVD structure"
            );
        }
    } else if (cdb[0] == 0xf1U && cdb_bytes == 12U
        && cdb[1] == 0U && output_bytes == 4U
        && read_be_u16(cdb + 7U) == 4U) {
        const uint32_t address = read_be_u32(cdb + 2U);
        uint8_t *field;

        if (!memory_field(fake, address, cdb[9], &field)
            || !log_command(
                fake,
                cdb[0],
                address,
                cdb[9],
                0U,
                0U,
                false,
                error
            )) {
            fake->invalid_command = true;
            return fail(
                error,
                GDOX_ERROR_PROTOCOL,
                "invalid F1 memory read"
            );
        }
        memcpy(output, field, output_bytes);
    } else if (cdb[0] == 0x25U && cdb_bytes == 10U
        && output_bytes == 8U) {
        put_be_u32(output, fake->last_lba);
        put_be_u32(output + 4U, GDOX_LOGICAL_SECTOR_BYTES);
    } else if (cdb[0] == 0x03U && cdb_bytes == 6U
        && output_bytes == 18U) {
        memset(output, 0, output_bytes);
    } else if (cdb[0] == 0x28U && cdb_bytes == 10U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        const uint32_t blocks = read_be_u16(cdb + 7U);

        if (blocks == 0U || blocks > 32U
            || output_bytes
                != (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES
            || !log_command(
                fake,
                cdb[0],
                0U,
                0U,
                lba,
                blocks,
                false,
                error
            )) {
            fake->invalid_command = true;
            return fail(
                error,
                GDOX_ERROR_PROTOCOL,
                "invalid READ(10)"
            );
        }
        memset(output, 0xa5, output_bytes);
        if (lba == fake->descriptor_lba && blocks == 1U) {
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
        return fail(
            error,
            GDOX_ERROR_PROTOCOL,
            "unexpected data-in command"
        );
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
    fake_asus *fake = raw_context;
    const uint32_t address = read_be_u32(cdb + 2U);
    uint8_t *field;

    (void)name;
    (void)timeout_ms;
    gdox_error_clear(error);
    *transferred = 0U;
    if (cdb[0] != 0xf1U || cdb_bytes != 12U
        || cdb[1] != 0x01U || input_bytes != 4U
        || read_be_u16(cdb + 7U) != 4U
        || !memory_field(fake, address, cdb[9], &field)
        || !log_command(
            fake,
            cdb[0],
            address,
            cdb[9],
            0U,
            0U,
            true,
            error
        )) {
        fake->invalid_command = true;
        return fail(
            error,
            GDOX_ERROR_PROTOCOL,
            "invalid F1 memory write"
        );
    }
    ++fake->write_count;
    if (fake->write_count == fake->fail_write_number) {
        return fail(
            error,
            GDOX_ERROR_TRANSPORT,
            "injected ASUS write failure"
        );
    }
    memcpy(field, input, input_bytes);
    if (address == UINT32_C(0x1600)) {
        fake->last_lba =
            memcmp(
                input,
                fake->profile_fields[7].live,
                input_bytes
            ) == 0
                ? fake->live_last_lba
                : fake->stock_last_lba;
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
    fake_asus *fake = raw_context;

    (void)name;
    (void)timeout_ms;
    gdox_error_clear(error);
    if (cdb_bytes != 6U || cdb[0] != 0U) {
        fake->invalid_command = true;
        return fail(
            error,
            GDOX_ERROR_PROTOCOL,
            "unexpected no-data command"
        );
    }
    return true;
}

static bool fake_reset(void *raw_context, gdox_error *error)
{
    (void)raw_context;
    gdox_error_clear(error);
    return true;
}

static bool fake_close(void *raw_context, gdox_error *error)
{
    fake_asus *fake = raw_context;
    fake->closed = true;
    gdox_error_clear(error);
    return true;
}

static const gdox_scsi_transport_ops fake_ops = {
    fake_command_in,
    fake_command_out,
    fake_command_none,
    fake_reset,
    fake_close,
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

static void fake_initialize(fake_asus *fake)
{
    size_t index;

    memset(fake, 0, sizeof(*fake));
    fake->profile_fields = expected_fields;
    memcpy(
        fake->pfi_prefix,
        (const uint8_t[]){0x03U, 0x1aU, 0xafU},
        sizeof(fake->pfi_prefix)
    );
    memcpy(
        fake->expected_start_psn,
        (const uint8_t[]){0x00U, 0x00U, 0x03U, 0x00U},
        sizeof(fake->expected_start_psn)
    );
    memcpy(
        fake->expected_complemented_start,
        (const uint8_t[]){0x10U, 0x1aU, 0x03U, 0x00U},
        sizeof(fake->expected_complemented_start)
    );
    fake->descriptor_lba = ASUS_DESCRIPTOR_LBA;
    fake->stock_last_lba = ASUS_STOCK_LAST_LBA;
    fake->live_last_lba = ASUS_LIVE_LAST_LBA;
    for (index = 0U; index < ASUS_FIELD_COUNT; ++index) {
        memcpy(
            fake->values[index],
            fake->profile_fields[index].stock,
            4U
        );
    }
    memcpy(
        fake->start_psn,
        fake->expected_start_psn,
        4U
    );
    memcpy(
        fake->complemented_start,
        fake->expected_complemented_start,
        4U
    );
    fake->last_lba = fake->stock_last_lba;
    fake->identity_valid = true;
    fake->descriptor_has_end_magic = true;
}

static bool fake_state_matches(const fake_asus *fake, bool live)
{
    size_t index;

    for (index = 0U; index < ASUS_FIELD_COUNT; ++index) {
        const uint8_t *expected =
            live ? fake->profile_fields[index].live
                 : fake->profile_fields[index].stock;
        if (memcmp(fake->values[index], expected, 4U) != 0) {
            return false;
        }
    }
    return fake->last_lba
        == (live ? fake->live_last_lba : fake->stock_last_lba);
}

static bool write_tail_matches(
    const fake_asus *fake,
    const uint32_t *addresses,
    size_t count
)
{
    size_t remaining = count;
    size_t index = fake->log_count;

    while (index > 0U && remaining > 0U) {
        const fake_log_entry *entry = &fake->log[--index];
        if (entry->opcode == 0xf1U && entry->write) {
            --remaining;
            if (entry->address != addresses[remaining]) {
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
    static const uint32_t activation_order[ASUS_FIELD_COUNT] = {
        UINT32_C(0x888da044),
        UINT32_C(0x1750),
        UINT32_C(0x18f4),
        UINT32_C(0x18f8),
        UINT32_C(0x1900),
        UINT32_C(0x1908),
        UINT32_C(0x19cc),
        UINT32_C(0x1600),
    };
    static const uint32_t restore_order[ASUS_FIELD_COUNT] = {
        UINT32_C(0x1600),
        UINT32_C(0x888da044),
        UINT32_C(0x1750),
        UINT32_C(0x18f4),
        UINT32_C(0x18f8),
        UINT32_C(0x1900),
        UINT32_C(0x1908),
        UINT32_C(0x19cc),
    };
    fake_asus fake;
    gdox_sector_source source = {0};
    gdox_physical_read_stats stats;
    gdox_error error;
    uint8_t output[70U * GDOX_LOGICAL_SECTOR_BYTES];
    uint32_t observed_blocks[4];
    size_t observed_count = 0U;
    size_t index;

    fake_initialize(&fake);
    CHECK(gdox_asus_nr09_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(fake_state_matches(&fake, true));
    CHECK(write_tail_matches(
        &fake,
        activation_order,
        ASUS_FIELD_COUNT
    ));
    CHECK(!fake.invalid_command);
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
            CHECK(observed_count < 4U);
            observed_blocks[observed_count++] =
                fake.log[index].blocks;
        }
    }
    CHECK(observed_count == 4U);
    CHECK(observed_blocks[0] == 1U);
    CHECK(observed_blocks[1] == 32U);
    CHECK(observed_blocks[2] == 32U);
    CHECK(observed_blocks[3] == 6U);
    CHECK(gdox_source_physical_read_stats(&source, &stats));
    CHECK(stats.commands == UINT64_C(3));
    CHECK(stats.sectors == UINT64_C(70));
    CHECK(stats.bytes
        == UINT64_C(70) * GDOX_LOGICAL_SECTOR_BYTES);
    CHECK(stats.last_lba == UINT64_C(69));
    CHECK(gdox_source_close(&source, &error));
    CHECK(fake.closed);
    CHECK(fake_state_matches(&fake, false));
    CHECK(write_tail_matches(
        &fake,
        restore_order,
        ASUS_FIELD_COUNT
    ));
    CHECK(!fake.invalid_command);
    return true;
}

static bool test_activation_failures_restore(void)
{
    uint32_t stage;

    for (stage = 1U; stage <= ASUS_FIELD_COUNT; ++stage) {
        fake_asus fake;
        gdox_sector_source source = {0};
        gdox_error error;

        fake_initialize(&fake);
        fake.fail_write_number = stage;
        CHECK(!gdox_asus_nr09_source_open(
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
        CHECK(fake_state_matches(&fake, false));
        CHECK(!fake.invalid_command);
    }
    return true;
}

static bool test_identity_and_stock_gate(void)
{
    fake_asus fake;
    gdox_sector_source source = {0};
    gdox_error error;

    fake_initialize(&fake);
    fake.identity_valid = false;
    CHECK(!gdox_asus_nr09_source_open(
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

    fake_initialize(&fake);
    fake.pfi_prefix[2] ^= 0x01U;
    CHECK(!gdox_asus_nr09_source_open(
        fake_open,
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);

    fake_initialize(&fake);
    fake.start_psn[0] ^= 0x01U;
    CHECK(!gdox_asus_nr09_source_open(
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

    fake_initialize(&fake);
    fake.complemented_start[3] ^= 0x01U;
    CHECK(!gdox_asus_nr09_source_open(
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

    fake_initialize(&fake);
    ++fake.last_lba;
    CHECK(!gdox_asus_nr09_source_open(
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

    fake_initialize(&fake);
    fake.values[3][0] ^= 0x01U;
    CHECK(!gdox_asus_nr09_source_open(
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
    fake_asus fake;
    gdox_sector_source source = {0};
    gdox_error error;

    fake_initialize(&fake);
    fake.descriptor_has_end_magic = false;
    CHECK(!gdox_asus_nr09_source_open(
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
    CHECK(fake_state_matches(&fake, false));
    CHECK(!fake.invalid_command);
    return true;
}

int main(void)
{
    if (!test_success_and_chunking()
        || !test_activation_failures_restore()
        || !test_identity_and_stock_gate()
        || !test_descriptor_rejection_restores()) {
        return 1;
    }
    (void)puts("ASUS NR09 adapter tests passed");
    return 0;
}
