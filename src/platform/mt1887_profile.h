#ifndef GDOX_MT1887_PROFILE_H
#define GDOX_MT1887_PROFILE_H

#include "platform/usb_bot.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct gdox_mt1887_profile {
    gdox_usb_bot_identity identity;
    const char *vendor;
    const char *model;
    const char *revision;
    uint16_t capacity_addresses[3];
    uint16_t geometry_addresses[3];
    bool auxiliary_present;
    uint16_t auxiliary_addresses[3];
    uint8_t auxiliary[3];
} gdox_mt1887_profile;

typedef struct gdox_mt1887_state {
    uint8_t capacity[3];
    uint8_t geometry[3];
    uint8_t auxiliary[3];
    uint32_t last_lba;
    uint32_t block_size;
} gdox_mt1887_state;

const gdox_mt1887_profile *gdox_mt1887_profile_find(
    gdox_usb_bot_identity identity,
    const char *vendor,
    const char *model,
    const char *revision
);
bool gdox_mt1887_state_is_known(
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_state *state
);
bool gdox_mt1887_state_is_stock(
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_state *state
);
bool gdox_mt1887_state_is_live(
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_state *state
);
uint32_t gdox_mt1887_max_read_blocks(
    const gdox_mt1887_profile *profile,
    bool windows_transport
);

#endif
