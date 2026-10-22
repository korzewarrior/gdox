#include "platform/ad7701h_source.h"
#include "gdox/hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static bool digest_matches = true;

/* Simulate the digest boundary without distributing a drive's firmware.
 * The real digest backend is covered by core.hash and live-drive validation. */
struct gdox_hash_stream { size_t bytes; };
bool gdox_hash_stream_create(gdox_hash_stream **stream, gdox_error *error)
{
    (void)error;
    *stream = calloc(1U, sizeof(**stream));
    return *stream != NULL;
}
bool gdox_hash_stream_update(gdox_hash_stream *stream, const uint8_t *data,
    size_t bytes, gdox_error *error)
{
    (void)data; (void)error;
    stream->bytes += bytes;
    return true;
}
bool gdox_hash_stream_finish(gdox_hash_stream *stream, gdox_hashes *hashes, gdox_error *error)
{
    static const uint8_t expected[32] = {
        0xc3, 0x63, 0x7c, 0xfb, 0xdc, 0x5f, 0x2f, 0x69,
        0xf8, 0x9a, 0x35, 0xdc, 0xdd, 0xae, 0x67, 0x99,
        0x96, 0xe8, 0xf4, 0xfb, 0x80, 0xab, 0x0c, 0x36,
        0x1c, 0xb3, 0xc6, 0x42, 0x88, 0x23, 0x07, 0x69,
    };
    (void)error;
    CHECK(stream->bytes == 1769472U);
    memset(hashes, 0, sizeof(*hashes));
    if (digest_matches) memcpy(hashes->sha256, expected, sizeof(expected));
    return true;
}
void gdox_hash_stream_destroy(gdox_hash_stream *stream) { free(stream); }

typedef struct drive {
    uint8_t geometry[4];
    unsigned int commands, fail_at, short_at, writes, reads, closed;
    bool fail_after_write, wrong_identity, wrong_mode, wrong_pfi, bad_descriptor;
    uint32_t firmware_address;
} drive;

static drive fresh(void)
{
    return (drive){.geometry = {0xafU, 0x1aU, 3U, 0U}, .firmware_address = 0x30000U};
}

static bool failure(drive *mock, gdox_error *error)
{
    if (++mock->commands != mock->fail_at) return false;
    gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected transport failure");
    return true;
}

static uint32_t be(const uint8_t *data, size_t bytes)
{
    uint32_t value = 0U;
    while (bytes-- != 0U) value = (value << 8U) | *data++;
    return value;
}

