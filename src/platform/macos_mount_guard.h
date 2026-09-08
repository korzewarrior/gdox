#ifndef GDOX_MACOS_MOUNT_GUARD_H
#define GDOX_MACOS_MOUNT_GUARD_H

#include "gdox/optical.h"

typedef struct gdox_macos_mount_guard_entry {
    uint64_t registry_id;
    uint32_t references;
} gdox_macos_mount_guard_entry;

/* The native DiskArbitration caller serializes access with its mutex. */
typedef struct gdox_macos_mount_guards {
    gdox_macos_mount_guard_entry entries[GDOX_OPTICAL_MAX_DEVICES];
} gdox_macos_mount_guards;

static inline bool gdox_macos_mount_guard_acquire(
    gdox_macos_mount_guards *guards, uint64_t registry_id
)
{
    gdox_macos_mount_guard_entry *available = NULL;
    if (registry_id == 0U) return false;
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        gdox_macos_mount_guard_entry *entry = &guards->entries[index];
        if (entry->registry_id == registry_id) {
            if (entry->references == UINT32_MAX) return false;
            ++entry->references;
            return true;
        }
        if (available == NULL && entry->references == 0U) available = entry;
    }
    if (available == NULL) return false;
    available->registry_id = registry_id;
    available->references = 1U;
    return true;
}

static inline void gdox_macos_mount_guard_release(
    gdox_macos_mount_guards *guards, uint64_t registry_id
)
{
    if (registry_id == 0U) return;
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        gdox_macos_mount_guard_entry *entry = &guards->entries[index];
        if (entry->registry_id == registry_id && entry->references != 0U) {
            if (--entry->references == 0U) entry->registry_id = 0U;
            return;
        }
    }
}

static inline bool gdox_macos_mount_guard_contains(
    const gdox_macos_mount_guards *guards, uint64_t registry_id
)
{
    if (registry_id == 0U) return false;
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        if (guards->entries[index].registry_id == registry_id
            && guards->entries[index].references != 0U) return true;
    }
    return false;
}

#endif
