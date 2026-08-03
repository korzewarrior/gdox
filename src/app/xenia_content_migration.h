#ifndef GDOX_APP_XENIA_CONTENT_MIGRATION_H
#define GDOX_APP_XENIA_CONTENT_MIGRATION_H

#include "gdox/error.h"

#include <stdbool.h>

/*
 * Removes validated non-save content types without following links. Unknown
 * or malformed entries remain untouched and make migration fail closed.
 */
bool gdox_xenia_content_migrate(
    const char *content_root,
    gdox_error *error
);

#endif