static bool command_in(void *raw, const char *name, const uint8_t *cdb, size_t cdb_bytes,
    uint8_t *data, size_t bytes, uint32_t timeout, size_t *transferred, gdox_error *error)
{
    drive *mock = raw;
    (void)name; (void)cdb_bytes; (void)timeout;
    if (failure(mock, error)) return false;
    memset(data, 0, bytes);
    *transferred = mock->commands == mock->short_at ? bytes - 1U : bytes;
    switch (cdb[0]) {
        case 0x03U:
            CHECK(bytes == 18U);
            data[0] = 0x70U; data[7] = 10U;
            break;
        case 0x12U:
            CHECK(bytes >= 36U);
            memset(data + 8U, ' ', 28U);
            memcpy(data + 8U, "hp", 2U);
            memcpy(data + 16U, "DVD RW AD-7701H", sizeof("DVD RW AD-7701H") - 1U);
            memcpy(data + 32U, mock->wrong_identity ? "1.00" : "2.85", 4U);
            break;
        case 0x3cU:
            CHECK(cdb[1] == 1U && cdb[2] == 1U && bytes == 65536U);
            CHECK(be(cdb + 3U, 3U) == mock->firmware_address);
            CHECK(be(cdb + 6U, 3U) == bytes);
            mock->firmware_address += (uint32_t)bytes;
            break;
        case 0xcfU:
            if (cdb[1] == 7U) {
                CHECK(bytes == 4U && cdb[9] == 1U);
                if (be(cdb + 2U, 4U) == 0x03ff8688U) memcpy(data, mock->geometry, 4U);
                else {
                    CHECK(be(cdb + 2U, 4U) == 0x03ff9790U);
                    data[0] = mock->wrong_mode ? 1U : 0U;
                }
            } else {
                CHECK(cdb[1] == 5U && cdb[2] == 1U && cdb[3] == 0x28U);
                CHECK(memcmp(mock->geometry, (uint8_t[4]){0xafU, 0x33U, 0x20U, 0U}, 4U) == 0);
                CHECK(be(cdb + 8U, 2U) <= 16U && bytes == be(cdb + 8U, 2U) * 2048U);
                ++mock->reads;
                memset(data, 0x5a, bytes);
                if (be(cdb + 4U, 4U) == 0x60620U && !mock->bad_descriptor) {
                    memcpy(data, "MICROSOFT*XBOX*MEDIA", 20U);
                    memcpy(data + 2048U - 20U, "MICROSOFT*XBOX*MEDIA", 20U);
                }
            }
            break;
        case 0x25U:
            CHECK(bytes == 8U);
            memcpy(data, (uint8_t[8]){0U, 0U, 0x1bU, 0x4fU, 0U, 0U, 8U, 0U}, 8U);
            break;
        case 0x28U:
            CHECK(be(cdb + 2U, 4U) == 16U && be(cdb + 7U, 2U) == 1U);
            memset(data, 0x5a, bytes);
            break;
        case 0xadU:
            CHECK(bytes == 2052U && (cdb[7] == 0U || cdb[7] == 4U));
            memcpy(data, (uint8_t[20]){8U, 2U, 0U, 0U, 1U, 2U, 0x31U, 0x10U,
                0U, 3U, 0U, 0U, 0U, 0xfcU, 0xe5U, 0xefU, 0U, 3U, 0x1aU, 0xafU}, 20U);
            if (mock->wrong_pfi) data[17] = 4U;
            break;
        case 0x4aU:
            CHECK(bytes == 8U);
            data[1] = 6U; data[2] = 0x84U; data[3] = 0x10U;
            break;
        default: CHECK(false);
    }
    return true;
}

static bool command_out(void *raw, const char *name, const uint8_t *cdb, size_t cdb_bytes,
    const uint8_t *data, size_t bytes, uint32_t timeout, size_t *transferred, gdox_error *error)
{
    (void)raw; (void)name; (void)cdb; (void)cdb_bytes; (void)data;
    (void)bytes; (void)timeout; (void)transferred; (void)error;
    CHECK(false); return false;
}

static bool command_none(void *raw, const char *name, const uint8_t *cdb,
    size_t cdb_bytes, uint32_t timeout, gdox_error *error)
{
    drive *mock = raw;
    bool failed = failure(mock, error);
    (void)name; (void)timeout;
    if (failed && !mock->fail_after_write) return false;
    if (cdb[0] == 0xceU) {
        size_t offset = (size_t)be(cdb + 3U, 2U) - 0x8688U;
        CHECK(cdb_bytes == 12U && cdb[1] == 0U && cdb[2] == 5U);
        CHECK((offset == 1U && (cdb[6] == 0x1aU || cdb[6] == 0x33U))
            || (offset == 2U && (cdb[6] == 3U || cdb[6] == 0x20U)));
        mock->geometry[offset] = cdb[6];
        ++mock->writes;
    } else CHECK(cdb[0] == 0U);
    return !failed;
}

static bool reset(void *raw, gdox_error *error)
{
    (void)raw; (void)error; CHECK(false); return false;
}
static bool close_drive(void *raw, gdox_error *error)
{
    drive *mock = raw;
    (void)error;
    CHECK(mock->geometry[1] != 0x33U && mock->geometry[2] != 0x20U);
    ++mock->closed;
    return true;
}
static const gdox_scsi_transport_ops ops = {
    .command_in = command_in, .command_out = command_out, .command_none = command_none,
    .reset = reset, .close = close_drive,
};
static bool open_drive(void *raw, gdox_scsi_transport *transport, gdox_error *error)
{
    (void)error;
    *transport = (gdox_scsi_transport){raw, &ops};
    return true;
}

