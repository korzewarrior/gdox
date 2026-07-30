#include "gdox/optical.h"
#include "gdox/source.h"
#include "platform/mt1887_source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t stock_capacity[3] = {0x03U, 0x1bU, 0x4fU};
static const uint8_t live_capacity[3] = {0x3dU, 0x4dU, 0x4fU};
static const uint8_t stock_geometry[3] = {0x03U, 0x1aU, 0xafU};
static const uint8_t live_geometry[3] = {0x20U, 0x33U, 0xafU};
static const uint8_t canonical_auxiliary[3] = {0x64U, 0x00U, 0x64U};
static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct fake_write {
    uint16_t address;
    uint8_t value;
} fake_write;

typedef struct fake_mt1887 {
    uint8_t capacity[3];
    uint8_t geometry[3];
    uint8_t auxiliary[3];
    char revision[5];
    fake_write writes[64];
    size_t write_count;
    uint32_t read_blocks[8];
    size_t read_count;
    bool descriptor_valid;
    bool fail_stock_capacity;
    bool fail_read_once;
    bool reset_to_stock;
    uint16_t fail_write_address_once;
    uint32_t forced_last_lba;
    uint32_t forced_block_size;
    unsigned int reset_count;
    bool closed;
} fake_mt1887;

static bool check(bool condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "failed: %s\n", name);
        return false;
    }
    return true;
}

static void put_be_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static uint32_t read_be_u32(const uint8_t input[4])
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | input[3];
}

static uint8_t *fake_xdata(fake_mt1887 *fake, uint16_t address)
{
    if (address >= 0x8a37U && address <= 0x8a39U) {
        return &fake->capacity[address - 0x8a37U];
    }
    if (address >= 0x8be2U && address <= 0x8be4U) {
        return &fake->geometry[address - 0x8be2U];
    }
    if (address >= 0x8538U && address <= 0x853aU) {
        return &fake->auxiliary[address - 0x8538U];
    }
    return NULL;
}

static bool fake_is_live(const fake_mt1887 *fake)
{
    return memcmp(fake->capacity, live_capacity, 3U) == 0
        && memcmp(fake->geometry, live_geometry, 3U) == 0;
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
    fake_mt1887 *fake = raw_context;

    (void)name;
    (void)cdb_bytes;
    (void)timeout_ms;
    memset(output, 0, output_bytes);
    if (cdb[0] == 0x12U && output_bytes >= 36U) {
        memcpy(output + 8U, "HL-DT-ST", 8U);
        memcpy(output + 16U, "DVDRAM GP65NB60", 16U);
        memcpy(output + 32U, fake->revision, 4U);
    } else if (cdb[0] == 0x03U && output_bytes == 18U) {
        output[0] = 0x70U;
    } else if (cdb[0] == 0xadU && output_bytes == 2052U) {
        if (cdb[7] == 0U) {
            memcpy(output + 17U, stock_geometry, 3U);
        }
    } else if (cdb[0] == 0xf1U && cdb[1] == 0x02U
        && output_bytes == 4U) {
        const uint16_t address =
            (uint16_t)((uint16_t)cdb[4] << 8U | cdb[5]);
        uint8_t *value = fake_xdata(fake, address);
        if (value == NULL) {
            gdox_error_set(error, GDOX_ERROR_TRANSPORT, "unknown XDATA read");
            return false;
        }
        output[3] = *value;
    } else if (cdb[0] == 0x25U && output_bytes == 8U) {
        put_be_u32(
            output,
            fake->forced_last_lba != 0U
                    && memcmp(fake->capacity, stock_capacity, 3U) != 0
                    && !fake_is_live(fake)
                ? fake->forced_last_lba
                : fake_is_live(fake) ? 3820879U : 6991U
        );
        put_be_u32(
            output + 4U,
            fake->forced_block_size != 0U
                ? fake->forced_block_size
                : GDOX_LOGICAL_SECTOR_BYTES
        );
    } else if (cdb[0] == 0xa8U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        if (fake->fail_read_once) {
            fake->fail_read_once = false;
            gdox_error_set(error, GDOX_ERROR_TRANSPORT,
                "injected sector read failure");
            return false;
        }
        if (fake->read_count < 8U) {
            fake->read_blocks[fake->read_count++] =
                read_be_u32(cdb + 6U);
        }
        if (lba == 198176U && fake->descriptor_valid) {
            memcpy(output, xdvdfs_magic, sizeof(xdvdfs_magic));
        }
    } else {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected data-in command");
        return false;
    }
    *transferred = output_bytes;
    gdox_error_clear(error);
    return true;
}

