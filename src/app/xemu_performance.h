#ifndef GDOX_APP_XEMU_PERFORMANCE_H
#define GDOX_APP_XEMU_PERFORMANCE_H

#include "app/model.h"

#include <stdint.h>

uint8_t gdox_xemu_effective_resolution_scale(
    gdox_host_profile host_profile,
    uint8_t requested_scale
);

#endif
