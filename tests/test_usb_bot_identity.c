#include "platform/usb_bot_identity.h"

#include "gdox/optical.h"
#include "test.h"

#include <stddef.h>

int gdox_test_failures = 0;

typedef struct candidate {
    gdox_usb_bot_observed_identity identity;
    gdox_usb_bot_location location;
} candidate;

static size_t select_candidate(
    gdox_usb_bot_identity requested,
    const gdox_usb_bot_location *location,
    const candidate *candidates,
    size_t candidate_count
)
{
    size_t index;

    for (index = 0U; index < candidate_count; ++index) {
        if (gdox_usb_bot_candidate_matches(
                requested,
                location,
                &candidates[index].identity,
                &candidates[index].location
            )) {
            return index;
        }
    }
    return candidate_count;
}

static gdox_usb_bot_observed_identity observed_gp63(void)
{
    return (gdox_usb_bot_observed_identity){
        GDOX_GP63_USB_VENDOR_ID,
        GDOX_GP63_USB_PRODUCT_ID,
        GDOX_GP63_SCSI_VENDOR,
        GDOX_GP63_SCSI_MODEL,
        GDOX_GP63_SCSI_REVISION,
    };
}

static gdox_usb_bot_observed_identity observed_gp65(void)
{
    return (gdox_usb_bot_observed_identity){
        GDOX_GP65_USB_VENDOR_ID,
        GDOX_GP65_USB_PRODUCT_ID,
        GDOX_GP65_SCSI_VENDOR,
        GDOX_GP65_SCSI_MODEL,
        GDOX_GP65_SCSI_REVISION,
    };
}

static void run(void)
{
    const gdox_usb_bot_identity_spec *gp63 =
        gdox_usb_bot_identity_get(GDOX_USB_BOT_GP63);
    const gdox_usb_bot_identity_spec *gp65 =
        gdox_usb_bot_identity_get(GDOX_USB_BOT_GP65);
    gdox_usb_bot_observed_identity gp63_observed = observed_gp63();
    gdox_usb_bot_observed_identity gp65_observed = observed_gp65();
    gdox_usb_bot_location gp63_location = {
        1U,
        7U,
        {2U, 3U},
        2U,
    };
    gdox_usb_bot_location gp65_location = {
        1U,
        8U,
        {2U, 4U},
        2U,
    };
    candidate candidates[2];

    GDOX_TEST_CHECK(gp63 != NULL);
    GDOX_TEST_CHECK(gp65 != NULL);
    if (gp63 == NULL || gp65 == NULL) {
        return;
    }
    GDOX_TEST_CHECK(gp63->vendor_id == gp65->vendor_id);
    GDOX_TEST_CHECK(gp63->product_id == gp65->product_id);
    GDOX_TEST_CHECK(gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP63,
        &gp63_observed
    ));
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        &gp63_observed
    ));
    GDOX_TEST_CHECK(gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        &gp65_observed
    ));
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP63,
        &gp65_observed
    ));
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        NULL
    ));

    gp65_observed.scsi_revision = "PB01";
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        &gp65_observed
    ));
    gp65_observed = observed_gp65();
    ++gp65_observed.product_id;
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        &gp65_observed
    ));
    gp65_observed = observed_gp65();
    gp65_observed.scsi_model = GDOX_GP63_SCSI_MODEL;
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        &gp65_observed
    ));
    gp65_observed = observed_gp65();
    gp65_observed.scsi_vendor = "HL-DT-SX";
    GDOX_TEST_CHECK(!gdox_usb_bot_identity_matches(
        GDOX_USB_BOT_GP65,
        &gp65_observed
    ));

    candidates[0] = (candidate){observed_gp63(), gp63_location};
    candidates[1] = (candidate){observed_gp65(), gp65_location};
    GDOX_TEST_CHECK(select_candidate(
        GDOX_USB_BOT_GP65,
        NULL,
        candidates,
        1U
    ) == 1U);
    GDOX_TEST_CHECK(select_candidate(
        GDOX_USB_BOT_GP65,
        NULL,
        candidates,
        2U
    ) == 1U);
    GDOX_TEST_CHECK(select_candidate(
        GDOX_USB_BOT_GP65,
        &gp65_location,
        candidates,
        2U
    ) == 1U);

    candidates[1].location.address = 19U;
    GDOX_TEST_CHECK(gdox_usb_bot_location_matches(
        &gp65_location,
        &candidates[1].location
    ));
    GDOX_TEST_CHECK(select_candidate(
        GDOX_USB_BOT_GP65,
        &gp65_location,
        candidates,
        2U
    ) == 1U);

    candidates[1].identity = observed_gp63();
    GDOX_TEST_CHECK(select_candidate(
        GDOX_USB_BOT_GP65,
        NULL,
        candidates,
        2U
    ) == 2U);
    GDOX_TEST_CHECK(select_candidate(
        GDOX_USB_BOT_GP65,
        &gp65_location,
        candidates,
        2U
    ) == 2U);

    gp65_location.port_count = 0U;
    candidates[1].location.port_count = 0U;
    candidates[1].location.address = gp65_location.address;
    GDOX_TEST_CHECK(gdox_usb_bot_location_matches(
        &gp65_location,
        &candidates[1].location
    ));
    candidates[1].location.address = (uint8_t)(gp65_location.address + 1U);
    GDOX_TEST_CHECK(!gdox_usb_bot_location_matches(
        &gp65_location,
        &candidates[1].location
    ));
}

int main(void)
{
    run();
    return gdox_test_failures == 0 ? 0 : 1;
}
