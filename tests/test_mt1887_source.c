#include "gdox/optical.h"
#include "gdox/source.h"
#include "platform/mt1887_source.h"
#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t stock_capacity[3] = {0x03U, 0x1bU, 0x4fU};
static const uint8_t live_capacity[3] = {0x3dU, 0x4dU, 0x4fU};
static const uint8_t stock_geometry[3] = {0x03U, 0x1aU, 0xafU};
static const uint8_t live_geometry[3] = {0x20U, 0x33U, 0xafU};
static const uint8_t xgd2_wave1_stock_capacity[3] = {0x03U, 0x0dU, 0xbeU};
static const uint8_t xgd2_wave1_live_capacity[3] = {0x3dU, 0x5fU, 0xdeU};
static const uint8_t xgd2_wave1_stock_geometry[3] = {0x03U, 0x0aU, 0x8fU};
static const uint8_t xgd2_wave1_live_geometry[3] = {0x20U, 0x33U, 0x9fU};
static const uint8_t xgd2_wave2_stock_capacity[3] = {0x03U, 0x0aU, 0xa3U};
static const uint8_t xgd2_wave2_live_capacity[3] = {0x3dU, 0x61U, 0x03U};
static const uint8_t xgd2_wave2_stock_geometry[3] = {0x03U, 0x08U, 0x6fU};
static const uint8_t xgd2_wave2_live_geometry[3] = {0x20U, 0x33U, 0x9fU};
static const uint8_t xgd3_stock_capacity[3] = {0x03U, 0x61U, 0xe6U};
static const uint8_t xgd3_live_capacity[3] = {0x44U, 0x1cU, 0x06U};
static const uint8_t xgd3_stock_geometry[3] = {0x03U, 0x30U, 0xffU};
static const uint8_t xgd3_live_geometry[3] = {0x23U, 0x8eU, 0x0fU};
static const uint8_t canonical_auxiliary[3] = {0x64U, 0x00U, 0x64U};
static const uint8_t xdvdfs_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

typedef struct fake_write {
    uint16_t address;
    uint8_t value;
} fake_write;

typedef enum fake_media_kind {
    FAKE_MEDIA_XGD1 = 0,
    FAKE_MEDIA_XGD2_WAVE1,
    FAKE_MEDIA_XGD2_WAVE2,
    FAKE_MEDIA_XGD3,
} fake_media_kind;

typedef struct fake_mt1887 {
    uint8_t capacity[3];
    uint8_t geometry[3];
    uint8_t auxiliary[3];
    char revision[5];
    fake_write writes[64];
    size_t write_count;
    uint32_t read_lbas[8];
    uint32_t read_blocks[8];
    size_t read_count;
    uint32_t read_attempt_lbas[16];
    uint32_t read_attempt_blocks[16];
    size_t read_attempt_count;
    size_t read_command_count;
    uint32_t maximum_read_timeout_ms;
    uint32_t maximum_accepted_read_blocks;
    bool descriptor_valid;
    bool descriptor_trailing_valid;
    bool pfi_valid;
    bool fail_stock_capacity;
    bool fail_read_once;
    bool reset_to_stock;
    bool fail_prepare_close;
    uint16_t fail_write_address_once;
    uint32_t forced_last_lba;
    uint32_t forced_block_size;
    bool invalid_live_last_lba;
    bool gp63;
    unsigned int open_count;
    unsigned int close_count;
    unsigned int prepare_close_count;
    unsigned int reset_count;
    unsigned int load_start_count;
    fake_media_kind media;
    bool eject_requested;
    bool eject_after_reset;
    bool eject_on_next_ready;
    bool closed;
} fake_mt1887;

static const uint8_t *fake_stock_capacity(const fake_mt1887 *fake)
{
    if (fake->media == FAKE_MEDIA_XGD2_WAVE1) {
        return xgd2_wave1_stock_capacity;
    }
    if (fake->media == FAKE_MEDIA_XGD2_WAVE2) {
        return xgd2_wave2_stock_capacity;
    }
    return fake->media == FAKE_MEDIA_XGD3
        ? xgd3_stock_capacity
        : stock_capacity;
}

static const uint8_t *fake_live_capacity(const fake_mt1887 *fake)
{
    if (fake->media == FAKE_MEDIA_XGD2_WAVE1) {
        return xgd2_wave1_live_capacity;
    }
    if (fake->media == FAKE_MEDIA_XGD2_WAVE2) {
        return xgd2_wave2_live_capacity;
    }
    return fake->media == FAKE_MEDIA_XGD3
        ? xgd3_live_capacity
        : live_capacity;
}

static const uint8_t *fake_stock_geometry(const fake_mt1887 *fake)
{
    if (fake->media == FAKE_MEDIA_XGD2_WAVE1) {
        return xgd2_wave1_stock_geometry;
    }
    if (fake->media == FAKE_MEDIA_XGD2_WAVE2) {
        return xgd2_wave2_stock_geometry;
    }
    return fake->media == FAKE_MEDIA_XGD3
        ? xgd3_stock_geometry
        : stock_geometry;
}

static const uint8_t *fake_live_geometry(const fake_mt1887 *fake)
{
    if (fake->media == FAKE_MEDIA_XGD2_WAVE1) {
        return xgd2_wave1_live_geometry;
    }
    if (fake->media == FAKE_MEDIA_XGD2_WAVE2) {
        return xgd2_wave2_live_geometry;
    }
    return fake->media == FAKE_MEDIA_XGD3
        ? xgd3_live_geometry
        : live_geometry;
}

static bool fake_media_is_xgd2(const fake_mt1887 *fake)
{
    return fake->media == FAKE_MEDIA_XGD2_WAVE1
        || fake->media == FAKE_MEDIA_XGD2_WAVE2;
}

static uint32_t fake_stock_last_lba(const fake_mt1887 *fake)
{
    if (fake->media == FAKE_MEDIA_XGD2_WAVE1) {
        return UINT32_C(0x0dbe);
    }
    if (fake->media == FAKE_MEDIA_XGD2_WAVE2) {
        return UINT32_C(0x0aa3);
    }
    return fake->media == FAKE_MEDIA_XGD3
        ? UINT32_C(0x61e6)
        : UINT32_C(6991);
}