static bool open_source(drive *mock, gdox_sector_source *source, gdox_error *error)
{
    return gdox_ad7701h_source_open(open_drive, mock, 3U, 1000U, source, error);
}

int main(void)
{
    gdox_error error;
    unsigned int total;
    uint8_t buffer[33U * 2048U];
    drive mock = fresh();
    gdox_sector_source source = {0};
    if (!open_source(&mock, &source, &error)) {
        fprintf(stderr, "open failed: %s\n", error.message);
        return 1;
    }
    CHECK(gdox_source_read(&source, 2000000U, 33U, buffer, sizeof(buffer), &error));
    CHECK(mock.reads == 4U);
    CHECK(gdox_source_prepare_close(&source, &error));
    total = mock.commands;
    CHECK(gdox_source_prepare_close(&source, &error) && mock.commands == total);
    CHECK(gdox_source_close(&source, &error) && mock.closed == 1U);
    CHECK(mock.writes == 4U);

    /* Every initialization/read/restore command can fail, including a write
     * acknowledged only after its byte has already changed. Ownership survives. */
    for (unsigned int after = 0U; after < 2U; ++after) {
        for (unsigned int fail = 1U; fail <= total; ++fail) {
            mock = fresh(); mock.fail_at = fail; mock.fail_after_write = after != 0U;
            if (open_source(&mock, &source, &error)) {
                (void)gdox_source_read(&source, 2000000U, 33U, buffer, sizeof(buffer), &error);
            }
            if (gdox_source_is_valid(&source) && !gdox_source_close(&source, &error)) {
                CHECK(gdox_source_is_valid(&source) && mock.closed == 0U);
                CHECK(gdox_source_close(&source, &error));
            }
            CHECK(mock.closed == 1U);
            CHECK(memcmp(mock.geometry, (uint8_t[4]){0xafU, 0x1aU, 3U, 0U}, 4U) == 0);
        }
    }
    for (unsigned int gate = 0U; gate < 5U; ++gate) {
        mock = fresh();
        mock.wrong_identity = gate == 0U;
        digest_matches = gate != 1U;
        mock.wrong_mode = gate == 2U;
        mock.wrong_pfi = gate == 3U;
        if (gate == 4U) mock.geometry[0] = 0xeeU;
        CHECK(!open_source(&mock, &source, &error));
        CHECK(mock.writes == 0U && mock.closed == 1U && !gdox_source_is_valid(&source));
    }
    digest_matches = true;
    mock = fresh(); mock.bad_descriptor = true;
    CHECK(!open_source(&mock, &source, &error));
    CHECK(mock.closed == 1U && mock.writes == 4U);

    mock = fresh();
    CHECK(open_source(&mock, &source, &error));
    mock.geometry[0] = 0xeeU;
    total = mock.writes;
    CHECK(!gdox_source_close(&source, &error) && mock.writes == total);
    CHECK(gdox_source_is_valid(&source) && mock.closed == 0U);
    mock.geometry[0] = 0xafU;
    CHECK(gdox_source_close(&source, &error));

    mock = fresh();
    CHECK(open_source(&mock, &source, &error));
    gdox_source_abort(&source);
    total = mock.commands;
    CHECK(!gdox_source_read(&source, 2000000U, 1U, buffer, 2048U, &error));
    CHECK(error.code == GDOX_ERROR_CANCELLED && mock.commands == total);
    CHECK(gdox_source_close(&source, &error));

    mock = fresh();
    CHECK(open_source(&mock, &source, &error));
    mock.short_at = mock.commands + 2U;
    CHECK(!gdox_source_read(&source, 2000000U, 1U, buffer, 2048U, &error));
    total = mock.commands;
    CHECK(!gdox_source_read(&source, 2000000U, 1U, buffer, 2048U, &error));
    CHECK(mock.commands == total);
    CHECK(gdox_source_close(&source, &error));
    puts("AD-7701H activation, read and restoration fault checks passed");
    return 0;
}
