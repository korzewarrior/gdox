#define _POSIX_C_SOURCE 200809L

#include "platform/ad7701h_source.h"
#include "gdox/hash.h"
#include "gdox/optical.h"
#include "platform/mmc_commands.h"
#include "platform/optical_driver.h"
#include "platform/portable_sync.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND_TIMEOUT UINT32_C(10000)
#define GEOMETRY_ADDRESS UINT32_C(0x03ff8688)
#define MODE_ADDRESS UINT32_C(0x03ff9790)
#define MAX_BLOCKS UINT32_C(16)

static const uint8_t stock_geometry[4] = {0xafU, 0x1aU, 0x03U, 0U};
static const uint8_t game_geometry[4] = {0xafU, 0x33U, 0x20U, 0U};
static const uint8_t stock_pfi[20] = {
    0x08U, 0x02U, 0U, 0U, 0x01U, 0x02U, 0x31U, 0x10U,
    0U, 0x03U, 0U, 0U, 0U, 0xfcU, 0xe5U, 0xefU,
    0U, 0x03U, 0x1aU, 0xafU,
};
static const uint8_t firmware_sha256[32] = {
    0xc3U, 0x63U, 0x7cU, 0xfbU, 0xdcU, 0x5fU, 0x2fU, 0x69U,
    0xf8U, 0x9aU, 0x35U, 0xdcU, 0xddU, 0xaeU, 0x67U, 0x99U,
    0x96U, 0xe8U, 0xf4U, 0xfbU, 0x80U, 0xabU, 0x0cU, 0x36U,
    0x1cU, 0xb3U, 0xc6U, 0x42U, 0x88U, 0x23U, 0x07U, 0x69U,
};

typedef struct ad7701h {
    gdox_scsi_transport transport;
    gdox_mutex mutex;
    gdox_mmc_media_tracker media;
    uint64_t generation;
    gdox_disc_evidence evidence;
    uint8_t video[GDOX_LOGICAL_SECTOR_BYTES];
    atomic_bool aborted;
    atomic_uint_fast64_t commands, sectors, last_lba;
    bool dirty, faulted, prepared;
} ad7701h;

static bool reject(gdox_error *error, const char *message)
{
    gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, message);
    return false;
}

static void put_be(uint8_t *output, uint32_t value, size_t bytes)
{
    while (bytes != 0U) {
        output[--bytes] = (uint8_t)value;
        value >>= 8U;
    }
}

static bool command_in(ad7701h *context, const uint8_t *cdb, size_t cdb_bytes,
    uint8_t *output, size_t bytes, gdox_error *error)
{
    size_t transferred = 0U;
    if (!gdox_scsi_command_in(&context->transport, "AD-7701H read",
            cdb, cdb_bytes, output, bytes, COMMAND_TIMEOUT, &transferred, error)) {
        return false;
    }
    if (transferred != bytes) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "AD-7701H returned a short transfer");
        return false;
    }
    return true;
}

static bool read_ram(ad7701h *context, uint32_t address, uint8_t output[4],
    gdox_error *error)
{
    uint8_t cdb[12] = {0xcfU, 7U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 1U, 0U, 0U};
    put_be(cdb + 2U, address, 4U);
    return command_in(context, cdb, sizeof(cdb), output, 4U, error);
}

static bool write_geometry_byte(ad7701h *context, size_t offset, uint8_t value,
    gdox_error *error)
{
    uint8_t cdb[12] = {0xceU, 0U, 5U};
    if ((offset != 1U && offset != 2U)
        || (value != stock_geometry[offset] && value != game_geometry[offset])) {
        return reject(error, "invalid AD-7701H geometry byte");
    }
    put_be(cdb + 3U, GEOMETRY_ADDRESS + (uint32_t)offset, 2U);
    cdb[6] = value;
    return gdox_scsi_command_none(&context->transport, "AD-7701H geometry byte",
        cdb, sizeof(cdb), COMMAND_TIMEOUT, error);
}

static bool known_geometry(const uint8_t value[4])
{
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        if (value[index] != stock_geometry[index]
            && value[index] != game_geometry[index]) return false;
    }
    return true;
}

static bool normal_mode(ad7701h *context, gdox_error *error)
{
    uint8_t mode[4];
    return read_ram(context, MODE_ADDRESS, mode, error)
        && (memcmp(mode, (uint8_t[4]){0}, sizeof(mode)) == 0
            || reject(error, "AD-7701H is not in normal mode"));
}

