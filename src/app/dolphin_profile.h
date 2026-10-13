#ifndef GDOX_APP_DOLPHIN_PROFILE_H
#define GDOX_APP_DOLPHIN_PROFILE_H
#include "gdox/error.h"
/* Initialize a new managed profile without replacing existing settings. */
bool gdox_dolphin_profile_prepare(const char *directory, gdox_error *error);
#endif
