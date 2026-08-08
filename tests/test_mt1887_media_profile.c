#include "platform/mt1887_media_profile.h"

#include "gdox/optical.h"
#include "gdox/sector.h"
#include "test.h"

#include <string.h>

int gdox_test_failures = 0;

static gdox_mt1887_state make_state(
    const gdox_mt1887_media_profile *media,
    const gdox_mt1887_profile *hardware,
    bool live
)
{
    gdox_mt1887_state state = {0};

    memcpy(
        state.capacity,
        live ? media->live_capacity : media->stock_capacity,
        sizeof(state.capacity)
    );
    memcpy(
        state.geometry,
        live ? media->live_geometry : media->stock_geometry,
        sizeof(state.geometry)
    );
    if (hardware->auxiliary_present) {
        memcpy(
            state.auxiliary,
            hardware->auxiliary,
            sizeof(state.auxiliary)
        );
    }
    state.last_lba = live ? media->live_last_lba : media->stock_last_lba;
    state.block_size = GDOX_LOGICAL_SECTOR_BYTES;
    return state;
}

static void run(void)
{
    static const uint8_t magic[20] = {
        'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
        'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
    };
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
    const gdox_mt1887_media_profile *xgd1 =
        gdox_mt1887_media_profile_xgd1();
    const gdox_mt1887_media_profile *xgd2_wave1 =
        gdox_mt1887_media_profile_gp63_xgd2_wave1();
    const gdox_mt1887_media_profile *xgd2_wave2 =
        gdox_mt1887_media_profile_gp63_xgd2_wave2();
    const gdox_mt1887_media_profile *xgd3 =
        gdox_mt1887_media_profile_gp63_xgd3();
    gdox_mt1887_state state;
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES] = {0};

    GDOX_TEST_CHECK(gp63 != NULL);
    GDOX_TEST_CHECK(gp65 != NULL);
    GDOX_TEST_CHECK(xgd1 != NULL);
    GDOX_TEST_CHECK(xgd2_wave1 != NULL);
    GDOX_TEST_CHECK(xgd2_wave2 != NULL);
    GDOX_TEST_CHECK(xgd3 != NULL);
    if (gp63 == NULL || gp65 == NULL || xgd1 == NULL
        || xgd2_wave1 == NULL || xgd2_wave2 == NULL || xgd3 == NULL) {
        return;
    }

    GDOX_TEST_CHECK(memcmp(
        xgd2_wave1->stock_capacity,
        (const uint8_t[]){0x03U, 0x0dU, 0xbeU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd2_wave1->live_capacity,
        (const uint8_t[]){0x3dU, 0x5fU, 0xdeU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd2_wave1->stock_geometry,
        (const uint8_t[]){0x03U, 0x0aU, 0x8fU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd2_wave1->live_geometry,
        (const uint8_t[]){0x20U, 0x33U, 0x9fU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(xgd2_wave1->stock_last_lba == UINT32_C(0x0dbe));
    GDOX_TEST_CHECK(xgd2_wave1->live_last_lba == UINT32_C(0x3a5fde));
    GDOX_TEST_CHECK(xgd2_wave1->live_sectors == UINT64_C(0x3a5fdf));
    GDOX_TEST_CHECK(xgd2_wave1->descriptor_lba == UINT32_C(0x1fb40));
    GDOX_TEST_CHECK(
        xgd2_wave1->game_partition_lba == GDOX_XGD2_GAME_PARTITION_LBA
    );

    GDOX_TEST_CHECK(memcmp(
        xgd2_wave2->stock_capacity,
        (const uint8_t[]){0x03U, 0x0aU, 0xa3U},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd2_wave2->live_capacity,
        (const uint8_t[]){0x3dU, 0x61U, 0x03U},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd2_wave2->stock_geometry,
        (const uint8_t[]){0x03U, 0x08U, 0x6fU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd2_wave2->live_geometry,
        (const uint8_t[]){0x20U, 0x33U, 0x9fU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(xgd2_wave2->stock_last_lba == UINT32_C(0x0aa3));
    GDOX_TEST_CHECK(xgd2_wave2->live_last_lba == UINT32_C(0x3a6103));
    GDOX_TEST_CHECK(xgd2_wave2->live_sectors == GDOX_XGD2_TOTAL_SECTORS);
    GDOX_TEST_CHECK(xgd2_wave2->descriptor_lba == UINT32_C(0x1fb40));
    GDOX_TEST_CHECK(
        xgd2_wave2->game_partition_lba == GDOX_XGD2_GAME_PARTITION_LBA
    );

    GDOX_TEST_CHECK(memcmp(
        xgd3->stock_capacity,
        (const uint8_t[]){0x03U, 0x61U, 0xe6U},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd3->live_capacity,
        (const uint8_t[]){0x44U, 0x1cU, 0x06U},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd3->stock_geometry,
        (const uint8_t[]){0x03U, 0x30U, 0xffU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(memcmp(
        xgd3->live_geometry,
        (const uint8_t[]){0x23U, 0x8eU, 0x0fU},
        3U
    ) == 0);
    GDOX_TEST_CHECK(xgd3->stock_last_lba == UINT32_C(0x61e6));
    GDOX_TEST_CHECK(xgd3->live_last_lba == UINT32_C(0x411c06));
    GDOX_TEST_CHECK(xgd3->live_sectors == UINT64_C(0x411c07));
    GDOX_TEST_CHECK(xgd3->descriptor_lba == UINT32_C(0x4120));
    GDOX_TEST_CHECK(
        xgd3->game_partition_lba == GDOX_GP63_XGD3_GAME_PARTITION_LBA
    );

    GDOX_TEST_CHECK(gdox_mt1887_media_profile_supports_hardware(xgd1, gp63));
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_supports_hardware(xgd1, gp65));
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_supports_hardware(
        xgd2_wave1, gp63
    ));
    GDOX_TEST_CHECK(!gdox_mt1887_media_profile_supports_hardware(
        xgd2_wave1, gp65
    ));
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_supports_hardware(
        xgd2_wave2, gp63
    ));
    GDOX_TEST_CHECK(!gdox_mt1887_media_profile_supports_hardware(
        xgd2_wave2, gp65
    ));
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_supports_hardware(xgd3, gp63));
    GDOX_TEST_CHECK(!gdox_mt1887_media_profile_supports_hardware(xgd3, gp65));

    state = make_state(xgd2_wave1, gp63, false);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_stock(gp63, &state)
        == xgd2_wave1);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_known(gp63, &state)
        == xgd2_wave1);
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(
        xgd2_wave2, gp63, &state
    ) == GDOX_MT1887_MEDIA_STATE_UNKNOWN);

    state = make_state(xgd2_wave2, gp63, false);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_stock(gp63, &state)
        == xgd2_wave2);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_known(gp63, &state)
        == xgd2_wave2);
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd1, gp63, &state)
        == GDOX_MT1887_MEDIA_STATE_UNKNOWN);

    state = make_state(xgd3, gp63, false);
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd3, gp63, &state)
        == GDOX_MT1887_MEDIA_STATE_STOCK);
    state = make_state(xgd3, gp63, true);
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd3, gp63, &state)
        == GDOX_MT1887_MEDIA_STATE_LIVE);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_known(gp63, &state)
        == xgd3);
    state.capacity[0] = xgd3->stock_capacity[0];
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd3, gp63, &state)
        == GDOX_MT1887_MEDIA_STATE_TRANSITION);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_known(gp63, &state)
        == xgd3);
    state.capacity[1] = xgd1->stock_capacity[1];
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd3, gp63, &state)
        == GDOX_MT1887_MEDIA_STATE_UNKNOWN);
    GDOX_TEST_CHECK(gdox_mt1887_media_profile_select_known(gp63, &state)
        == NULL);

    state = make_state(xgd1, gp65, false);
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd1, gp65, &state)
        == GDOX_MT1887_MEDIA_STATE_STOCK);
    memcpy(state.auxiliary, xgd1->stock_capacity, 3U);
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd1, gp65, &state)
        == GDOX_MT1887_MEDIA_STATE_TRANSITION);
    state.auxiliary[0] = 0xffU;
    GDOX_TEST_CHECK(gdox_mt1887_media_state_classify(xgd1, gp65, &state)
        == GDOX_MT1887_MEDIA_STATE_UNKNOWN);

    GDOX_TEST_CHECK(gdox_mt1887_media_stock_geometry_matches(
        xgd3,
        xgd3->stock_geometry
    ));
    GDOX_TEST_CHECK(!gdox_mt1887_media_stock_geometry_matches(
        xgd3,
        xgd3->live_geometry
    ));
    GDOX_TEST_CHECK(
        gdox_mt1887_media_profile_select_physical_geometry(
            gp63, xgd2_wave1->stock_geometry
        ) == xgd2_wave1
    );
    GDOX_TEST_CHECK(
        gdox_mt1887_media_profile_select_physical_geometry(
            gp63, xgd2_wave2->stock_geometry
        ) == xgd2_wave2
    );
    GDOX_TEST_CHECK(
        gdox_mt1887_media_profile_select_physical_geometry(
            gp63, xgd3->stock_geometry
        ) == xgd3
    );
    GDOX_TEST_CHECK(
        gdox_mt1887_media_profile_select_physical_geometry(
            gp65, xgd2_wave1->stock_geometry
        ) == NULL
    );
    GDOX_TEST_CHECK(
        gdox_mt1887_media_profile_select_physical_geometry(
            gp63, xgd3->live_geometry
        ) == NULL
    );
    memcpy(descriptor, magic, sizeof(magic));
    GDOX_TEST_CHECK(gdox_mt1887_media_descriptor_valid(
        xgd1,
        descriptor,
        sizeof(descriptor)
    ));
    GDOX_TEST_CHECK(!gdox_mt1887_media_descriptor_valid(
        xgd3,
        descriptor,
        sizeof(descriptor)
    ));
    memcpy(
        descriptor + sizeof(descriptor) - sizeof(magic),
        magic,
        sizeof(magic)
    );
    GDOX_TEST_CHECK(gdox_mt1887_media_descriptor_valid(
        xgd3,
        descriptor,
        sizeof(descriptor)
    ));
}

int main(void)
{
    run();
    return gdox_test_failures == 0 ? 0 : 1;
}
