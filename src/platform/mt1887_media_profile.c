#include "platform/mt1887_media_profile.h"

#include "gdox/optical.h"
#include "gdox/sector.h"

#include <stddef.h>
#include <string.h>

static const uint8_t gdfx_magic[20] = {
    'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
    'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A',
};

#define GDOX_GP63_XGD2_WAVE1_TOTAL_SECTORS UINT64_C(0x3a5fdf)

static const gdox_mt1887_media_profile xgd1 = {
    GDOX_MT1887_MEDIA_XGD1,
    {0x03U, 0x1bU, 0x4fU},
    {0x3dU, 0x4dU, 0x4fU},
    {0x03U, 0x1aU, 0xafU},
    {0x20U, 0x33U, 0xafU},
    UINT32_C(6991),
    UINT32_C(3820879),
    UINT32_C(0x30620),
    GDOX_XGD1_TOTAL_SECTORS,
    UINT64_C(0),
};

/*
 * XGD2 Wave 1 uses 0xa90 stock layer-zero video sectors and 0x32f stock
 * layer-one video sectors. The unlocked view retains the standard XGD2 layer
 * break and exposes host LBAs 0..0x3a5fde. The game volume starts at 0x1fb20.
 */
static const gdox_mt1887_media_profile gp63_xgd2_wave1 = {
    GDOX_MT1887_MEDIA_GP63_XGD2,
    {0x03U, 0x0dU, 0xbeU},
    {0x3dU, 0x5fU, 0xdeU},
    {0x03U, 0x0aU, 0x8fU},
    {0x20U, 0x33U, 0x9fU},
    UINT32_C(0x0dbe),
    UINT32_C(0x3a5fde),
    UINT32_C(0x1fb40),
    GDOX_GP63_XGD2_WAVE1_TOTAL_SECTORS,
    GDOX_XGD2_GAME_PARTITION_LBA,
};

/*
 * XGD2 Wave 2 uses 0x870 stock layer-zero video sectors and 0x234 stock
 * layer-one video sectors. The unlocked view retains the standard XGD2 layer
 * break and exposes host LBAs 0..0x3a6103.
 */
static const gdox_mt1887_media_profile gp63_xgd2_wave2 = {
    GDOX_MT1887_MEDIA_GP63_XGD2,
    {0x03U, 0x0aU, 0xa3U},
    {0x3dU, 0x61U, 0x03U},
    {0x03U, 0x08U, 0x6fU},
    {0x20U, 0x33U, 0x9fU},
    UINT32_C(0x0aa3),
    UINT32_C(0x3a6103),
    UINT32_C(0x1fb40),
    GDOX_XGD2_TOTAL_SECTORS,
    GDOX_XGD2_GAME_PARTITION_LBA,
};

static const gdox_mt1887_media_profile gp63_xgd3 = {
    GDOX_MT1887_MEDIA_GP63_XGD3,
    {0x03U, 0x61U, 0xe6U},
    {0x44U, 0x1cU, 0x06U},
    {0x03U, 0x30U, 0xffU},
    {0x23U, 0x8eU, 0x0fU},
    UINT32_C(0x61e6),
    UINT32_C(0x411c06),
    UINT32_C(0x4120),
    UINT64_C(0x411c07),
    GDOX_GP63_XGD3_GAME_PARTITION_LBA,
};

_Static_assert(
    UINT64_C(0x3a5fde) + 1U == GDOX_GP63_XGD2_WAVE1_TOTAL_SECTORS,
    "XGD2 Wave 1 media profile sector count must match its live last LBA"
);
_Static_assert(
    UINT64_C(0x3a6103) + 1U == GDOX_XGD2_TOTAL_SECTORS,
    "XGD2 Wave 2 media profile sector count must match its live last LBA"
);
_Static_assert(
    GDOX_XGD2_GAME_PARTITION_LBA + 32U == UINT64_C(0x1fb40),
    "XGD2 descriptor must be sector 32 of the game partition"
);
_Static_assert(
    UINT64_C(3820879) + 1U == GDOX_XGD1_TOTAL_SECTORS,
    "XGD1 media profile sector count must match its live last LBA"
);
_Static_assert(
    UINT64_C(0x411c06) + 1U == UINT64_C(0x411c07),
    "XGD3 media profile sector count must match its live last LBA"
);
_Static_assert(
    GDOX_GP63_XGD3_GAME_PARTITION_LBA + 32U == UINT64_C(0x4120),
    "XGD3 descriptor must be sector 32 of the game partition"
);

static const gdox_mt1887_media_profile *const media_profiles[] = {
    &xgd1,
    &gp63_xgd2_wave1,
    &gp63_xgd2_wave2,
    &gp63_xgd3,
};