static uint32_t fake_live_last_lba(const fake_mt1887 *fake)
{
    if (fake->media == FAKE_MEDIA_XGD2_WAVE1) {
        return UINT32_C(0x3a5fde);
    }
    if (fake->media == FAKE_MEDIA_XGD2_WAVE2) {
        return UINT32_C(0x3a6103);
    }
    return fake->media == FAKE_MEDIA_XGD3
        ? UINT32_C(0x411c06)
        : UINT32_C(3820879);
}

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
    if (fake->gp63
        && address >= 0x8538U && address <= 0x853aU) {
        return &fake->capacity[address - 0x8538U];
    }
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
    return memcmp(
            fake->capacity,
            fake_live_capacity(fake),
            3U
        ) == 0
        && memcmp(
            fake->geometry,
            fake_live_geometry(fake),
            3U
        ) == 0;
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
    memset(output, 0, output_bytes);
    if (cdb[0] == 0x12U && output_bytes >= 36U) {
        memcpy(output + 8U, "HL-DT-ST", 8U);
        memcpy(
            output + 16U,
            fake->gp63
                ? "DVDRAM GP63EX70"
                : "DVDRAM GP65NB60",
            16U
        );
        memcpy(output + 32U, fake->revision, 4U);
    } else if (cdb[0] == 0x03U && output_bytes == 18U) {
        output[0] = 0x70U;
    } else if (cdb[0] == 0x4aU && output_bytes == 8U) {
        const bool eject_requested = fake->eject_requested;

        output[0] = 0U;
        output[1] = 6U;
        output[2] = eject_requested ? 0x04U : 0x84U;
        output[4] = eject_requested ? 0x01U : 0U;
        fake->eject_requested = false;
    } else if (cdb[0] == 0xadU && output_bytes == 2052U) {
        if (cdb[7] == 0U && fake->pfi_valid) {
            memcpy(output + 17U, fake_stock_geometry(fake), 3U);
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
            fake->invalid_live_last_lba && fake_is_live(fake)
                ? UINT32_C(123456)
                : fake->forced_last_lba != 0U
                    && memcmp(
                        fake->capacity,
                        fake_stock_capacity(fake),
                        3U
                    ) != 0
                    && !fake_is_live(fake)
                ? fake->forced_last_lba
                : fake_is_live(fake)
                    ? fake_live_last_lba(fake)
                    : fake_stock_last_lba(fake)
        );
        put_be_u32(
            output + 4U,
            fake->forced_block_size != 0U
                ? fake->forced_block_size
                : GDOX_LOGICAL_SECTOR_BYTES
        );
    } else if (cdb[0] == 0xa8U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        const uint32_t blocks = read_be_u32(cdb + 6U);

        ++fake->read_command_count;
        if (fake->read_attempt_count < 16U) {
            fake->read_attempt_lbas[fake->read_attempt_count] = lba;
            fake->read_attempt_blocks[fake->read_attempt_count++] = blocks;
        }
        if (timeout_ms > fake->maximum_read_timeout_ms) {
            fake->maximum_read_timeout_ms = timeout_ms;
        }
        if (fake->fail_read_once) {
            fake->fail_read_once = false;
            gdox_error_set(error, GDOX_ERROR_TRANSPORT,
                "injected sector read failure");
            return false;
        }
        if (fake->maximum_accepted_read_blocks != 0U
            && blocks > fake->maximum_accepted_read_blocks) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "injected transfer-size rejection"
            );
            return false;
        }
        if (fake->read_count < 8U) {
            fake->read_lbas[fake->read_count] = lba;
            fake->read_blocks[fake->read_count++] = blocks;
        }
        const uint32_t descriptor_lba =
            fake_media_is_xgd2(fake)
                ? UINT32_C(0x1fb40)
                : fake->media == FAKE_MEDIA_XGD3
                    ? UINT32_C(0x4120)
                    : UINT32_C(198176);
        if (lba == descriptor_lba
            && fake->descriptor_valid) {
            memcpy(output, xdvdfs_magic, sizeof(xdvdfs_magic));
            if (fake->descriptor_trailing_valid) {
                memcpy(
                    output + output_bytes - sizeof(xdvdfs_magic),
                    xdvdfs_magic,
                    sizeof(xdvdfs_magic)
                );
            }
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
        if (cdb[0] == 0U && fake->eject_on_next_ready) {
            fake->eject_on_next_ready = false;
            fake->eject_requested = true;
        }
        if (cdb[0] == 0x1bU && cdb[4] == 0x03U) {
            ++fake->load_start_count;
        }
        gdox_error_clear(error);
        return true;
    }
    if (cdb[0] == 0xf1U && cdb[1] == 0x01U) {
        const uint16_t address =
            (uint16_t)((uint16_t)cdb[4] << 8U | cdb[5]);
        const uint16_t capacity_address =
            fake->gp63 ? 0x8538U : 0x8a37U;
        const uint8_t *expected_stock = fake_stock_capacity(fake);
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
                && address >= capacity_address
                && address <= capacity_address + 2U
                && cdb[9] == expected_stock[address - capacity_address])) {
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
    if (fake->eject_after_reset) {
        fake->eject_after_reset = false;
        fake->eject_requested = true;
    }
    if (fake->reset_to_stock) {
        memcpy(
            fake->capacity,
            fake_stock_capacity(fake),
            3U
        );
        memcpy(
            fake->geometry,
            fake_stock_geometry(fake),
            3U
        );
        memcpy(fake->auxiliary, canonical_auxiliary, 3U);
    }
    gdox_error_clear(error);
    return true;
}

static bool fake_close(void *raw_context, gdox_error *error)
{
    fake_mt1887 *fake = raw_context;
    ++fake->close_count;
    fake->closed = true;
    gdox_error_clear(error);
    return true;
}

static bool fake_prepare_close(void *raw_context, gdox_error *error)
{
    fake_mt1887 *fake = raw_context;

    ++fake->prepare_close_count;
    if (fake->fail_prepare_close) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "injected transport close preparation failure"
        );
        return false;
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
    fake_mt1887 *fake = raw_context;

    ++fake->open_count;
    fake->closed = false;
    transport->context = raw_context;
    transport->ops = &fake_ops;
    gdox_error_clear(error);
    return true;
}

static bool open_detected_profile(
    gdox_mt1887_media_kind expected,
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_error close_error;

    if (!gdox_mt1887_detected_source_open(
            opener,
            opener_context,
            read_speed_kbps,
            read_retries,
            ready_timeout_ms,
            source,
            &selected,
            error
        )) {
        return false;
    }
    if (selected != NULL && selected->kind == expected) {
        return true;
    }
    if (!gdox_source_close(source, &close_error)) {
        *error = close_error;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "detected MT1887 media profile did not match the test fixture"
        );
    }
    return false;
}