static bool baseline(ad7701h *context, uint8_t video[GDOX_LOGICAL_SECTOR_BYTES],
    gdox_error *error)
{
    uint32_t last, bytes;
    return normal_mode(context, error)
        && gdox_mmc_read_capacity_10(&context->transport, COMMAND_TIMEOUT,
            &last, &bytes, error)
        && ((last == UINT32_C(6991) && bytes == GDOX_LOGICAL_SECTOR_BYTES)
            || reject(error, "AD-7701H media capacity is not the validated XGD1 layout"))
        && gdox_mmc_read_10(&context->transport, 16U, 1U, 1U,
            GDOX_LOGICAL_SECTOR_BYTES, video, GDOX_LOGICAL_SECTOR_BYTES,
            COMMAND_TIMEOUT, error);
}

static bool restore(ad7701h *context, gdox_error *error)
{
    uint8_t state[4], video[GDOX_LOGICAL_SECTOR_BYTES];
    size_t offset;
    if (!context->dirty) return true;
    if (!normal_mode(context, error)
        || !read_ram(context, GEOMETRY_ADDRESS, state, error)) return false;
    if (!known_geometry(state)) {
        return reject(error, "AD-7701H geometry changed; restoration requires reconnecting the drive");
    }
    for (offset = 2U; offset != 0U; --offset) {
        if (state[offset] == stock_geometry[offset]) continue;
        if (!write_geometry_byte(context, offset, stock_geometry[offset], error)
            || !read_ram(context, GEOMETRY_ADDRESS, state, error)) return false;
        if (!known_geometry(state) || state[offset] != stock_geometry[offset]) {
            return reject(error, "AD-7701H restoration did not verify");
        }
    }
    if (memcmp(state, stock_geometry, sizeof(state)) != 0
        || !baseline(context, video, error)) return false;
    if (memcmp(video, context->video, sizeof(video)) != 0) {
        return reject(error, "AD-7701H ordinary read changed after restoration");
    }
    context->dirty = false;
    return true;
}

static bool activate(ad7701h *context, gdox_error *error)
{
    uint8_t state[4], expected[4];
    size_t offset;
    if (!read_ram(context, GEOMETRY_ADDRESS, state, error)) return false;
    if (memcmp(state, stock_geometry, sizeof(state)) != 0) {
        return reject(error, "AD-7701H geometry is not original");
    }
    memcpy(expected, stock_geometry, sizeof(expected));
    /* A lost acknowledgement can still mean the byte reached the drive. */
    context->dirty = true;
    for (offset = 1U; offset <= 2U; ++offset) {
        expected[offset] = game_geometry[offset];
        if (!write_geometry_byte(context, offset, expected[offset], error)
            || !read_ram(context, GEOMETRY_ADDRESS, state, error)) return false;
        if (memcmp(state, expected, sizeof(state)) != 0) {
            return reject(error, "AD-7701H activation did not verify");
        }
    }
    return true;
}

static bool validate_firmware(ad7701h *context, gdox_error *error)
{
    gdox_hash_stream *stream = NULL;
    gdox_hashes hashes;
    uint8_t *buffer = malloc(65536U);
    uint32_t address;
    bool success = false;
    if (buffer == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate firmware verification buffer");
        return false;
    }
    if (!gdox_hash_stream_create(&stream, error)) goto done;
    for (address = UINT32_C(0x30000); address < UINT32_C(0x1e0000); address += 65536U) {
        uint8_t cdb[10] = {0x3cU, 1U, 1U};
        put_be(cdb + 3U, address, 3U);
        put_be(cdb + 6U, 65536U, 3U);
        if (!command_in(context, cdb, sizeof(cdb), buffer, 65536U, error)
            || !gdox_hash_stream_update(stream, buffer, 65536U, error)) goto done;
    }
    if (!gdox_hash_stream_finish(stream, &hashes, error)) goto done;
    success = memcmp(hashes.sha256, firmware_sha256, sizeof(firmware_sha256)) == 0;
    if (!success) (void)reject(error, "AD-7701H firmware does not match the validated image");
done:
    gdox_hash_stream_destroy(stream);
    free(buffer);
    return success;
}

