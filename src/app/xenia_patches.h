#ifndef GDOX_APP_XENIA_PATCHES_H
#define GDOX_APP_XENIA_PATCHES_H

#include "gdox/error.h"
#include "gdox/xenia_policy.h"

#include <stdbool.h>

bool gdox_xenia_provision_patches(
    const char *storage_root,
    gdox_xenia_patch_set patch_set,
    gdox_error *error
);

#endif
