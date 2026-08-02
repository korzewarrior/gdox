#ifndef GDOX_PREFERENCES_H
#define GDOX_PREFERENCES_H

#include "app/model.h"

#include "gdox/error.h"

#include <stdbool.h>

typedef gdox_app_settings gdox_preferences;

void gdox_preferences_defaults(gdox_preferences *preferences);
bool gdox_preferences_load(gdox_preferences *preferences, gdox_error *error);
bool gdox_preferences_save(
    const gdox_preferences *preferences,
    gdox_error *error
);

#endif
