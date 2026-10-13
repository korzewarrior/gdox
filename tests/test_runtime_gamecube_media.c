#include "app/runtime_media.h"
#include "gdox/source.h"

#include <stdio.h>
#include <string.h>

static struct {
    uint8_t header[GDOX_LOGICAL_SECTOR_BYTES];
    bool read_fails;
    bool restore_fails;
    unsigned closes;
} fixture;

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (false)

static uint64_t source_sectors(const void *context)
{
    (void)context;
    return GDOX_GAMECUBE_TOTAL_SECTORS;
}

static bool source_read(void *context, uint64_t lba, uint32_t blocks,
    uint8_t *output, size_t output_bytes, gdox_error *error)
{
    (void)context;
    if (fixture.read_fails || lba != 0U || blocks != 1U
        || output_bytes != sizeof(fixture.header)) {
        gdox_error_set(error, GDOX_ERROR_IO, "injected header read failure");
        return false;
    }
    memcpy(output, fixture.header, sizeof(fixture.header));
    gdox_error_clear(error);
    return true;
}

static bool source_close(void *context, gdox_error *error)
{
    (void)context;
    ++fixture.closes;
    gdox_error_clear(error);
    return true;
}

static bool source_prepare_close(void *context, gdox_error *error)
{
    (void)context;
    if (fixture.restore_fails) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected restoration failure");
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops source_ops = {
    source_sectors, source_read, NULL, source_close, NULL, NULL, NULL,
    source_prepare_close, NULL,
};

bool __wrap_gdox_optical_open_device_media(const gdox_optical_device *device,
    uint8_t retries, uint32_t ready_timeout_ms, gdox_sector_source *source,
    gdox_optical_media_info *info, gdox_error *error);

bool __wrap_gdox_optical_open_device_media(const gdox_optical_device *device,
    uint8_t retries, uint32_t ready_timeout_ms, gdox_sector_source *source,
    gdox_optical_media_info *info, gdox_error *error)
{
    (void)device;
    (void)retries;
    (void)ready_timeout_ms;
    source->context = &fixture;
    source->ops = &source_ops;
    memset(info, 0, sizeof(*info));
    info->profile = GDOX_OPTICAL_MEDIA_GAMECUBE;
    gdox_error_clear(error);
    return true;
}

static bool test_physical_gamecube(void)
{
    for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
        gdox_runtime_media_session session = {0};
        gdox_runtime_media_open_result result = {0};
        gdox_optical_device device = {0};
        gdox_error error;
        memset(&fixture, 0, sizeof(fixture));
        memcpy(fixture.header + 0x1cU, "\xc2\x33\x9f\x3d", 4U);
        memcpy(fixture.header + 0x20U, "Test title", 11U);
        if (scenario == 1U) fixture.header[0x1cU] = 0U;
        if (scenario == 2U) fixture.read_fails = true;
        if (scenario == 3U) memset(fixture.header + 0x20U, 0xff, 0x3e0U);
        if (scenario == 4U) fixture.header[0x20U] = 0U;
        if (scenario == 5U) {
            fixture.header[0x1cU] = 0U;
            fixture.restore_fails = true;
        }
        const bool opened = gdox_runtime_media_open_physical(&device, &session, &result, &error);
        if (scenario == 1U || scenario == 2U || scenario == 5U) {
            CHECK(!opened && !session.open);
            CHECK(result.state == GDOX_RUNTIME_MEDIA_UNIDENTIFIED);
            CHECK(gdox_runtime_media_is_owned(&session) == (scenario == 5U));
        } else {
            CHECK(opened && session.open && session.exported != NULL);
            CHECK(session.info.source == GDOX_MEDIA_PHYSICAL_DISC);
            CHECK(session.info.platform == GDOX_MEDIA_PLATFORM_GAMECUBE);
            CHECK(session.info.backend == GDOX_MEDIA_BACKEND_DOLPHIN);
            CHECK(session.info.source_sectors == GDOX_GAMECUBE_TOTAL_SECTORS);
            CHECK(session.image_path[0] == '\0');
            if (scenario == 0U) CHECK(strcmp(session.info.title, "Test title") == 0);
            if (scenario == 3U) {
                CHECK(strlen(session.info.title) == sizeof(session.info.title) - 1U);
                for (size_t index = 0U; index < strlen(session.info.title); ++index)
                    CHECK(session.info.title[index] == '?');
            }
            if (scenario == 4U) CHECK(strcmp(session.info.title, "GameCube disc") == 0);
        }
        if (scenario == 5U) {
            CHECK(fixture.closes == 0U);
            CHECK(!gdox_runtime_media_close(&session, &error));
            CHECK(gdox_runtime_media_is_owned(&session));
            fixture.restore_fails = false;
        }
        CHECK(gdox_runtime_media_close(&session, &error));
        CHECK(!gdox_runtime_media_is_owned(&session));
        CHECK(fixture.closes == 1U);
    }
    return true;
}

int main(void)
{
    return test_physical_gamecube() ? 0 : 1;
}
