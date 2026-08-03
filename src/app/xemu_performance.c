#include "app/xemu_performance.h"

uint8_t gdox_xemu_effective_resolution_scale(
    gdox_host_profile host_profile,
    uint8_t requested_scale
)
{
    return host_profile == GDOX_HOST_PROFILE_HANDHELD
        ? 1U
        : requested_scale;
}
