#ifndef GDOX_PLATFORM_INSTANCE_GUARD_H
#define GDOX_PLATFORM_INSTANCE_GUARD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_instance_guard gdox_instance_guard;

gdox_instance_guard *gdox_instance_guard_acquire(bool *already_running);
bool gdox_instance_guard_activate_existing(void);
bool gdox_instance_guard_take_activation(gdox_instance_guard *guard);
void gdox_instance_guard_report_conflict(void);
void gdox_instance_guard_report_failure(void);
void gdox_instance_guard_release(gdox_instance_guard *guard);

#ifdef __cplusplus
}
#endif

#endif