static bool open_detected_xgd2(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return open_detected_profile(
        GDOX_MT1887_MEDIA_GP63_XGD2,
        opener,
        opener_context,
        read_speed_kbps,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

static bool open_detected_xgd3(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return open_detected_profile(
        GDOX_MT1887_MEDIA_GP63_XGD3,
        opener,
        opener_context,
        read_speed_kbps,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

static fake_mt1887 fake_stock(void)
{
    fake_mt1887 fake = {0};

    memcpy(fake.capacity, stock_capacity, 3U);
    memcpy(fake.geometry, stock_geometry, 3U);
    memcpy(fake.auxiliary, canonical_auxiliary, 3U);
    memcpy(fake.revision, "PB00", 5U);
    fake.descriptor_valid = true;
    fake.descriptor_trailing_valid = true;
    fake.pfi_valid = true;
    return fake;
}

static fake_mt1887 fake_xgd3_stock(void)
{
    fake_mt1887 fake = {0};

    memcpy(fake.capacity, xgd3_stock_capacity, 3U);
    memcpy(fake.geometry, xgd3_stock_geometry, 3U);
    memcpy(fake.revision, "RF02", 5U);
    fake.descriptor_valid = true;
    fake.descriptor_trailing_valid = true;
    fake.pfi_valid = true;
    fake.media = FAKE_MEDIA_XGD3;
    fake.gp63 = true;
    return fake;
}

static fake_mt1887 fake_xgd2_wave1_stock(void)
{
    fake_mt1887 fake = {0};

    memcpy(fake.capacity, xgd2_wave1_stock_capacity, 3U);
    memcpy(fake.geometry, xgd2_wave1_stock_geometry, 3U);
    memcpy(fake.revision, "RF02", 5U);
    fake.descriptor_valid = true;
    fake.descriptor_trailing_valid = true;
    fake.pfi_valid = true;
    fake.media = FAKE_MEDIA_XGD2_WAVE1;
    fake.gp63 = true;
    return fake;
}

static fake_mt1887 fake_xgd2_wave2_stock(void)
{
    fake_mt1887 fake = {0};

    memcpy(fake.capacity, xgd2_wave2_stock_capacity, 3U);
    memcpy(fake.geometry, xgd2_wave2_stock_geometry, 3U);
    memcpy(fake.revision, "RF02", 5U);
    fake.descriptor_valid = true;
    fake.descriptor_trailing_valid = true;
    fake.pfi_valid = true;
    fake.media = FAKE_MEDIA_XGD2_WAVE2;
    fake.gp63 = true;
    return fake;
}

static fake_mt1887 fake_gp63_xgd1_stock(void)
{
    fake_mt1887 fake = fake_stock();

    memcpy(fake.revision, "RF02", 5U);
    fake.gp63 = true;
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
    fake.maximum_read_timeout_ms = 0U;
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
        || !check(fake.maximum_read_timeout_ms > 0U
                && fake.maximum_read_timeout_ms <= UINT32_C(20000),
            "source read command is bounded by the recovery deadline")
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

static bool test_gp63_read_batching(void)
{
    const uint32_t blocks = UINT32_C(65);
    const size_t output_bytes =
        (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
    fake_mt1887 fake = fake_gp63_xgd1_stock();
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t *output = malloc(output_bytes);
    bool passed;

    if (!check(output != NULL, "allocate GP63 batching output")) {
        return false;
    }
    passed = check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP63,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "GP63 batching source opens");
    if (passed) {
        fake.read_count = 0U;
        passed = check(gdox_source_read(
                &source,
                0U,
                blocks,
                output,
                output_bytes,
                &error
            ), "GP63 source reads 65 sectors")
#if defined(_WIN32)
            && check(fake.read_count == 3U,
                "Windows GP63 read uses three transfers")
            && check(fake.read_lbas[0] == 0U
                    && fake.read_blocks[0] == 32U
                    && fake.read_lbas[1] == 32U
                    && fake.read_blocks[1] == 32U
                    && fake.read_lbas[2] == 64U
                    && fake.read_blocks[2] == 1U,
                "Windows GP63 uses exact bounded 64 KiB batches")
#else
            && check(fake.read_count == 1U,
                "portable GP63 read remains one transfer")
            && check(fake.read_lbas[0] == 0U
                    && fake.read_blocks[0] == 65U,
                "portable GP63 keeps the requested batch")
#endif
            ;
    }
    if (gdox_source_is_valid(&source)) {
        passed = check(gdox_source_close(&source, &error),
                "GP63 batching source closes")
            && passed;
    }
    free(output);
    return passed;
}

static bool test_read_batch_bisection(void)
{
    const uint32_t blocks = UINT32_C(17);
    fake_mt1887 fake = fake_gp63_xgd1_stock();
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[17U * GDOX_LOGICAL_SECTOR_BYTES];

    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP63,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "batch-bisection source opens")) {
        return false;
    }
    fake.maximum_accepted_read_blocks = 8U;
    fake.read_count = 0U;
    fake.read_command_count = 0U;
    fake.read_attempt_count = 0U;
    if (!check(gdox_source_read(
            &source,
            0U,
            blocks,
            output,
            sizeof(output),
            &error
        ), "rejected batch is bisected")
        || !check(fake.read_command_count == 5U,
            "bisection issues only bounded probe and batch commands")
        || !check(fake.read_attempt_count == 5U
                && fake.read_attempt_lbas[0] == 0U
                && fake.read_attempt_blocks[0] == 17U
                && fake.read_attempt_lbas[1] == 0U
                && fake.read_attempt_blocks[1] == 8U
                && fake.read_attempt_lbas[2] == 8U
                && fake.read_attempt_blocks[2] == 9U
                && fake.read_attempt_lbas[3] == 8U
                && fake.read_attempt_blocks[3] == 4U
                && fake.read_attempt_lbas[4] == 12U
                && fake.read_attempt_blocks[4] == 5U,
            "bisection attempts the exact bounded range partition")
        || !check(fake.read_count == 3U,
            "bisection retains three successful batches")
        || !check(fake.read_lbas[0] == 0U
                && fake.read_blocks[0] == 8U
                && fake.read_lbas[1] == 8U
                && fake.read_blocks[1] == 4U
                && fake.read_lbas[2] == 12U
                && fake.read_blocks[2] == 5U,
            "bisection covers the requested range exactly")
        || !check(gdox_source_close(&source, &error),
            "batch-bisection source closes")) {
        gdox_source_destroy(&source);
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
    fake.read_command_count = 0U;
    fake.maximum_read_timeout_ms = 0U;
    if (!check(gdox_source_read(
            &source,
            0U,
            1U,
            output,
            sizeof(output),
            &error
        ), "read recovery reapplies live state")
        || !check(fake.read_command_count == 2U,
            "read recovery owns exactly one retry")
        || !check(fake.maximum_read_timeout_ms > 0U
                && fake.maximum_read_timeout_ms <= UINT32_C(20000),
            "read recovery commands remain inside one deadline")
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

static bool test_eject_request_suppresses_read_recovery_load(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_media_observation observation = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    unsigned int reset_count;
    unsigned int load_start_count;

    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            1U,
            0U,
            &source,
            &error
        ), "eject recovery source opens")) {
        return false;
    }
    reset_count = fake.reset_count;
    load_start_count = fake.load_start_count;
    fake.fail_read_once = true;
    fake.eject_requested = true;
    fake.read_command_count = 0U;
    if (!check(!gdox_source_read(
            &source,
            0U,
            1U,
            output,
            sizeof(output),
            &error
        ), "eject request stops the failed read")
        || !check(error.code == GDOX_ERROR_NOT_FOUND,
            "eject request reports media removal")
        || !check(fake.read_command_count == 1U,
            "eject request prevents a read retry")
        || !check(fake.reset_count == reset_count,
            "eject request prevents recovery reset")
        || !check(fake.load_start_count == load_start_count,
            "eject request prevents load/start")
        || !check(gdox_source_observe_media(&source, &observation),
            "eject request remains observable")
        || !check(observation.event == GDOX_MEDIA_EVENT_EJECT_REQUEST,
            "eject request remains sticky until close")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "eject recovery source closes");
}

static bool test_eject_transition_windows_stop_recovery(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];
    unsigned int reset_count;
    unsigned int load_start_count;

    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            1U,
            0U,
            &source,
            &error
        ), "transition-window source opens")) {
        return false;
    }
    reset_count = fake.reset_count;
    load_start_count = fake.load_start_count;
    fake.fail_read_once = true;
    fake.eject_after_reset = true;
    fake.read_command_count = 0U;
    if (!check(!gdox_source_read(
            &source,
            0U,
            1U,
            output,
            sizeof(output),
            &error
        ), "post-reset eject stops recovery")
        || !check(error.code == GDOX_ERROR_NOT_FOUND,
            "post-reset eject reports media transition")
        || !check(fake.reset_count == reset_count + 1U,
            "post-reset eject occurs after the guarded reset")
        || !check(fake.load_start_count == load_start_count,
            "post-reset eject prevents load/start")
        || !check(fake.read_command_count == 1U,
            "post-reset eject prevents read retry")) {
        gdox_source_destroy(&source);
        return false;
    }
    if (!check(gdox_source_close(&source, &error),
            "post-reset transition source closes")) {
        return false;
    }

    fake = fake_stock();
    source = (gdox_sector_source){0};
    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            1U,
            0U,
            &source,
            &error
        ), "post-ready transition source opens")) {
        return false;
    }
    load_start_count = fake.load_start_count;
    fake.fail_read_once = true;
    fake.eject_on_next_ready = true;
    fake.read_command_count = 0U;
    if (!check(!gdox_source_read(
            &source,
            0U,
            1U,
            output,
            sizeof(output),
            &error
        ), "post-ready eject stops recovery")
        || !check(error.code == GDOX_ERROR_NOT_FOUND,
            "post-ready eject reports media transition")
        || !check(fake.load_start_count == load_start_count + 1U,
            "post-ready transition follows the first guarded load")
        || !check(fake.read_command_count == 1U,
            "post-ready eject prevents read retry")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "post-ready transition source closes");
}

