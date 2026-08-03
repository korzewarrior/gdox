#ifndef GDOX_APP_TERMINATION_H
#define GDOX_APP_TERMINATION_H

#include "gdox/error.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool gdox_app_termination_install(gdox_error *error);
bool gdox_app_termination_requested(void);
void gdox_app_termination_uninstall(void);

#ifdef __cplusplus
}
#endif

#endif
