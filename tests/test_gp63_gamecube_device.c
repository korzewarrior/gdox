#include "platform/gp63_gamecube_device.h"
#include "platform/gp63_gamecube_source.h"
#include "platform/gp63_sg_linux.h"
#include <stdio.h>
#include <string.h>

static unsigned int opens, closes, probes, streams;
static uint16_t profile;
static size_t reply_bytes;
static gdox_error_code open_error;
static bool command_ok, close_ok, selected;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static bool input(void *ctx, const char *operation, const uint8_t *cdb,
    size_t cdb_bytes, uint8_t *output, size_t capacity, uint32_t timeout,
    size_t *transferred, gdox_error *error)
{
    (void)ctx; (void)operation;
    ++probes;
    CHECK(cdb_bytes == 10U && cdb[0] == 0x46U && cdb[1] == 2U && capacity == 8U && timeout == 10000U);
    memset(output, 0, capacity); output[3] = 4U;
    output[6] = (uint8_t)(profile >> 8); output[7] = (uint8_t)profile;
    *transferred = reply_bytes;
    if (!command_ok) gdox_error_set(error, GDOX_ERROR_IO, "profile read failed");
    return command_ok;
}
static bool close_transport(void *ctx, gdox_error *error)
{
    (void)ctx; ++closes;
    if (!close_ok) gdox_error_set(error, GDOX_ERROR_IO, "close failed");
    return close_ok;
}
static bool unexpected_output(void *ctx, const char *name, const uint8_t *cdb,
    size_t cdb_bytes, const uint8_t *data, size_t bytes, uint32_t timeout,
    size_t *transferred, gdox_error *error)
{
    (void)ctx; (void)name; (void)cdb; (void)cdb_bytes; (void)data;
    (void)bytes; (void)timeout; (void)transferred; (void)error;
    CHECK(false); return false;
}
static bool unexpected_command(void *ctx, const char *name, const uint8_t *cdb,
    size_t bytes, uint32_t timeout, gdox_error *error)
{
    (void)ctx; (void)name; (void)cdb; (void)bytes; (void)timeout; (void)error;
    CHECK(false); return false;
}
static bool unexpected_reset(void *ctx, gdox_error *error)
{
    (void)ctx; (void)error; CHECK(false); return false;
}
static const gdox_scsi_transport_ops ops = {
    .command_in = input, .command_out = unexpected_output,
    .command_none = unexpected_command, .reset = unexpected_reset,
    .close = close_transport,
};
bool gdox_gp63_sg_linux_open_device(const gdox_optical_device *device,
    gdox_scsi_transport *transport, gdox_error *error)
{
    ++opens; selected = strcmp(device->id, "selected-test-drive") == 0;
    if (open_error != GDOX_ERROR_NONE) {
        gdox_error_set(error, open_error, "open failed"); return false;
    }
    transport->context = &opens; transport->ops = &ops; return true;
}
bool gdox_gp63_gamecube_stream_source_open(gdox_gp63_gamecube_transport_opener opener,
    void *ctx, gdox_sector_source *source, gdox_error *error)
{
    gdox_scsi_transport transport = {0};
    (void)source; ++streams;
    CHECK(opener(ctx, &transport, error));
    CHECK(transport.context == &opens && transport.ops == &ops);
    CHECK(!gdox_scsi_transport_is_valid((gdox_scsi_transport *)ctx));
    CHECK(gdox_scsi_transport_close(&transport, error));
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "fixture decoder rejects media");
    return false;
}
static void reset(void)
{
    opens = closes = probes = streams = 0U; profile = 0x10U;
    reply_bytes = 8U; open_error = GDOX_ERROR_NONE;
    command_ok = close_ok = true; selected = false;
}
int main(void)
{
    gdox_optical_device device = {.drive = GDOX_OPTICAL_DRIVE_GP63};
    gdox_sector_source source = {0}; gdox_error error; bool candidate;
    strcpy(device.id, "selected-test-drive");
    reset();
    CHECK(gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(!candidate && selected && opens == 1U && closes == 1U && streams == 0U);
    reset(); profile = 0U;
    CHECK(!gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(candidate && opens == 1U && closes == 1U && streams == 1U);
    reset(); reply_bytes = 4U;
    CHECK(!gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(!candidate && streams == 0U && closes == 1U && error.code == GDOX_ERROR_PROTOCOL);
    reset(); open_error = GDOX_ERROR_UNSUPPORTED;
    CHECK(gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(!candidate && probes == 0U && !gdox_error_is_set(&error));
    reset(); open_error = GDOX_ERROR_NOT_FOUND;
    CHECK(gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(!candidate && probes == 0U);
    reset(); open_error = GDOX_ERROR_IO;
    CHECK(!gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(!candidate && probes == 0U && error.code == GDOX_ERROR_IO);
    reset(); command_ok = false;
    CHECK(!gdox_gp63_gamecube_open_selected(&device, &source, &candidate, &error));
    CHECK(closes == 1U && streams == 0U);
    reset();
    CHECK(!gdox_gp63_gamecube_open_selected(NULL, &source, &candidate, &error));
    CHECK(opens == 0U);
    return failures == 0 ? 0 : 1;
}
