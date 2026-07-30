#include "platform/mt1887_profile.h"

#include "gdox/optical.h"

#include <stddef.h>
#include <string.h>

static const uint8_t stock_capacity[3] = {0x03U, 0x1bU, 0x4fU};
static const uint8_t live_capacity[3] = {0x3dU, 0x4dU, 0x4fU};
static const uint8_t stock_geometry[3] = {0x03U, 0x1aU, 0xafU};
static const uint8_t live_geometry[3] = {0x20U, 0x33U, 0xafU};

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

static bool triplet_is_known(
    const uint8_t observed[3],
    const uint8_t stock[3],
    const uint8_t live[3]
)
{
    size_t index;

    for (index = 0U; index < 3U; ++index) {
        if (observed[index] != stock[index]
            && observed[index] != live[index]) {
            return false;
        }
    }
    return true;
}

static bool auxiliary_is_known(
    const gdox_mt1887_profile *profile,
    const uint8_t observed[3]
)
{
    size_t index;

    if (!profile->auxiliary_present) {
        return true;
    }
    for (index = 0U; index < 3U; ++index) {
        if (observed[index] != profile->auxiliary[index]
            && observed[index] != stock_capacity[index]
            && observed[index] != live_capacity[index]) {
            return false;
        }
    }
    return true;
}

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

bool gdox_mt1887_state_is_known(
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_state *state
)
{
    return profile != NULL && state != NULL
        && triplet_is_known(state->capacity, stock_capacity, live_capacity)
        && triplet_is_known(state->geometry, stock_geometry, live_geometry)
        && auxiliary_is_known(profile, state->auxiliary);
}

bool gdox_mt1887_state_is_stock(
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_state *state
)
{
    return profile != NULL && state != NULL
        && memcmp(state->capacity, stock_capacity, 3U) == 0
        && memcmp(state->geometry, stock_geometry, 3U) == 0
        && (!profile->auxiliary_present
            || memcmp(state->auxiliary, profile->auxiliary, 3U) == 0)
        && state->last_lba == 6991U
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

bool gdox_mt1887_state_is_live(
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_state *state
)
{
    return profile != NULL && state != NULL
        && memcmp(state->capacity, live_capacity, 3U) == 0
        && memcmp(state->geometry, live_geometry, 3U) == 0
        && (!profile->auxiliary_present
            || memcmp(state->auxiliary, profile->auxiliary, 3U) == 0)
        && (uint64_t)state->last_lba + 1U == GDOX_XGD1_TOTAL_SECTORS
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES;
}

uint32_t gdox_mt1887_max_read_blocks(
    const gdox_mt1887_profile *profile,
    bool windows_transport
)
{
    return profile != NULL
        && profile->identity == GDOX_USB_BOT_GP65
        && windows_transport
        ? UINT32_C(32)
        : UINT32_C(128);
}
