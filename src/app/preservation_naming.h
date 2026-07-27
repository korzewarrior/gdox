#ifndef GDOX_PRESERVATION_NAMING_H
#define GDOX_PRESERVATION_NAMING_H

#include "gdox/preserve.h"

#include <stdbool.h>
#include <stddef.h>

#define GDOX_PRESERVATION_FILENAME_CAPACITY 256U

#ifdef __cplusplus
extern "C" {
#endif

bool gdox_preservation_suggest_filename(
    const char *title,
    gdox_preservation_format format,
    char *output,
    size_t output_capacity
);

#ifdef __cplusplus
}
#endif

#endif
