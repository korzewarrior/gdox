#ifndef GDOX_APP_XENIA_CONTENT_POLICY_H
#define GDOX_APP_XENIA_CONTENT_POLICY_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>

bool gdox_xenia_content_hexadecimal_name(
    const char *name,
    size_t characters
);

bool gdox_xenia_content_persistent_type(const char *name);

bool gdox_xenia_content_relative_path(
    char *output,
    size_t capacity,
    const char *parent,
    const char *child,
    gdox_error *error
);

void gdox_xenia_content_layout_error(
    const char *content_root,
    const char *relative,
    gdox_error *error
);

#endif