static bool fake_command_out(
    void *context,
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
    (void)context;
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

static bool fake_command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    fake_mt1887 *fake = raw_context;

    (void)name;
    (void)cdb_bytes;
    (void)timeout_ms;
    if (cdb[0] == 0U || cdb[0] == 0x1bU || cdb[0] == 0xbbU) {
        gdox_error_clear(error);
        return true;
    }
    if (cdb[0] == 0xf1U && cdb[1] == 0x01U) {
        const uint16_t address =
            (uint16_t)((uint16_t)cdb[4] << 8U | cdb[5]);
        uint8_t *value = fake_xdata(fake, address);
        if (value == NULL || fake->write_count >= 64U) {
            gdox_error_set(error, GDOX_ERROR_TRANSPORT, "unknown XDATA write");
            return false;
        }
        fake->writes[fake->write_count++] =
            (fake_write){address, cdb[9]};
        if (fake->fail_write_address_once == address) {
            fake->fail_write_address_once = 0U;
            gdox_error_set(error, GDOX_ERROR_TRANSPORT,
                "injected volatile write failure");
            return false;
        }
        if (!(fake->fail_stock_capacity
                && address >= 0x8a37U && address <= 0x8a39U
                && cdb[9] == stock_capacity[address - 0x8a37U])) {
            *value = cdb[9];
        }
        gdox_error_clear(error);
        return true;
    }
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected no-data command");
    return false;
}

static bool fake_reset(void *context, gdox_error *error)
{
    fake_mt1887 *fake = context;
    ++fake->reset_count;
    if (fake->reset_to_stock) {
        memcpy(fake->capacity, stock_capacity, 3U);
        memcpy(fake->geometry, stock_geometry, 3U);
        memcpy(fake->auxiliary, canonical_auxiliary, 3U);
    }
    gdox_error_clear(error);
    return true;
}

static bool fake_close(void *raw_context, gdox_error *error)
{
    fake_mt1887 *fake = raw_context;
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
    transport->context = raw_context;
    transport->ops = &fake_ops;
    gdox_error_clear(error);
    return true;
}

static fake_mt1887 fake_stock(void)
{
    fake_mt1887 fake = {0};

    memcpy(fake.capacity, stock_capacity, 3U);
    memcpy(fake.geometry, stock_geometry, 3U);
    memcpy(fake.auxiliary, canonical_auxiliary, 3U);
    memcpy(fake.revision, "PB00", 5U);
    fake.descriptor_valid = true;
    return fake;
}

static bool writes_begin_at(
    const fake_mt1887 *fake,
    size_t offset,
    uint16_t address
)
{
    return fake->write_count >= offset + 3U
        && fake->writes[offset].address == address
        && fake->writes[offset + 1U].address == address + 1U
        && fake->writes[offset + 2U].address == address + 2U;
}

