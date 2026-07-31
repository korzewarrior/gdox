#include "platform/usb_bot_identity.h"

#include "gdox/optical.h"

#include <string.h>

static const gdox_usb_bot_identity_spec gp63 = {
    GDOX_USB_BOT_GP63,
    GDOX_GP63_USB_VENDOR_ID,
    GDOX_GP63_USB_PRODUCT_ID,
    GDOX_GP63_SCSI_VENDOR,
    GDOX_GP63_SCSI_MODEL,
    GDOX_GP63_SCSI_REVISION,
};

static const gdox_usb_bot_identity_spec gp65 = {
    GDOX_USB_BOT_GP65,
    GDOX_GP65_USB_VENDOR_ID,
    GDOX_GP65_USB_PRODUCT_ID,
    GDOX_GP65_SCSI_VENDOR,
    GDOX_GP65_SCSI_MODEL,
    GDOX_GP65_SCSI_REVISION,
};

static const gdox_usb_bot_identity_spec gp08 = {
    GDOX_USB_BOT_GP08,
    GDOX_GP08_USB_VENDOR_ID,
    GDOX_GP08_USB_PRODUCT_ID,
    GDOX_GP08_SCSI_VENDOR,
    GDOX_GP08_SCSI_MODEL,
    GDOX_GP08_SCSI_REVISION,
};

static const gdox_usb_bot_identity_spec asus_nr09 = {
    GDOX_USB_BOT_ASUS_NR09,
    GDOX_ASUS_USB_VENDOR_ID,
    GDOX_ASUS_USB_PRODUCT_ID,
    GDOX_ASUS_SCSI_VENDOR,
    GDOX_ASUS_SCSI_MODEL,
    GDOX_ASUS_SCSI_REVISION,
};

const gdox_usb_bot_identity_spec *gdox_usb_bot_identity_get(
    gdox_usb_bot_identity identity
)
{
    if (identity == GDOX_USB_BOT_GP63) {
        return &gp63;
    }
    if (identity == GDOX_USB_BOT_GP65) {
        return &gp65;
    }
    if (identity == GDOX_USB_BOT_GP08) {
        return &gp08;
    }
    if (identity == GDOX_USB_BOT_ASUS_NR09) {
        return &asus_nr09;
    }
    return NULL;
}

bool gdox_usb_bot_identity_matches(
    gdox_usb_bot_identity requested,
    const gdox_usb_bot_observed_identity *observed
)
{
    const gdox_usb_bot_identity_spec *expected =
        gdox_usb_bot_identity_get(requested);

    return expected != NULL && observed != NULL
        && observed->scsi_vendor != NULL
        && observed->scsi_model != NULL
        && observed->scsi_revision != NULL
        && observed->vendor_id == expected->vendor_id
        && observed->product_id == expected->product_id
        && strcmp(observed->scsi_vendor, expected->scsi_vendor) == 0
        && strcmp(observed->scsi_model, expected->scsi_model) == 0
        && strcmp(observed->scsi_revision, expected->scsi_revision) == 0;
}

bool gdox_usb_bot_location_matches(
    const gdox_usb_bot_location *expected,
    const gdox_usb_bot_location *observed
)
{
    if (expected == NULL || observed == NULL
        || expected->bus == 0U || observed->bus == 0U
        || expected->port_count > GDOX_USB_BOT_MAX_PORT_DEPTH
        || observed->port_count > GDOX_USB_BOT_MAX_PORT_DEPTH
        || expected->bus != observed->bus) {
        return false;
    }
    if (expected->port_count != 0U) {
        return expected->port_count == observed->port_count
            && memcmp(
                expected->ports,
                observed->ports,
                expected->port_count
            ) == 0;
    }
    return expected->address != 0U
        && expected->address == observed->address;
}

bool gdox_usb_bot_candidate_matches(
    gdox_usb_bot_identity requested,
    const gdox_usb_bot_location *expected_location,
    const gdox_usb_bot_observed_identity *observed_identity,
    const gdox_usb_bot_location *observed_location
)
{
    return gdox_usb_bot_identity_matches(requested, observed_identity)
        && (expected_location == NULL
            || gdox_usb_bot_location_matches(
                expected_location,
                observed_location
            ));
}