static bool test_session_baseline_discards_queued_eject(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_media_observation observation = {0};
    gdox_error error;

    fake.eject_requested = true;
    if (!check(gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            1U,
            0U,
            &source,
            &error
        ), "queued-eject source opens")) {
        return false;
    }
    if (!check(gdox_source_observe_media(&source, &observation),
            "queued-eject session is observable")
        || !check(observation.event == GDOX_MEDIA_EVENT_NONE,
            "pre-session eject is discarded")) {
        gdox_source_destroy(&source);
        return false;
    }
    fake.eject_requested = true;
    if (!check(gdox_source_observe_media(&source, &observation),
            "post-baseline eject is observable")
        || !check(observation.event == GDOX_MEDIA_EVENT_EJECT_REQUEST,
            "post-baseline eject remains actionable")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "queued-eject source closes");
}

static bool test_persistent_restore_failure(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.descriptor_valid = false;
    fake.fail_stock_capacity = true;
    if (!check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "persistent restore failure returned")
        || !check(strstr(error.message, "power-cycle the drive") != NULL,
            "persistent restore failure requires power cycle")
        || !check(fake.reset_count == 2U, "restore retried three times")
        || !check(fake.write_count == 33U, "every restore attempt writes all fields")
        || !check(gdox_source_is_valid(&source),
            "failed restoration retains cleanup source")
        || !check(!fake.closed,
            "failed restoration retains transport")) {
        return false;
    }
    fake.fail_stock_capacity = false;
    return check(gdox_source_close(&source, &error),
            "retained failed-open source closes after restoration recovers")
        && check(!gdox_source_is_valid(&source),
            "successful failed-open cleanup consumes source")
        && check(fake.closed,
            "successful failed-open cleanup closes transport");
}

static bool test_failed_open_transport_prepare_retry(void)
{
    fake_mt1887 fake = fake_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.descriptor_valid = false;
    fake.fail_prepare_close = true;
    if (!check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP65,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "failed-open transport preparation is returned")
        || !check(gdox_source_is_valid(&source),
            "failed-open transport preparation retains source")
        || !check(!fake.closed,
            "failed-open transport preparation retains transport")
        || !check(fake.prepare_close_count == 1U,
            "failed-open cleanup prepares transport once")) {
        return false;
    }
    fake.fail_prepare_close = false;
    return check(gdox_source_close(&source, &error),
            "failed-open transport close can be retried")
        && check(fake.closed,
            "failed-open transport retry closes transport")
        && check(fake.prepare_close_count == 3U,
            "failed-open transport retry is idempotent");
}

static bool test_close_prepare_retains_source(void)
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
        ), "close retention source opens")) {
        return false;
    }
    fake.fail_stock_capacity = true;
    if (!check(!gdox_source_close(&source, &error),
            "failed SRAM close preparation is returned")
        || !check(gdox_source_is_valid(&source),
            "failed SRAM close preparation retains source")
        || !check(!fake.closed,
            "failed SRAM close preparation retains transport")
        || !check(fake.prepare_close_count == 0U,
            "transport prepare waits for verified stock SRAM")) {
        return false;
    }

    fake.fail_stock_capacity = false;
    fake.fail_prepare_close = true;
    if (!check(!gdox_source_close(&source, &error),
            "failed transport close preparation is returned")
        || !check(gdox_source_is_valid(&source),
            "failed transport close preparation retains source")
        || !check(!fake.closed,
            "failed transport close preparation retains transport")
        || !check(memcmp(fake.capacity, stock_capacity, 3U) == 0,
            "transport preparation begins only after stock capacity verifies")
        || !check(memcmp(fake.geometry, stock_geometry, 3U) == 0,
            "transport preparation begins only after stock geometry verifies")) {
        return false;
    }

    fake.fail_prepare_close = false;
    return check(gdox_source_close(&source, &error),
            "retained source close can be retried")
        && check(!gdox_source_is_valid(&source),
            "successful retry consumes source")
        && check(fake.closed,
            "successful retry closes transport")
        && check(fake.prepare_close_count == 3U,
            "transport close preparation remains idempotent");
}

