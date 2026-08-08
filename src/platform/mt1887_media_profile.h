#ifndef GDOX_MT1887_MEDIA_PROFILE_H
#define GDOX_MT1887_MEDIA_PROFILE_H

#include "platform/mt1887_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum gdox_mt1887_media_kind {
    GDOX_MT1887_MEDIA_XGD1 = 0,
    GDOX_MT1887_MEDIA_GP63_XGD2,
    GDOX_MT1887_MEDIA_GP63_XGD3
} gdox_mt1887_media_kind;

typedef enum gdox_mt1887_media_state_class {
    GDOX_MT1887_MEDIA_STATE_UNKNOWN = 0,
    GDOX_MT1887_MEDIA_STATE_STOCK,
    GDOX_MT1887_MEDIA_STATE_LIVE,
    GDOX_MT1887_MEDIA_STATE_TRANSITION
} gdox_mt1887_media_state_class;

typedef struct gdox_mt1887_state {
    uint8_t capacity[3];
    uint8_t geometry[3];
    uint8_t auxiliary[3];
    uint32_t last_lba;
    uint32_t block_size;
} gdox_mt1887_state;

typedef struct gdox_mt1887_media_profile {
    gdox_mt1887_media_kind kind;
    uint8_t stock_capacity[3];
    uint8_t live_capacity[3];
    uint8_t stock_geometry[3];
    uint8_t live_geometry[3];
    uint32_t stock_last_lba;
    uint32_t live_last_lba;
    uint32_t descriptor_lba;
    uint64_t live_sectors;
    uint64_t game_partition_lba;
} gdox_mt1887_media_profile;

const gdox_mt1887_media_profile *gdox_mt1887_media_profile_xgd1(void);
const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_gp63_xgd2_wave1(void);
const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_gp63_xgd2_wave2(void);
const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_gp63_xgd3(void);

const gdox_mt1887_media_profile *gdox_mt1887_media_profile_select_stock(
    const gdox_mt1887_profile *hardware,
    const gdox_mt1887_state *state
);
const gdox_mt1887_media_profile *gdox_mt1887_media_profile_select_known(
    const gdox_mt1887_profile *hardware,
    const gdox_mt1887_state *state
);
const gdox_mt1887_media_profile *
gdox_mt1887_media_profile_select_physical_geometry(
    const gdox_mt1887_profile *hardware,
    const uint8_t geometry[3]
);

bool gdox_mt1887_media_profile_supports_hardware(
    const gdox_mt1887_media_profile *media,
    const gdox_mt1887_profile *hardware
);
gdox_mt1887_media_state_class gdox_mt1887_media_state_classify(
    const gdox_mt1887_media_profile *media,
    const gdox_mt1887_profile *hardware,
    const gdox_mt1887_state *state
);
bool gdox_mt1887_media_stock_geometry_matches(
    const gdox_mt1887_media_profile *media,
    const uint8_t geometry[3]
);
bool gdox_mt1887_media_descriptor_valid(
    const gdox_mt1887_media_profile *media,
    const uint8_t *sector,
    size_t sector_bytes
);

#endif