static bool triplet_is_transition(
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

static bool auxiliary_is_transition(
    const gdox_mt1887_media_profile *media,
    const gdox_mt1887_profile *hardware,
    const uint8_t observed[3]
)
{
    size_t index;

    if (!hardware->auxiliary_present) {
        return true;
    }
    for (index = 0U; index < 3U; ++index) {
        if (observed[index] != hardware->auxiliary[index]
            && observed[index] != media->stock_capacity[index]
            && observed[index] != media->live_capacity[index]) {
            return false;
        }
    }
    return true;
}

const gdox_mt1887_media_profile *gdox_mt1887_media_profile_xgd1(void)
{
    return &xgd1;
}

const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_gp63_xgd2_wave1(void)
{
    return &gp63_xgd2_wave1;
}

const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_gp63_xgd2_wave2(void)
{
    return &gp63_xgd2_wave2;
}

const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_gp63_xgd3(void)
{
    return &gp63_xgd3;
}

bool gdox_mt1887_media_profile_supports_hardware(
    const gdox_mt1887_media_profile *media,
    const gdox_mt1887_profile *hardware
)
{
    if (media == NULL || hardware == NULL) {
        return false;
    }
    if (media->kind == GDOX_MT1887_MEDIA_XGD1) {
        return hardware->identity == GDOX_USB_BOT_GP63
            || hardware->identity == GDOX_USB_BOT_GP65;
    }
    return (media->kind == GDOX_MT1887_MEDIA_GP63_XGD2
            || media->kind == GDOX_MT1887_MEDIA_GP63_XGD3)
        && hardware->identity == GDOX_USB_BOT_GP63;
}

const gdox_mt1887_media_profile *gdox_mt1887_media_profile_select_stock(
    const gdox_mt1887_profile *hardware,
    const gdox_mt1887_state *state
)
{
    size_t index;

    for (index = 0U;
         index < sizeof(media_profiles) / sizeof(media_profiles[0]);
         ++index) {
        const gdox_mt1887_media_profile *media = media_profiles[index];

        if (gdox_mt1887_media_profile_supports_hardware(media, hardware)
            && gdox_mt1887_media_state_classify(media, hardware, state)
                == GDOX_MT1887_MEDIA_STATE_STOCK) {
            return media;
        }
    }
    return NULL;
}

const gdox_mt1887_media_profile *gdox_mt1887_media_profile_select_known(
    const gdox_mt1887_profile *hardware,
    const gdox_mt1887_state *state
)
{
    const gdox_mt1887_media_profile *selected = NULL;
    size_t index;

    for (index = 0U;
         index < sizeof(media_profiles) / sizeof(media_profiles[0]);
         ++index) {
        const gdox_mt1887_media_profile *media = media_profiles[index];

        if (gdox_mt1887_media_state_classify(media, hardware, state)
            != GDOX_MT1887_MEDIA_STATE_UNKNOWN) {
            if (selected != NULL) {
                return NULL;
            }
            selected = media;
        }
    }
    return selected;
}

const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_select_physical_geometry(
    const gdox_mt1887_profile *hardware,
    const uint8_t geometry[3]
)
{
    const gdox_mt1887_media_profile *selected = NULL;
    size_t index;

    if (hardware == NULL || geometry == NULL) {
        return NULL;
    }
    for (index = 0U;
         index < sizeof(media_profiles) / sizeof(media_profiles[0]);
         ++index) {
        const gdox_mt1887_media_profile *media = media_profiles[index];

        if (gdox_mt1887_media_profile_supports_hardware(media, hardware)
            && gdox_mt1887_media_stock_geometry_matches(media, geometry)) {
            if (selected != NULL) {
                return NULL;
            }
            selected = media;
        }
    }
    return selected;
}

gdox_mt1887_media_state_class gdox_mt1887_media_state_classify(
    const gdox_mt1887_media_profile *media,
    const gdox_mt1887_profile *hardware,
    const gdox_mt1887_state *state
)
{
    if (state == NULL
        || !gdox_mt1887_media_profile_supports_hardware(media, hardware)) {
        return GDOX_MT1887_MEDIA_STATE_UNKNOWN;
    }
    if (memcmp(state->capacity, media->stock_capacity, 3U) == 0
        && memcmp(state->geometry, media->stock_geometry, 3U) == 0
        && (!hardware->auxiliary_present
            || memcmp(state->auxiliary, hardware->auxiliary, 3U) == 0)
        && state->last_lba == media->stock_last_lba
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES) {
        return GDOX_MT1887_MEDIA_STATE_STOCK;
    }
    if (memcmp(state->capacity, media->live_capacity, 3U) == 0
        && memcmp(state->geometry, media->live_geometry, 3U) == 0
        && (!hardware->auxiliary_present
            || memcmp(state->auxiliary, hardware->auxiliary, 3U) == 0)
        && state->last_lba == media->live_last_lba
        && state->block_size == GDOX_LOGICAL_SECTOR_BYTES) {
        return GDOX_MT1887_MEDIA_STATE_LIVE;
    }
    if (triplet_is_transition(
            state->capacity,
            media->stock_capacity,
            media->live_capacity
        )
        && triplet_is_transition(
            state->geometry,
            media->stock_geometry,
            media->live_geometry
        )
        && auxiliary_is_transition(media, hardware, state->auxiliary)) {
        return GDOX_MT1887_MEDIA_STATE_TRANSITION;
    }
    return GDOX_MT1887_MEDIA_STATE_UNKNOWN;
}

bool gdox_mt1887_media_stock_geometry_matches(
    const gdox_mt1887_media_profile *media,
    const uint8_t geometry[3]
)
{
    return media != NULL && geometry != NULL
        && memcmp(geometry, media->stock_geometry, 3U) == 0;
}

bool gdox_mt1887_media_descriptor_valid(
    const gdox_mt1887_media_profile *media,
    const uint8_t *sector,
    size_t sector_bytes
)
{
    if (media == NULL || sector == NULL
        || sector_bytes != GDOX_LOGICAL_SECTOR_BYTES
        || memcmp(sector, gdfx_magic, sizeof(gdfx_magic)) != 0) {
        return false;
    }
    return media->kind == GDOX_MT1887_MEDIA_XGD1
        || memcmp(
            sector + sector_bytes - sizeof(gdfx_magic),
            gdfx_magic,
            sizeof(gdfx_magic)
        ) == 0;
}