static bool test_detected_profiles_use_one_transport(void)
{
    fake_mt1887 xgd1 = fake_gp63_xgd1_stock();
    fake_mt1887 xgd2_wave1 = fake_xgd2_wave1_stock();
    fake_mt1887 xgd2_wave2 = fake_xgd2_wave2_stock();
    fake_mt1887 xgd3 = fake_xgd3_stock();
    gdox_sector_source source = {0};
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_error error;

    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd1,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detected XGD1 source opens")
        || !check(
            selected == gdox_mt1887_media_profile_xgd1(),
            "detector selects XGD1"
        )
        || !check(xgd1.open_count == 1U,
            "XGD1 detection opens one transport")
        || !check(gdox_source_sector_count(&source) == GDOX_XGD1_TOTAL_SECTORS,
            "detected XGD1 source has exact size")
        || !check(gdox_source_close(&source, &error),
            "detected XGD1 source closes")) {
        gdox_source_destroy(&source);
        return false;
    }

    selected = NULL;
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd2_wave1,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detected XGD2 Wave 1 source opens")
        || !check(
            selected == gdox_mt1887_media_profile_gp63_xgd2_wave1(),
            "detector selects XGD2 Wave 1"
        )
        || !check(xgd2_wave1.open_count == 1U,
            "XGD2 Wave 1 detection opens one transport")
        || !check(gdox_source_sector_count(&source) == UINT64_C(0x3a5fdf),
            "detected XGD2 Wave 1 source has exact size")
        || !check(gdox_source_close(&source, &error),
            "detected XGD2 Wave 1 source closes")) {
        gdox_source_destroy(&source);
        return false;
    }

    selected = NULL;
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd2_wave2,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detected XGD2 Wave 2 source opens")
        || !check(
            selected == gdox_mt1887_media_profile_gp63_xgd2_wave2(),
            "detector selects XGD2 Wave 2"
        )
        || !check(xgd2_wave2.open_count == 1U,
            "XGD2 Wave 2 detection opens one transport")
        || !check(gdox_source_sector_count(&source) == GDOX_XGD2_TOTAL_SECTORS,
            "detected XGD2 Wave 2 source has exact size")
        || !check(gdox_source_close(&source, &error),
            "detected XGD2 Wave 2 source closes")) {
        gdox_source_destroy(&source);
        return false;
    }

    selected = NULL;
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd3,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detected XGD3 source opens")
        || !check(
            selected == gdox_mt1887_media_profile_gp63_xgd3(),
            "detector selects XGD3"
        )
        || !check(xgd3.open_count == 1U,
            "XGD3 detection opens one transport")
        || !check(gdox_source_sector_count(&source) == UINT64_C(0x411c07),
            "detected XGD3 source has exact size")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "detected XGD3 source closes");
}

static bool test_detected_live_xgd3_startup_recovery(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_error error;

    memcpy(fake.capacity, xgd3_live_capacity, 3U);
    memcpy(fake.geometry, xgd3_live_geometry, 3U);
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detector recovers an orphaned live XGD3 state")
        || !check(selected == gdox_mt1887_media_profile_gp63_xgd3(),
            "orphan recovery retains XGD3 identity")
        || !check(fake.open_count == 1U,
            "orphan recovery never reopens the transport")
        || !check(fake.write_count == 12U,
            "orphan recovery restores then activates transactionally")
        || !check(writes_begin_at(&fake, 0U, 0x8be2U),
            "orphan recovery restores geometry first")
        || !check(writes_begin_at(&fake, 6U, 0x8538U),
            "orphan recovery activates selected capacity")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
            "recovered detected XGD3 source closes")
        && check(memcmp(fake.capacity, xgd3_stock_capacity, 3U) == 0,
            "recovered XGD3 capacity returns to stock")
        && check(memcmp(fake.geometry, xgd3_stock_geometry, 3U) == 0,
            "recovered XGD3 geometry returns to stock");
}

static bool test_detected_cross_profile_disc_swap(void)
{
    fake_mt1887 xgd1 = fake_gp63_xgd1_stock();
    fake_mt1887 xgd2 = fake_xgd2_wave2_stock();
    fake_mt1887 xgd3 = fake_xgd3_stock();
    gdox_sector_source source = {0};
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_error error;

    memcpy(xgd1.capacity, xgd3_stock_capacity, 3U);
    memcpy(xgd1.geometry, xgd3_stock_geometry, 3U);
    xgd1.forced_last_lba = UINT32_C(0x61e6);
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd1,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "XGD1 opens after an XGD3 volatile state was retained")
        || !check(
            selected == gdox_mt1887_media_profile_xgd1(),
            "physical XGD1 geometry overrides stale XGD3 state"
        )
        || !check(xgd1.write_count == 12U,
            "XGD3-to-XGD1 swap normalizes then activates")
        || !check(writes_begin_at(&xgd1, 0U, 0x8be2U),
            "XGD3-to-XGD1 swap restores physical-disc geometry first")
        || !check(writes_begin_at(&xgd1, 3U, 0x8538U),
            "XGD3-to-XGD1 swap restores physical-disc capacity second")
        || !check(writes_begin_at(&xgd1, 6U, 0x8538U),
            "XGD3-to-XGD1 swap activates XGD1 capacity")
        || !check(gdox_source_sector_count(&source)
                == GDOX_XGD1_TOTAL_SECTORS,
            "XGD3-to-XGD1 swap exports the exact XGD1 size")
        || !check(gdox_source_close(&source, &error),
            "XGD3-to-XGD1 swap restores on close")) {
        gdox_source_destroy(&source);
        return false;
    }

    memcpy(xgd2.capacity, xgd3_stock_capacity, 3U);
    memcpy(xgd2.geometry, xgd3_stock_geometry, 3U);
    xgd2.forced_last_lba = UINT32_C(0x61e6);
    selected = NULL;
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd2,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "XGD2 opens after an XGD3 volatile state was retained")
        || !check(
            selected == gdox_mt1887_media_profile_gp63_xgd2_wave2(),
            "physical XGD2 geometry overrides stale XGD3 state"
        )
        || !check(xgd2.write_count == 12U,
            "XGD3-to-XGD2 swap normalizes then activates")
        || !check(writes_begin_at(&xgd2, 0U, 0x8be2U),
            "XGD3-to-XGD2 swap restores physical-disc geometry first")
        || !check(writes_begin_at(&xgd2, 3U, 0x8538U),
            "XGD3-to-XGD2 swap restores physical-disc capacity second")
        || !check(writes_begin_at(&xgd2, 6U, 0x8538U),
            "XGD3-to-XGD2 swap activates XGD2 capacity")
        || !check(gdox_source_sector_count(&source)
                == GDOX_XGD2_TOTAL_SECTORS,
            "XGD3-to-XGD2 swap exports the exact XGD2 size")
        || !check(gdox_source_close(&source, &error),
            "XGD3-to-XGD2 swap restores on close")) {
        gdox_source_destroy(&source);
        return false;
    }

    memcpy(xgd3.capacity, xgd2_wave2_stock_capacity, 3U);
    memcpy(xgd3.geometry, xgd2_wave2_stock_geometry, 3U);
    xgd3.forced_last_lba = UINT32_C(0x0aa3);
    selected = NULL;
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &xgd3,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "XGD3 opens after an XGD2 volatile state was retained")
        || !check(
            selected == gdox_mt1887_media_profile_gp63_xgd3(),
            "physical XGD3 geometry overrides stale XGD2 state"
        )
        || !check(xgd3.write_count == 12U,
            "XGD2-to-XGD3 swap normalizes then activates")
        || !check(gdox_source_sector_count(&source) == UINT64_C(0x411c07),
            "XGD2-to-XGD3 swap exports the exact XGD3 size")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "XGD2-to-XGD3 swap restores on close");
}

