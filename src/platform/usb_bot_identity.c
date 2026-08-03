#include "platform/usb_bot_identity.h"

#include "gdox/optical.h"

#include <string.h>

static const gdox_usb_bot_identity_spec identities[] = {
    {
        GDOX_USB_BOT_GP63,
        GDOX_GP63_USB_VENDOR_ID,
        GDOX_GP63_USB_PRODUCT_ID,
        GDOX_GP63_SCSI_VENDOR,
        GDOX_GP63_SCSI_MODEL,
        GDOX_GP63_SCSI_REVISION,
    },
    {
        GDOX_USB_BOT_GP65,
        GDOX_GP65_USB_VENDOR_ID,
        GDOX_GP65_USB_PRODUCT_ID,
        GDOX_GP65_SCSI_VENDOR,
        GDOX_GP65_SCSI_MODEL,
        GDOX_GP65_SCSI_REVISION,
    },
    {
        GDOX_USB_BOT_GP08,
        GDOX_GP08_USB_VENDOR_ID,
        GDOX_GP08_USB_PRODUCT_ID,
        GDOX_GP08_SCSI_VENDOR,
        GDOX_GP08_SCSI_MODEL,
        GDOX_GP08_SCSI_REVISION,
    },
    {
        GDOX_USB_BOT_ASUS_NR09,
        GDOX_ASUS_USB_VENDOR_ID,
        GDOX_ASUS_USB_PRODUCT_ID,
        GDOX_ASUS_SCSI_VENDOR,
        GDOX_ASUS_SCSI_MODEL,
        GDOX_ASUS_SCSI_REVISION,
    },
};

_Static_assert(
    sizeof(identities) / sizeof(identities[0])
        == GDOX_USB_BOT_IDENTITY_COUNT,
    "USB BOT identity registry must cover every identity"
);

const gdox_usb_bot_identity_spec *gdox_usb_bot_identity_get(
    gdox_usb_bot_identity identity
)
{
    if ((unsigned int)identity < GDOX_USB_BOT_IDENTITY_COUNT) {
        return &identities[(size_t)identity];
    }
    return NULL;
}

bool gdox_usb_bot_recovery_identity(
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_usb_bot_identity *identity
)
{
    size_t index;

    if (identity == NULL) {
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        if (identities[index].vendor_id == vendor_id
            && identities[index].product_id == product_id) {
            *identity = identities[index].identity;
            return true;
        }
    }
    return false;
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