static bool test_activation_and_restore(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t read_output[65U * GDOX_LOGICAL_SECTOR_BYTES];

    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "PB00 source opens")) {
        return false;
    }
    fake.read_count = 0U;
    if (!check(gdox_source_read(
            &source,
            0U,
            65U,
            read_output,
            sizeof(read_output),
            &error
        ), "source reads 65 sectors")
#if defined(_WIN32)
        || !check(fake.read_count == 3U, "Windows PB00 read uses three transfers")
        || !check(fake.read_blocks[0] == 32U, "first Windows transfer is 32 sectors")
        || !check(fake.read_blocks[1] == 32U, "second Windows transfer is 32 sectors")
        || !check(fake.read_blocks[2] == 1U, "final Windows transfer is one sector")
#else
        || !check(fake.read_count == 1U, "non-Windows PB00 read remains one transfer")
        || !check(fake.read_blocks[0] == 65U, "non-Windows transfer keeps 65 sectors")
#endif
        || !check(fake.write_count == 6U, "activation writes six bytes")
        || !check(writes_begin_at(&fake, 0U, 0x8a37U), "capacity activates first")
        || !check(writes_begin_at(&fake, 3U, 0x8be2U), "geometry activates second")
        || !check(memcmp(fake.auxiliary, canonical_auxiliary, 3U) == 0,
            "activation preserves auxiliary")
        || !check(gdox_source_close(&source, &error), "source restores on close")
        || !check(fake.write_count == 15U, "close writes complete restore")
        || !check(writes_begin_at(&fake, 6U, 0x8be2U), "geometry restores first")
        || !check(writes_begin_at(&fake, 9U, 0x8a37U), "capacity restores second")
        || !check(writes_begin_at(&fake, 12U, 0x8538U), "auxiliary restores last")
        || !check(memcmp(fake.capacity, stock_capacity, 3U) == 0,
            "capacity restored")
        || !check(memcmp(fake.geometry, stock_geometry, 3U) == 0,
            "geometry restored")
        || !check(memcmp(fake.auxiliary, canonical_auxiliary, 3U) == 0,
            "auxiliary canonical")) {
        return false;
    }
    return true;
}

static bool test_auxiliary_recovery(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    memcpy(fake.auxiliary, stock_capacity, 3U);
    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "known old auxiliary recovers")
        || !check(writes_begin_at(&fake, 0U, 0x8be2U),
            "recovery starts with geometry")
        || !check(writes_begin_at(&fake, 3U, 0x8a37U),
            "recovery continues with capacity")
        || !check(writes_begin_at(&fake, 6U, 0x8538U),
            "recovery canonicalizes auxiliary")
        || !check(writes_begin_at(&fake, 9U, 0x8a37U),
            "activation follows recovery")) {
        return false;
    }
    return check(gdox_source_close(&source, &error), "recovered source closes");
}

static bool test_intermediate_capacity_recovery(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.capacity[0] = live_capacity[0];
    fake.forced_last_lba = UINT32_C(123456);
    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "known intermediate capacity recovers")
        || !check(writes_begin_at(&fake, 0U, 0x8be2U),
            "intermediate recovery starts with geometry")
        || !check(writes_begin_at(&fake, 3U, 0x8a37U),
            "intermediate recovery restores capacity")) {
        return false;
    }
    fake.forced_last_lba = 0U;
    return check(gdox_source_close(&source, &error),
        "intermediate source closes");
}

static bool test_unknown_block_size_is_not_written(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.capacity[0] = live_capacity[0];
    fake.forced_block_size = UINT32_C(4096);
    return check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "unknown block size rejected")
        && check(fake.write_count == 0U,
            "unknown block size is not written");
}

static bool test_rejections_restore(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.auxiliary[0] = 0xffU;
    if (!check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "unknown auxiliary rejected")
        || !check(fake.write_count == 0U, "unknown state is not written")) {
        return false;
    }

    fake = fake_stock();
    memcpy(fake.revision, "PB01", 5U);
    if (!check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "PB01 rejected")
        || !check(fake.write_count == 0U, "PB01 is not written")) {
        return false;
    }

    fake = fake_stock();
    fake.descriptor_valid = false;
    return check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "missing descriptor rejected")
        && check(fake.write_count == 15U, "descriptor failure restores")
        && check(memcmp(fake.capacity, stock_capacity, 3U) == 0,
            "descriptor failure restores capacity")
        && check(memcmp(fake.geometry, stock_geometry, 3U) == 0,
            "descriptor failure restores geometry")
        && check(memcmp(fake.auxiliary, canonical_auxiliary, 3U) == 0,
            "descriptor failure restores auxiliary");
}