static bool test_persistent_drive_xgd3_to_xgd1_lifecycle(void)
{
    fake_mt1887 drive = fake_xgd3_stock();
    gdox_sector_source xgd3_source = {0};
    gdox_sector_source xgd1_source = {0};
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_media_observation observation = {0};
    gdox_error error;

    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &drive,
            UINT16_C(0xffff),
            0U,
            0U,
            &xgd3_source,
            &selected,
            &error
        ), "persistent drive opens XGD3 session")
        || !check(selected == gdox_mt1887_media_profile_gp63_xgd3(),
            "persistent drive selects XGD3 first")
        || !check(drive.open_count == 1U && drive.close_count == 0U,
            "XGD3 session owns one open transport")
        || !check(gdox_source_sector_count(&xgd3_source)
                == UINT64_C(0x411c07),
            "XGD3 session exposes exact geometry")) {
        gdox_source_destroy(&xgd3_source);
        return false;
    }

    drive.eject_requested = true;
    if (!check(gdox_source_observe_media(&xgd3_source, &observation),
            "XGD3 session observes the eject request")
        || !check(observation.event == GDOX_MEDIA_EVENT_EJECT_REQUEST,
            "XGD3 eject request belongs to the old session")) {
        gdox_source_destroy(&xgd3_source);
        return false;
    }
    gdox_source_abort(&xgd3_source);
    if (!check(gdox_source_close(&xgd3_source, &error),
            "ejected XGD3 session restores and closes")
        || !check(!gdox_source_is_valid(&xgd3_source),
            "XGD3 close releases source ownership")
        || !check(drive.open_count == 1U && drive.close_count == 1U
                && drive.closed,
            "XGD3 close releases its transport endpoint")
        || !check(memcmp(drive.capacity, xgd3_stock_capacity, 3U) == 0
                && memcmp(drive.geometry, xgd3_stock_geometry, 3U) == 0,
            "XGD3 close verifies stock volatile state")) {
        return false;
    }

    drive.media = FAKE_MEDIA_XGD1;
    drive.forced_last_lba = UINT32_C(0x61e6);
    drive.read_count = 0U;
    drive.read_command_count = 0U;
    drive.eject_requested = true;
    selected = NULL;
    if (!check(gdox_mt1887_detected_source_open(
            fake_open,
            &drive,
            UINT16_C(0xffff),
            0U,
            0U,
            &xgd1_source,
            &selected,
            &error
        ), "persistent drive reopens as XGD1")
        || !check(selected == gdox_mt1887_media_profile_xgd1(),
            "replacement PFI selects XGD1")
        || !check(drive.open_count == 2U && drive.close_count == 1U
                && !drive.closed,
            "XGD1 session owns a fresh transport endpoint")
        || !check(drive.write_count == 24U,
            "XGD1 reopen normalizes stale XGD3 state then activates")
        || !check(writes_begin_at(&drive, 12U, 0x8be2U),
            "XGD1 reopen restores replacement geometry first")
        || !check(writes_begin_at(&drive, 15U, 0x8538U),
            "XGD1 reopen restores replacement capacity second")
        || !check(writes_begin_at(&drive, 18U, 0x8538U),
            "XGD1 reopen activates replacement capacity")
        || !check(writes_begin_at(&drive, 21U, 0x8be2U),
            "XGD1 reopen activates replacement geometry")
        || !check(drive.read_count >= 1U
                && drive.read_lbas[0] == UINT32_C(0x30620)
                && drive.read_blocks[0] == 1U,
            "XGD1 reopen validates its exact live descriptor sector")
        || !check(gdox_source_sector_count(&xgd1_source)
                == GDOX_XGD1_TOTAL_SECTORS,
            "XGD1 session exposes exact export geometry")
        || !check(gdox_source_observe_media(&xgd1_source, &observation),
            "fresh XGD1 session is observable")
        || !check(observation.event == GDOX_MEDIA_EVENT_NONE,
            "fresh XGD1 session discards the queued old eject event")) {
        gdox_source_destroy(&xgd1_source);
        return false;
    }
    return check(gdox_source_close(&xgd1_source, &error),
            "persistent XGD1 session restores and closes")
        && check(drive.open_count == 2U && drive.close_count == 2U
                && drive.closed,
            "persistent lifecycle closes both transport endpoints")
        && check(memcmp(drive.capacity, stock_capacity, 3U) == 0
                && memcmp(drive.geometry, stock_geometry, 3U) == 0,
            "persistent lifecycle leaves XGD1 stock state");
}

