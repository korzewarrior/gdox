#ifndef GDOX_APP_BACKGROUND_H
#define GDOX_APP_BACKGROUND_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_app_background_event {
    GDOX_APP_BACKGROUND_NONE = 0,
    GDOX_APP_BACKGROUND_OPEN,
    GDOX_APP_BACKGROUND_QUIT,
    GDOX_APP_BACKGROUND_AVAILABLE,
    GDOX_APP_BACKGROUND_UNAVAILABLE,
} gdox_app_background_event;

typedef struct gdox_app_background gdox_app_background;
typedef struct gdox_app_instance gdox_app_instance;

gdox_app_background *gdox_app_background_create(void);
gdox_app_background_event gdox_app_background_poll(
    gdox_app_background *background,
    bool background_only
);
void gdox_app_background_set_status(
    gdox_app_background *background,
    const char *status
);
void gdox_app_background_set_window_visible(
    gdox_app_background *background,
    bool visible
);
#if defined(_WIN32)
typedef void (*gdox_app_background_shutdown_handler)(void *context);
void gdox_app_background_set_shutdown_handler(
    gdox_app_background *background,
    gdox_app_background_shutdown_handler handler,
    void *context
);
#endif
void gdox_app_background_complete_shutdown(gdox_app_background *background);
void gdox_app_background_destroy(gdox_app_background *background);

gdox_app_instance *gdox_app_instance_acquire(bool *already_running);
bool gdox_app_instance_activate_existing(void);
bool gdox_app_instance_take_activation(gdox_app_instance *instance);
void gdox_app_instance_report_conflict(void);
void gdox_app_instance_report_failure(void);
void gdox_app_instance_release(gdox_app_instance *instance);

#ifdef __cplusplus
}
#endif

#endif