static bool read_game(ad7701h *context, uint32_t lba, uint32_t blocks,
    uint8_t *output, gdox_error *error)
{
    uint8_t cdb[12] = {0xcfU, 5U, 1U, 0x28U};
    put_be(cdb + 4U, lba + UINT32_C(0x30000), 4U);
    put_be(cdb + 8U, blocks, 2U);
    if (!command_in(context, cdb, sizeof(cdb), output,
            (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES, error)) return false;
    (void)atomic_fetch_add(&context->commands, 1U);
    (void)atomic_fetch_add(&context->sectors, blocks);
    atomic_store(&context->last_lba, (uint64_t)lba + blocks - 1U);
    return true;
}

static uint64_t sector_count(const void *context)
{
    (void)context;
    return GDOX_XGD1_TOTAL_SECTORS;
}

static bool read_sectors(void *raw, uint64_t lba, uint32_t blocks,
    uint8_t *output, size_t bytes, gdox_error *error)
{
    ad7701h *context = raw;
    uint8_t state[4];
    bool success = false;
    if (!gdox_source_validate_read(GDOX_XGD1_TOTAL_SECTORS, lba, blocks, bytes, error)) return false;
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not lock AD-7701H transport");
        return false;
    }
    if (context->faulted || context->prepared || !context->dirty
        || gdox_mmc_media_tracker_transitioned(&context->media, context->generation)) {
        (void)reject(error, "AD-7701H source must be closed before further reads");
        goto done;
    }
    if (atomic_load(&context->aborted)) {
        gdox_error_set(error, GDOX_ERROR_CANCELLED, "AD-7701H read cancelled");
        goto done;
    }
    if (!read_ram(context, GEOMETRY_ADDRESS, state, error)) goto done;
    if (memcmp(state, game_geometry, sizeof(state)) != 0) {
        (void)reject(error, "AD-7701H media geometry changed");
        goto done;
    }
    while (blocks != 0U) {
        const uint32_t count = blocks < MAX_BLOCKS ? blocks : MAX_BLOCKS;
        if (atomic_load(&context->aborted)) {
            gdox_error_set(error, GDOX_ERROR_CANCELLED, "AD-7701H read cancelled");
            goto done;
        }
        if (!read_game(context, (uint32_t)lba, count, output, error)) goto done;
        lba += count;
        blocks -= count;
        output += (size_t)count * GDOX_LOGICAL_SECTOR_BYTES;
    }
    success = true;
done:
    if (!success) context->faulted = true;
    gdox_mutex_unlock(&context->mutex);
    return success;
}

static void observe_media(const void *raw, gdox_media_observation *output)
{
    ad7701h *context = (ad7701h *)raw;
    *output = (gdox_media_observation){.readiness = GDOX_MEDIA_READINESS_UNKNOWN};
    if (!gdox_mutex_lock(&context->mutex)) return;
    if (!context->prepared && !context->faulted) {
        gdox_mmc_observe_media(&context->transport, 1000U, &context->media, output);
    } else {
        output->generation = context->media.generation;
    }
    gdox_mutex_unlock(&context->mutex);
}

static bool media_present(const void *raw)
{
    gdox_media_observation observation;
    observe_media(raw, &observation);
    return observation.readiness == GDOX_MEDIA_READINESS_PRESENT;
}

static bool evidence(const void *raw, gdox_disc_evidence *output)
{
    *output = ((const ad7701h *)raw)->evidence;
    return output->pfi_present && output->dmi_present;
}

static bool read_stats(const void *raw, gdox_physical_read_stats *output)
{
    const ad7701h *context = raw;
    output->commands = atomic_load(&context->commands);
    output->sectors = atomic_load(&context->sectors);
    output->bytes = output->sectors * GDOX_LOGICAL_SECTOR_BYTES;
    output->last_lba = atomic_load(&context->last_lba);
    return true;
}

static void abort_reads(void *raw)
{
    atomic_store(&((ad7701h *)raw)->aborted, true);
}

static bool prepare_close(void *raw, gdox_error *error)
{
    ad7701h *context = raw;
    bool success;
    if (context->prepared) return true;
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not lock AD-7701H for restoration");
        return false;
    }
    context->faulted = true;
    success = restore(context, error)
        && gdox_scsi_transport_prepare_close(&context->transport, error);
    context->prepared = success;
    gdox_mutex_unlock(&context->mutex);
    return success;
}

static bool close_source(void *raw, gdox_error *error)
{
    ad7701h *context = raw;
    const bool success = gdox_scsi_transport_close(&context->transport, error);
    gdox_mutex_destroy(&context->mutex);
    free(context);
    return success;
}

static const gdox_sector_source_ops source_ops = {
    .sector_count = sector_count, .read = read_sectors,
    .media_present = media_present, .close = close_source, .evidence = evidence,
    .physical_read_stats = read_stats, .abort = abort_reads,
    .prepare_close = prepare_close, .observe_media = observe_media,
};

