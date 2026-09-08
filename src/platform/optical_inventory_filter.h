#ifndef GDOX_OPTICAL_INVENTORY_FILTER_H
#define GDOX_OPTICAL_INVENTORY_FILTER_H

#include "gdox/optical.h"

#include <string.h>

static inline bool gdox_optical_media_query_valid(
    const gdox_optical_media_query *query, gdox_error *error
)
{
    if (query == NULL || (query->excluded_device_count != 0U
            && query->excluded_device_ids == NULL)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "optical media query options and excluded device IDs are required");
        return false;
    }
    for (size_t index = 0U; index < query->excluded_device_count; ++index) {
        const char *id = query->excluded_device_ids[index];
        if (id == NULL || id[0] == '\0'
            || strlen(id) >= GDOX_OPTICAL_DEVICE_ID_CAPACITY) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
                "optical media query exclusions require valid physical device IDs");
            return false;
        }
    }
    return true;
}

static inline bool gdox_optical_media_query_allows(
    const gdox_optical_media_query *query, const char *id
)
{
    if (!query->enabled) {
        return false;
    }
    for (size_t index = 0U; index < query->excluded_device_count; ++index) {
        if (strcmp(id, query->excluded_device_ids[index]) == 0) {
            return false;
        }
    }
    return true;
}

#endif
