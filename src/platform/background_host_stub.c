#include "platform/background_host.h"

#include <stddef.h>

gdox_background_host *gdox_background_host_create(void)
{
    return NULL;
}

gdox_background_host_event gdox_background_host_poll(
    gdox_background_host *host,
    bool background_only
)
{
    (void)host;
    (void)background_only;
    return GDOX_BACKGROUND_HOST_NONE;
}

void gdox_background_host_set_status(
    gdox_background_host *host,
    const char *status
)
{
    (void)host;
    (void)status;
}

void gdox_background_host_set_window_visible(
    gdox_background_host *host,
    bool visible
)
{
    (void)host;
    (void)visible;
}

void gdox_background_host_complete_shutdown(gdox_background_host *host)
{
    (void)host;
}

void gdox_background_host_destroy(gdox_background_host *host)
{
    (void)host;
}
