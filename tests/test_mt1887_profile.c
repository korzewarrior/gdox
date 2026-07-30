#include "platform/mt1887_profile.h"

#include "gdox/optical.h"
#include "test.h"

#include <string.h>

int gdox_test_failures = 0;

static gdox_mt1887_state state_for(
    const gdox_mt1887_profile *profile,
    bool live
)
{
    static const uint8_t stock_capacity[3] = {0x03U, 0x1bU, 0x4fU};
    static const uint8_t live_capacity[3] = {0x3dU, 0x4dU, 0x4fU};
    static const uint8_t stock_geometry[3] = {0x03U, 0x1aU, 0xafU};
    static const uint8_t live_geometry[3] = {0x20U, 0x33U, 0xafU};
    gdox_mt1887_state state = {0};

    memcpy(
        state.capacity,
        live ? live_capacity : stock_capacity,
        sizeof(state.capacity)
    );
    memcpy(
        state.geometry,
        live ? live_geometry : stock_geometry,
        sizeof(state.geometry)
    );
    if (profile->auxiliary_present) {
        memcpy(
            state.auxiliary,
            profile->auxiliary,
            sizeof(state.auxiliary)
        );
    }
    state.last_lba = live ? 3820879U : 6991U;
    state.block_size = GDOX_LOGICAL_SECTOR_BYTES;
    return state;
}

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
    gdox_mt1887_state state;

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
    if (gp63 == NULL || gp65 == NULL) {
        return;
    }
    GDOX_TEST_CHECK(gp63->capacity_addresses[0] == 0x8538U);
    GDOX_TEST_CHECK(gp65->capacity_addresses[0] == 0x8a37U);
    GDOX_TEST_CHECK(gp65->geometry_addresses[0] == 0x8be2U);
    GDOX_TEST_CHECK(gp65->auxiliary_present);
    GDOX_TEST_CHECK(gp65->auxiliary_addresses[0] == 0x8538U);
    GDOX_TEST_CHECK(memcmp(
        gp65->auxiliary,
        (const uint8_t[]){0x64U, 0x00U, 0x64U},
        3U
    ) == 0);

    state = state_for(gp65, false);
    GDOX_TEST_CHECK(gdox_mt1887_state_is_known(gp65, &state));
    GDOX_TEST_CHECK(gdox_mt1887_state_is_stock(gp65, &state));
    state = state_for(gp65, true);
    GDOX_TEST_CHECK(gdox_mt1887_state_is_known(gp65, &state));
    GDOX_TEST_CHECK(gdox_mt1887_state_is_live(gp65, &state));

    state = state_for(gp65, false);
    memcpy(
        state.auxiliary,
        (const uint8_t[]){0x03U, 0x1bU, 0x4fU},
        3U
    );
    GDOX_TEST_CHECK(gdox_mt1887_state_is_known(gp65, &state));
    GDOX_TEST_CHECK(!gdox_mt1887_state_is_stock(gp65, &state));
    memcpy(
        state.auxiliary,
        (const uint8_t[]){0x3dU, 0x00U, 0x64U},
        3U
    );
    GDOX_TEST_CHECK(gdox_mt1887_state_is_known(gp65, &state));
    state.last_lba = UINT32_C(123456);
    GDOX_TEST_CHECK(gdox_mt1887_state_is_known(gp65, &state));
    state.block_size = UINT32_C(4096);
    GDOX_TEST_CHECK(gdox_mt1887_state_is_known(gp65, &state));
    state.auxiliary[0] = 0xffU;
    GDOX_TEST_CHECK(!gdox_mt1887_state_is_known(gp65, &state));

    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp63, true) == 128U);
    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp65, false) == 128U);
    GDOX_TEST_CHECK(gdox_mt1887_max_read_blocks(gp65, true) == 32U);
}

int main(void)
{
    run();
    return gdox_test_failures == 0 ? 0 : 1;
}
