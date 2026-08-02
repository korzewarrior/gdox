#include "platform/optical_driver.h"

#include "test.h"

#include <string.h>

int gdox_test_failures = 0;

static void run(void)
{
    gdox_usb_bot_observation
        observations[GDOX_USB_BOT_IDENTITY_COUNT] = {0};
    gdox_optical_presence presence;
    gdox_error error;

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
}

int main(void)
{
    run();
    return gdox_test_failures == 0 ? 0 : 1;
}
