#ifndef GDOX_CORE_HDD_CACHE_H
#define GDOX_CORE_HDD_CACHE_H

#include "gdox/error.h"

#include <stdbool.h>

/*
 * Restore the managed Xbox HDD's X, Y, and Z filesystem metadata to the
 * empty state. Game saves and dashboard data live outside these partitions.
 */
bool gdox_hdd_reset_cache_partitions(
    const char *path,
    bool *changed,
    gdox_error *error
);

#endif