static bool initialize(ad7701h *context, uint32_t ready_timeout, gdox_error *error)
{
    gdox_mmc_identity identity;
    uint8_t data[2052], state[4];
    size_t transferred;
    unsigned int format;
    if (!gdox_mmc_inquiry(&context->transport, COMMAND_TIMEOUT, &identity, error)) return false;
    if (strcmp(identity.vendor, GDOX_AD7701H_SCSI_VENDOR) != 0
        || strcmp(identity.model, GDOX_AD7701H_SCSI_MODEL) != 0
        || strcmp(identity.revision, GDOX_AD7701H_SCSI_REVISION) != 0) {
        return reject(error, "AD-7701H identity does not match the validated drive");
    }
    if (!gdox_mmc_test_unit_ready(&context->transport,
            ready_timeout != 0U ? ready_timeout : COMMAND_TIMEOUT, error)
        || !validate_firmware(context, error)
        || !read_ram(context, GEOMETRY_ADDRESS, state, error)) return false;
    if (memcmp(state, stock_geometry, sizeof(state)) != 0) {
        return reject(error, "AD-7701H geometry is not original");
    }
    if (!baseline(context, context->video, error)) return false;
    for (format = 0U; format <= 4U; format += 4U) {
        if (!gdox_mmc_read_dvd_structure(&context->transport, (uint8_t)format,
                data, sizeof(data), COMMAND_TIMEOUT, &transferred, error)) return false;
        if (transferred != sizeof(data)) return reject(error, "AD-7701H disc metadata is incomplete");
        if (format == 0U) {
            if (memcmp(data, stock_pfi, sizeof(stock_pfi)) != 0) {
                return reject(error, "AD-7701H disc geometry is not the validated XGD1 layout");
            }
            memcpy(context->evidence.pfi, data + 4U, GDOX_DISC_STRUCTURE_BYTES);
            context->evidence.pfi_present = true;
        } else {
            memcpy(context->evidence.dmi, data + 4U, GDOX_DISC_STRUCTURE_BYTES);
            context->evidence.dmi_present = true;
        }
    }
    memcpy(context->evidence.note, "Security-sector evidence is unavailable.",
        sizeof("Security-sector evidence is unavailable."));
    if (!activate(context, error)
        || !read_game(context, UINT32_C(0x30620), 1U, data, error)) return false;
    if (memcmp(data, "MICROSOFT*XBOX*MEDIA", 20U) != 0
        || memcmp(data + 2048U - 20U, "MICROSOFT*XBOX*MEDIA", 20U) != 0) {
        return reject(error, "AD-7701H did not return an XDVDFS descriptor");
    }
    gdox_mmc_media_tracker_begin_session(&context->transport, COMMAND_TIMEOUT, &context->media);
    context->generation = context->media.generation;
    return true;
}

bool gdox_ad7701h_source_open(
    bool (*opener)(void *, gdox_scsi_transport *, gdox_error *), void *opener_context,
    uint8_t read_retries, uint32_t ready_timeout_ms, gdox_sector_source *source,
    gdox_error *error)
{
    ad7701h *context;
    gdox_error original, cleanup;
    /* Failed reads stop the session; this drive has no validated reset path. */
    (void)read_retries;
    gdox_error_clear(error);
    if (opener == NULL || source == NULL || gdox_source_is_valid(source)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "an opener and empty AD-7701H source are required");
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate AD-7701H source");
        return false;
    }
    atomic_init(&context->aborted, false);
    atomic_init(&context->commands, 0U);
    atomic_init(&context->sectors, 0U);
    atomic_init(&context->last_lba, 0U);
    if (!gdox_mutex_init(&context->mutex)) {
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize AD-7701H lock");
        return false;
    }
    if (opener(opener_context, &context->transport, error)
        && initialize(context, ready_timeout_ms, error)) {
        *source = (gdox_sector_source){context, &source_ops};
        return true;
    }
    original = *error;
    if (gdox_scsi_transport_is_valid(&context->transport)) {
        *source = (gdox_sector_source){context, &source_ops};
        if (!gdox_source_close(source, &cleanup)) {
            *error = cleanup;
            return false;
        }
    } else {
        gdox_mutex_destroy(&context->mutex);
        free(context);
    }
    *error = original;
    return false;
}

static bool open_transport(void *unused, gdox_scsi_transport *transport, gdox_error *error)
{
    (void)unused;
    return gdox_usb_bot_open(GDOX_USB_BOT_AD7701H, transport, error);
}

bool gdox_optical_open_ad7701h(uint8_t retries, uint32_t ready_timeout,
    gdox_sector_source *source, gdox_error *error)
{
    return gdox_ad7701h_source_open(open_transport, NULL, retries, ready_timeout, source, error);
}