static bool test_detected_failure_rolls_back_and_closes(void)
{
    fake_mt1887 fake = fake_xgd2_wave1_stock();
    gdox_sector_source source = {0};
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_error error;

    fake.descriptor_trailing_valid = false;
    return check(!gdox_mt1887_detected_source_open(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detected descriptor failure is returned")
        && check(fake.open_count == 1U,
            "failed detected session uses one transport")
        && check(fake.write_count == 12U,
            "failed detected session runs complete rollback")
        && check(memcmp(
                fake.capacity, xgd2_wave1_stock_capacity, 3U
            ) == 0,
            "failed detected session restores capacity")
        && check(memcmp(
                fake.geometry, xgd2_wave1_stock_geometry, 3U
            ) == 0,
            "failed detected session restores geometry")
        && check(fake.closed,
            "failed detected session closes transport")
        && check(!gdox_source_is_valid(&source),
            "failed detected session returns no source");
}

static bool test_detected_unknown_state_is_write_free(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    const gdox_mt1887_media_profile *selected = NULL;
    gdox_error error;

    fake.capacity[1] = stock_capacity[1];
    return check(!gdox_mt1887_detected_source_open(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &selected,
            &error
        ), "detector rejects mixed unknown state")
        && check(fake.open_count == 1U,
            "unknown state uses one transport")
        && check(fake.write_count == 0U,
            "unknown state performs no volatile writes")
        && check(fake.closed,
            "unknown state closes its transport")
        && check(selected == NULL,
            "unknown state returns no profile")
        && check(!gdox_source_is_valid(&source),
            "unknown state returns no source");
}

static bool test_xgd3_activation_and_restore(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    if (!check(open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "RF02 XGD3 source opens")
        || !check(
            gdox_source_sector_count(&source) == UINT64_C(0x411c07),
            "XGD3 source has the exact sector count"
        )
        || !check(fake.write_count == 6U, "XGD3 activation writes six bytes")
        || !check(writes_begin_at(&fake, 0U, 0x8538U),
            "XGD3 capacity activates first")
        || !check(writes_begin_at(&fake, 3U, 0x8be2U),
            "XGD3 geometry activates second")
        || !check(memcmp(fake.capacity, xgd3_live_capacity, 3U) == 0,
            "XGD3 capacity is live")
        || !check(memcmp(fake.geometry, xgd3_live_geometry, 3U) == 0,
            "XGD3 geometry is live")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error), "XGD3 source closes")
        && check(fake.write_count == 12U, "XGD3 close writes full restore")
        && check(writes_begin_at(&fake, 6U, 0x8be2U),
            "XGD3 geometry restores first")
        && check(writes_begin_at(&fake, 9U, 0x8538U),
            "XGD3 capacity restores second")
        && check(memcmp(fake.capacity, xgd3_stock_capacity, 3U) == 0,
            "XGD3 capacity restored")
        && check(memcmp(fake.geometry, xgd3_stock_geometry, 3U) == 0,
            "XGD3 geometry restored");
}

static bool check_xgd2_activation_and_restore(
    fake_mt1887 fake,
    uint64_t expected_sectors
)
{
    gdox_sector_source source = {0};
    gdox_error error;
    const uint8_t *expected_stock_capacity = fake_stock_capacity(&fake);
    const uint8_t *expected_live_capacity = fake_live_capacity(&fake);
    const uint8_t *expected_stock_geometry = fake_stock_geometry(&fake);
    const uint8_t *expected_live_geometry = fake_live_geometry(&fake);

    if (!check(open_detected_xgd2(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "RF02 XGD2 source opens")
        || !check(
            gdox_source_sector_count(&source) == expected_sectors,
            "XGD2 source has the exact sector count"
        )
        || !check(fake.write_count == 6U, "XGD2 activation writes six bytes")
        || !check(writes_begin_at(&fake, 0U, 0x8538U),
            "XGD2 capacity activates first")
        || !check(writes_begin_at(&fake, 3U, 0x8be2U),
            "XGD2 geometry activates second")
        || !check(memcmp(fake.capacity, expected_live_capacity, 3U) == 0,
            "XGD2 capacity is live")
        || !check(memcmp(fake.geometry, expected_live_geometry, 3U) == 0,
            "XGD2 geometry is live")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error), "XGD2 source closes")
        && check(fake.write_count == 12U, "XGD2 close writes full restore")
        && check(writes_begin_at(&fake, 6U, 0x8be2U),
            "XGD2 geometry restores first")
        && check(writes_begin_at(&fake, 9U, 0x8538U),
            "XGD2 capacity restores second")
        && check(memcmp(fake.capacity, expected_stock_capacity, 3U) == 0,
            "XGD2 capacity restored")
        && check(memcmp(fake.geometry, expected_stock_geometry, 3U) == 0,
            "XGD2 geometry restored");
}

static bool test_xgd2_activation_and_restore(void)
{
    return check_xgd2_activation_and_restore(
            fake_xgd2_wave1_stock(), UINT64_C(0x3a5fdf)
        )
        && check_xgd2_activation_and_restore(
            fake_xgd2_wave2_stock(), GDOX_XGD2_TOTAL_SECTORS
        );
}

static bool test_xgd1_handoff_to_detector_is_write_free(void)
{
    fake_mt1887 fake = fake_xgd2_wave2_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    return check(!gdox_mt1887_source_open(
            fake_open,
            &fake,
            GDOX_USB_BOT_GP63,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "XGD1 opener hands known XGD2 to the profile selector")
        && check(error.code == GDOX_ERROR_INVALID_SOURCE,
            "known XGD2 mismatch is a media mismatch")
        && check(fake.write_count == 0U,
            "known XGD2 mismatch performs no volatile writes");
}

static bool test_xgd3_exact_identity_and_state_gate(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    memcpy(fake.revision, "RF03", 5U);
    if (!check(!open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "non-RF02 GP63 is rejected")
        || !check(error.code == GDOX_ERROR_UNSUPPORTED,
            "non-RF02 rejection is explicit")
        || !check(fake.write_count == 0U,
            "non-RF02 drive is never written")) {
        return false;
    }

    fake = fake_xgd3_stock();
    fake.capacity[1] = stock_capacity[1];
    return check(!open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "mixed unknown XGD3 state is rejected")
        && check(error.code == GDOX_ERROR_TRANSPORT,
            "mixed unknown state fails closed")
        && check(fake.write_count == 0U,
            "mixed unknown state is never written");
}

static bool test_xgd3_each_activation_failure_restores(void)
{
    static const uint16_t addresses[] = {
        0x8538U, 0x8539U, 0x853aU, 0x8be2U, 0x8be3U, 0x8be4U,
    };
    size_t index;

    for (index = 0U; index < sizeof(addresses) / sizeof(addresses[0]);
         ++index) {
        fake_mt1887 fake = fake_xgd3_stock();
        gdox_sector_source source = {0};
        gdox_error error;

        fake.fail_write_address_once = addresses[index];
        if (!check(!open_detected_xgd3(
                fake_open,
                &fake,
                UINT16_C(0xffff),
                0U,
                0U,
                &source,
                &error
            ), "each XGD3 activation failure is returned")
            || !check(strstr(
                error.message,
                "injected volatile write failure"
            ) != NULL, "XGD3 activation error is preserved")
            || !check(fake.write_count == index + 7U,
                "XGD3 activation failure runs six-byte rollback")
            || !check(memcmp(
                fake.capacity,
                xgd3_stock_capacity,
                3U
            ) == 0, "XGD3 failed activation restores capacity")
            || !check(memcmp(
                fake.geometry,
                xgd3_stock_geometry,
                3U
            ) == 0, "XGD3 failed activation restores geometry")) {
            return false;
        }
    }
    return true;
}

static bool test_xgd3_probe_failures_restore(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    fake.descriptor_trailing_valid = false;
    if (!check(!open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "incomplete XGD3 descriptor is rejected")
        || !check(fake.write_count == 12U,
            "XGD3 descriptor failure runs full restore")
        || !check(memcmp(fake.capacity, xgd3_stock_capacity, 3U) == 0,
            "descriptor failure restores XGD3 capacity")
        || !check(memcmp(fake.geometry, xgd3_stock_geometry, 3U) == 0,
            "descriptor failure restores XGD3 geometry")) {
        return false;
    }

    fake = fake_xgd3_stock();
    fake.invalid_live_last_lba = true;
    if (!check(!open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "invalid live XGD3 capacity is rejected")
        || !check(fake.write_count == 12U,
            "live-state verification failure runs full restore")
        || !check(memcmp(fake.capacity, xgd3_stock_capacity, 3U) == 0,
            "live-state verification restores XGD3 capacity")
        || !check(memcmp(fake.geometry, xgd3_stock_geometry, 3U) == 0,
            "live-state verification restores XGD3 geometry")) {
        return false;
    }

    fake = fake_xgd3_stock();
    memcpy(fake.capacity, xgd3_live_capacity, 3U);
    memcpy(fake.geometry, xgd3_live_geometry, 3U);
    fake.pfi_valid = false;
    return check(!open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "PFI failure is rejected")
        && check(fake.write_count == 0U,
            "invalid PFI is rejected before volatile writes")
        && check(memcmp(fake.capacity, xgd3_live_capacity, 3U) == 0,
            "invalid PFI leaves the prior capacity untouched")
        && check(memcmp(fake.geometry, xgd3_live_geometry, 3U) == 0,
            "invalid PFI leaves the prior geometry untouched");
}

static bool test_xgd3_known_live_state_recovers(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    gdox_error error;

    memcpy(fake.capacity, xgd3_live_capacity, 3U);
    memcpy(fake.geometry, xgd3_live_geometry, 3U);
    if (!check(open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            0U,
            0U,
            &source,
            &error
        ), "known live XGD3 state recovers")
        || !check(writes_begin_at(&fake, 0U, 0x8be2U),
            "known live XGD3 restores geometry first")
        || !check(writes_begin_at(&fake, 3U, 0x8538U),
            "known live XGD3 restores capacity second")
        || !check(writes_begin_at(&fake, 6U, 0x8538U),
            "known live XGD3 reapplies capacity")
        || !check(writes_begin_at(&fake, 9U, 0x8be2U),
            "known live XGD3 reapplies geometry")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "recovered XGD3 source closes");
}

static bool test_xgd3_read_recovery_reapplies_selected_profile(void)
{
    fake_mt1887 fake = fake_xgd3_stock();
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];

    if (!check(open_detected_xgd3(
            fake_open,
            &fake,
            UINT16_C(0xffff),
            1U,
            0U,
            &source,
            &error
        ), "XGD3 recovery source opens")) {
        return false;
    }
    fake.fail_read_once = true;
    fake.reset_to_stock = true;
    fake.read_command_count = 0U;
    fake.maximum_read_timeout_ms = 0U;
    if (!check(gdox_source_read(
            &source,
            0U,
            1U,
            output,
            sizeof(output),
            &error
        ), "XGD3 read recovery succeeds")
        || !check(fake.read_command_count == 2U,
            "XGD3 source owns exactly one read retry")
        || !check(fake.maximum_read_timeout_ms > 0U
                && fake.maximum_read_timeout_ms <= UINT32_C(20000),
            "XGD3 recovery commands remain inside one deadline")
        || !check(fake.reset_count >= 1U,
            "XGD3 read recovery resets transport")
        || !check(fake.write_count == 12U,
            "XGD3 read recovery reapplies six bytes")
        || !check(writes_begin_at(&fake, 6U, 0x8538U),
            "XGD3 recovery reapplies its capacity addresses")
        || !check(writes_begin_at(&fake, 9U, 0x8be2U),
            "XGD3 recovery reapplies its geometry addresses")
        || !check(fake_is_live(&fake),
            "XGD3 recovery leaves the selected profile live")) {
        gdox_source_destroy(&source);
        return false;
    }
    return check(gdox_source_close(&source, &error),
        "XGD3 read-recovery source restores on close");
}

