#include "platform/mt1887_profile.h"

#include "gdox/optical.h"
#include "test.h"

#include <string.h>

int gdox_test_failures = 0;

static void run(void)
{
    const gdox_mt1887_profile *gp63 = gdox_mt1887_profile_find(
        GDOX_USB_BOT_GP63,
        GDOX_GP63_SCSI_VENDOR,
        GDOX_GP63_SCSI_MODEL,
        GDOX_GP63_SCSI_REVISION
    );
    const gdox_mt1887_profile *gp65 = gdox_mt1887_profile_find(
        GDOX_USB_BOT_GP65,
        GDOX_GP65_SCSI_VENDOR,
        GDOX_GP65_SCSI_MODEL,
        GDOX_GP65_SCSI_REVISION
    );

    GDOX_TEST_CHECK(gp63 != NULL);
    GDOX_TEST_CHECK(gp65 != NULL);
    GDOX_TEST_CHECK(gdox_mt1887_profile_find(
        GDOX_USB_BOT_GP65,
        GDOX_GP65_SCSI_VENDOR,
        GDOX_GP65_SCSI_MODEL,
        "PB01"
    ) == NULL);
    GDOX_TEST_CHECK(gdox_mt1887_profile_find(
        GDOX_USB_BOT_GP63,
        GDOX_GP65_SCSI_VENDOR,
        GDOX_GP65_SCSI_MODEL,
        GDOX_GP65_SCSI_REVISION
    ) == NULL);
    GDOX_TEST_CHECK(gdox_mt1887_profile_find(
        GDOX_USB_BOT_GP08,
        GDOX_GP08_SCSI_VENDOR,
        GDOX_GP08_SCSI_MODEL,
        GDOX_GP08_SCSI_REVISION
    ) == NULL);
    if (gp63 == NULL || gp65 == NULL) {
        return;
    }
    GDOX_TEST_CHECK(gp63->capacity_addresses[0] == 0x8538U);
    GDOX_TEST_CHECK(gp63->geometry_addresses[0] == 0x8be2U);
    GDOX_TEST_CHECK(!gp63->auxiliary_present);
    GDOX_TEST_CHECK(gp65->capacity_addresses[0] == 0x8a37U);
    GDOX_TEST_CHECK(gp65->geometry_addresses[0] == 0x8be2U);
    GDOX_TEST_CHECK(gp65->auxiliary_present);
    GDOX_TEST_CHECK(gp65->auxiliary_addresses[0] == 0x8538U);
    GDOX_TEST_CHECK(memcmp(
        gp65->auxiliary,
        (const uint8_t[]){0x64U, 0x00U, 0x64U},
        3U
    ) == 0);
    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp63, false) == 128U);
    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp63, true) == 32U);
    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp65, false) == 128U);
    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp65, true) == 32U);
}

int main(void)
{
    run();
    return gdox_test_failures == 0 ? 0 : 1;
}
