#include "app/background.h"

#include "platform/background_host.h"
#include "platform/instance_guard.h"

#include <stdlib.h>

struct gdox_app_background {
    gdox_background_host *host;
};

struct gdox_app_instance {
    gdox_instance_guard *guard;
};

gdox_app_background *gdox_app_background_create(void)
{
    gdox_app_background *background = calloc(1U, sizeof(*background));

    if (background == NULL) {
        return NULL;
    }
    background->host = gdox_background_host_create();
    if (background->host == NULL) {
        free(background);
        return NULL;
    }
    return background;
}

gdox_app_background_event gdox_app_background_poll(
    gdox_app_background *background,
    bool background_only
)
{
    if (background == NULL) {
        return GDOX_APP_BACKGROUND_NONE;
    }
    switch (gdox_background_host_poll(background->host, background_only)) {
        case GDOX_BACKGROUND_HOST_NONE:
            return GDOX_APP_BACKGROUND_NONE;
        case GDOX_BACKGROUND_HOST_OPEN:
            return GDOX_APP_BACKGROUND_OPEN;
        case GDOX_BACKGROUND_HOST_QUIT:
            return GDOX_APP_BACKGROUND_QUIT;
        case GDOX_BACKGROUND_HOST_AVAILABLE:
            return GDOX_APP_BACKGROUND_AVAILABLE;
        case GDOX_BACKGROUND_HOST_UNAVAILABLE:
            return GDOX_APP_BACKGROUND_UNAVAILABLE;
    }
    return GDOX_APP_BACKGROUND_NONE;
}

void gdox_app_background_set_status(
    gdox_app_background *background,
    const char *status
)
{
    if (background != NULL) {
        gdox_background_host_set_status(background->host, status);
    }
}

void gdox_app_background_set_window_visible(
    gdox_app_background *background,
    bool visible
)
{
    if (background != NULL) {
        gdox_background_host_set_window_visible(background->host, visible);
    }
}

#if defined(_WIN32)
void gdox_app_background_set_shutdown_handler(
    gdox_app_background *background,
    gdox_app_background_shutdown_handler handler,
    void *context
)
{
    if (background != NULL) {
        gdox_background_host_set_shutdown_handler(
            background->host, handler, context
        );
    }
}
#endif

void gdox_app_background_complete_shutdown(gdox_app_background *background)
{
    if (background != NULL) {
        gdox_background_host_complete_shutdown(background->host);
    }
}

void gdox_app_background_destroy(gdox_app_background *background)
{
    if (background == NULL) {
        return;
    }
    gdox_background_host_destroy(background->host);
    free(background);
}

gdox_app_instance *gdox_app_instance_acquire(bool *already_running)
{
    gdox_app_instance *instance = calloc(1U, sizeof(*instance));

    if (instance == NULL) {
        if (already_running != NULL) {
            *already_running = false;
        }
        return NULL;
    }
    instance->guard = gdox_instance_guard_acquire(already_running);
    if (instance->guard == NULL) {
        free(instance);
        return NULL;
    }
    return instance;
}

bool gdox_app_instance_activate_existing(void)
{
    return gdox_instance_guard_activate_existing();
}

bool gdox_app_instance_take_activation(gdox_app_instance *instance)
{
    return instance != NULL
        && gdox_instance_guard_take_activation(instance->guard);
}

void gdox_app_instance_report_conflict(void)
{
    gdox_instance_guard_report_conflict();
}

void gdox_app_instance_report_failure(void)
{
    gdox_instance_guard_report_failure();
}

void gdox_app_instance_release(gdox_app_instance *instance)
{
    if (instance == NULL) {
        return;
    }
    gdox_instance_guard_release(instance->guard);
    free(instance);
}