int main(void)
{
    if (!test_activation_and_restore()
        || !test_gp63_read_batching()
        || !test_read_batch_bisection()
        || !test_auxiliary_recovery()
        || !test_intermediate_capacity_recovery()
        || !test_unknown_block_size_is_not_written()
        || !test_rejections_restore()
        || !test_activation_write_failure_restores()
        || !test_abort_then_close_restores()
        || !test_read_recovery_reapplies_live_state()
        || !test_eject_request_suppresses_read_recovery_load()
        || !test_eject_transition_windows_stop_recovery()
        || !test_session_baseline_discards_queued_eject()
        || !test_persistent_restore_failure()
        || !test_failed_open_transport_prepare_retry()
        || !test_close_prepare_retains_source()
        || !test_detected_profiles_use_one_transport()
        || !test_detected_live_xgd3_startup_recovery()
        || !test_detected_cross_profile_disc_swap()
        || !test_persistent_drive_xgd3_to_xgd1_lifecycle()
        || !test_detected_failure_rolls_back_and_closes()
        || !test_detected_unknown_state_is_write_free()
        || !test_xgd2_activation_and_restore()
        || !test_xgd1_handoff_to_detector_is_write_free()
        || !test_xgd3_activation_and_restore()
        || !test_xgd3_exact_identity_and_state_gate()
        || !test_xgd3_each_activation_failure_restores()
        || !test_xgd3_probe_failures_restore()
        || !test_xgd3_known_live_state_recovers()
        || !test_xgd3_read_recovery_reapplies_selected_profile()) {
        return 1;
    }
    (void)puts("MT1887 source tests passed");
    return 0;
}