static bool test_activation_write_failure_restores(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.fail_write_address_once = UINT16_C(0x8a38);
    return check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "activation write failure returned")
        && check(strstr(error.message, "injected volatile write failure") != NULL,
            "activation write error preserved")
        && check(fake.write_count == 11U,
            "activation write failure runs complete restore")
        && check(writes_begin_at(&fake, 2U, 0x8be2U),
            "failed activation restores geometry first")
        && check(writes_begin_at(&fake, 5U, 0x8a37U),
            "failed activation restores capacity second")
        && check(writes_begin_at(&fake, 8U, 0x8538U),
            "failed activation restores auxiliary last")
        && check(memcmp(fake.capacity, stock_capacity, 3U) == 0,
            "failed activation restores stock capacity")
        && check(memcmp(fake.geometry, stock_geometry, 3U) == 0,
            "failed activation restores stock geometry")
        && check(memcmp(fake.auxiliary, canonical_auxiliary, 3U) == 0,
            "failed activation restores canonical auxiliary");
}

static bool test_abort_then_close_restores(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "abort source opens")) {
        return false;
    }
    gdox_source_abort(&source);
    return check(gdox_source_close(&source, &error),
            "aborted source restores on close")
        && check(fake.write_count == 15U,
            "aborted close writes complete restore")
        && check(memcmp(fake.capacity, stock_capacity, 3U) == 0,
            "aborted close restores capacity")
        && check(memcmp(fake.geometry, stock_geometry, 3U) == 0,
            "aborted close restores geometry")
        && check(memcmp(fake.auxiliary, canonical_auxiliary, 3U) == 0,
            "aborted close restores auxiliary");
}

static bool test_read_recovery_reapplies_live_state(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];

    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            1U,
            0U,
            &source,
            &error
        ), "recovery source opens")) {
        return false;
    }
    fake.fail_read_once = true;
    fake.reset_to_stock = true;
    if (!check(gdox_source_read(
            &source,
            0U,
            1U,
            output,
            sizeof(output),
            &error
        ), "read recovery reapplies live state")
        || !check(fake.reset_count >= 1U,
            "read recovery resets transport")
        || !check(fake.write_count == 12U,
            "read recovery reapplies six live bytes")
        || !check(fake_is_live(&fake),
            "read recovery leaves live state")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
            "recovered read source closes")
        && check(memcmp(fake.capacity, stock_capacity, 3U) == 0,
            "recovered read restores capacity")
        && check(memcmp(fake.geometry, stock_geometry, 3U) == 0,
            "recovered read restores geometry")
        && check(memcmp(fake.auxiliary, canonical_auxiliary, 3U) == 0,
            "recovered read restores auxiliary");
}

static bool test_persistent_restore_failure(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.descriptor_valid = false;
    fake.fail_stock_capacity = true;
    return check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "persistent restore failure returned")
        && check(strstr(error.message, "power-cycle the drive") != NULL,
            "persistent restore failure requires power cycle")
        && check(fake.reset_count == 2U, "restore retried three times")
        && check(fake.write_count == 33U, "every restore attempt writes all fields");
}

int main(void)
{
    if (!test_activation_and_restore()
        || !test_auxiliary_recovery()
        || !test_intermediate_capacity_recovery()
        || !test_unknown_block_size_is_not_written()
        || !test_rejections_restore()
        || !test_activation_write_failure_restores()
        || !test_abort_then_close_restores()
        || !test_read_recovery_reapplies_live_state()
        || !test_persistent_restore_failure()) {
        return 1;
    }
    (void)puts("MT1887 PB00 source tests passed");
    return 0;
}
