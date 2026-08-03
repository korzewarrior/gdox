#ifndef GDOX_CORE_XENIA_LAUNCH_H
#define GDOX_CORE_XENIA_LAUNCH_H

#include "gdox/xenia.h"

#include <stddef.h>

#define GDOX_XENIA_MAX_ARGUMENTS 48U

typedef struct gdox_xenia_launch_plan {
    char module[128];
    char storage[GDOX_EMULATOR_PATH_CAPACITY + 32U];
    char content[GDOX_EMULATOR_PATH_CAPACITY + 32U];
    char cache[GDOX_EMULATOR_PATH_CAPACITY + 32U];
    char log[GDOX_EMULATOR_PATH_CAPACITY + 32U];
    char queued_frames[64];
    char framerate_limit[64];
    char occlusion_query_saturation[64];
    char display_resolution_x[64];
    char display_resolution_y[64];
    char gdox_disc[GDOX_EMULATOR_PATH_CAPACITY + 32U];
    char gdox_disc_length[64];
    const char *arguments[GDOX_XENIA_MAX_ARGUMENTS];
    size_t count;
} gdox_xenia_launch_plan;

bool gdox_xenia_build_target_launch_plan(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
);

bool gdox_xenia_build_launch_plan(
    const gdox_xenia_options *options,
    const char *disc_path,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
);

#endif
