#include "platform/optical_driver.h"

#include "test.h"

#include <string.h>

int gdox_test_failures = 0;

static uint64_t retained_sector_count(const void *context)
{
    (void)context;
    return UINT64_C(1);
}

static bool retained_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    (void)context;
    (void)lba;
    (void)blocks;
    (void)output;
    (void)output_bytes;
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected retained-source read");
    return false;
}

static bool retained_close(void *context, gdox_error *error)
{
    bool *closed = context;
    *closed = true;
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops retained_ops = {
    retained_sector_count,
    retained_read,
    NULL,
    retained_close,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static void run(void)
{
    gdox_usb_bot_observation
        observations[GDOX_USB_BOT_IDENTITY_COUNT] = {0};
    gdox_optical_presence presence;
    gdox_sector_source source = {0};
    gdox_optical_media_info media_info;
    gdox_optical_eject_completion eject_completion;
    gdox_error error;
    bool retained_closed = false;

    GDOX_TEST_CHECK(gdox_optical_select_presence(
        observations,
        &presence,
        &error
    ));
    GDOX_TEST_CHECK(!presence.drive_present);
    GDOX_TEST_CHECK(presence.drive == GDOX_OPTICAL_DRIVE_NONE);

    observations[GDOX_USB_BOT_GP65] = (gdox_usb_bot_observation){
        true,
        true,
        true,
    };
    GDOX_TEST_CHECK(gdox_optical_select_presence(
        observations,
        &presence,
        &error
    ));
    GDOX_TEST_CHECK(presence.drive_present);
    GDOX_TEST_CHECK(presence.media_status_known);
    GDOX_TEST_CHECK(presence.media_present);
    GDOX_TEST_CHECK(presence.drive == GDOX_OPTICAL_DRIVE_GP65);

    observations[GDOX_USB_BOT_GP08].drive_present = true;
    GDOX_TEST_CHECK(!gdox_optical_select_presence(
        observations,
        &presence,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(!presence.drive_present);
    GDOX_TEST_CHECK(presence.drive == GDOX_OPTICAL_DRIVE_NONE);

    GDOX_TEST_CHECK(strcmp(
        gdox_optical_drive_name(GDOX_OPTICAL_DRIVE_GP63),
        GDOX_GP63_SCSI_VENDOR " " GDOX_GP63_SCSI_MODEL " "
            GDOX_GP63_SCSI_REVISION
    ) == 0);
    GDOX_TEST_CHECK(gdox_optical_drive_can_eject(
        GDOX_OPTICAL_DRIVE_GP63
    ));
    GDOX_TEST_CHECK(!gdox_optical_drive_can_eject(
        GDOX_OPTICAL_DRIVE_ASUS_NR09
    ));
    GDOX_TEST_CHECK(!gdox_optical_drive_can_eject(
        GDOX_OPTICAL_DRIVE_NONE
    ));
    GDOX_TEST_CHECK(gdox_optical_complete_eject_request(
        GDOX_OPTICAL_DRIVE_ASUS_NR09,
        &eject_completion,
        &error
    ));
    GDOX_TEST_CHECK(
        eject_completion
            == GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT
    );
    GDOX_TEST_CHECK(!gdox_error_is_set(&error));
    eject_completion = GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED;
    GDOX_TEST_CHECK(!gdox_optical_complete_eject_request(
        GDOX_OPTICAL_DRIVE_NONE,
        &eject_completion,
        &error
    ));
    GDOX_TEST_CHECK(
        eject_completion == GDOX_OPTICAL_EJECT_COMPLETION_NONE
    );
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    source.context = &retained_closed;
    source.ops = &retained_ops;
    GDOX_TEST_CHECK(!gdox_optical_open_media(
        GDOX_OPTICAL_DRIVE_GP63,
        0U,
        0U,
        &source,
        &media_info,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(gdox_source_is_valid(&source));
    GDOX_TEST_CHECK(!retained_closed);
    GDOX_TEST_CHECK(gdox_source_close(&source, &error));
    GDOX_TEST_CHECK(retained_closed);

    memset(&media_info, 0xff, sizeof(media_info));
    GDOX_TEST_CHECK(!gdox_optical_open_media(
        GDOX_OPTICAL_DRIVE_NONE,
        0U,
        0U,
        &source,
        &media_info,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(!gdox_source_is_valid(&source));
}

int main(void)
{
    run();
    return gdox_test_failures == 0 ? 0 : 1;
}
