#include "platform/mt1887_profile.h"

#include "gdox/optical.h"

#include <string.h>

static const gdox_mt1887_profile gp63 = {
    GDOX_USB_BOT_GP63,
    GDOX_GP63_SCSI_VENDOR,
    GDOX_GP63_SCSI_MODEL,
    GDOX_GP63_SCSI_REVISION,
    {0x8538U, 0x8539U, 0x853aU},
    {0x8be2U, 0x8be3U, 0x8be4U},
    false,
    {0U, 0U, 0U},
    {0U, 0U, 0U},
};

static const gdox_mt1887_profile gp65 = {
    GDOX_USB_BOT_GP65,
    GDOX_GP65_SCSI_VENDOR,
    GDOX_GP65_SCSI_MODEL,
    GDOX_GP65_SCSI_REVISION,
    {0x8a37U, 0x8a38U, 0x8a39U},
    {0x8be2U, 0x8be3U, 0x8be4U},
    true,
    {0x8538U, 0x8539U, 0x853aU},
    {0x64U, 0x00U, 0x64U},
};

const gdox_mt1887_profile *gdox_mt1887_profile_find(
    gdox_usb_bot_identity identity,
    const char *vendor,
    const char *model,
    const char *revision
)
{
    const gdox_mt1887_profile *profile;

    if (identity == GDOX_USB_BOT_GP63) {
        profile = &gp63;
    } else if (identity == GDOX_USB_BOT_GP65) {
        profile = &gp65;
    } else {
        return NULL;
    }
    return vendor != NULL && model != NULL && revision != NULL
        && strcmp(vendor, profile->vendor) == 0
        && strcmp(model, profile->model) == 0
        && strcmp(revision, profile->revision) == 0
        ? profile
        : NULL;
}

uint32_t gdox_mt1887_max_read_blocks(
    const gdox_mt1887_profile *profile,
    bool windows_transport
)
{
    if (profile == NULL || !windows_transport) {
        return UINT32_C(128);
    }
    if (profile->identity == GDOX_USB_BOT_GP63) {
        return UINT32_C(32);
    }
    if (profile->identity == GDOX_USB_BOT_GP65) {
        return UINT32_C(32);
    }
    return UINT32_C(128);
}
