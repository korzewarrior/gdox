#ifndef GDOX_PLATFORM_BACKGROUND_HOST_H
#define GDOX_PLATFORM_BACKGROUND_HOST_H

#include <stdbool.h>
#if defined(_WIN32)
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_background_host_event {
    GDOX_BACKGROUND_HOST_NONE = 0,
    GDOX_BACKGROUND_HOST_OPEN,
    GDOX_BACKGROUND_HOST_QUIT,
    GDOX_BACKGROUND_HOST_AVAILABLE,
    GDOX_BACKGROUND_HOST_UNAVAILABLE,
} gdox_background_host_event;

typedef struct gdox_background_host gdox_background_host;

#if defined(_WIN32)
typedef void (*gdox_background_host_shutdown_handler)(void *context);

void gdox_background_host_set_shutdown_handler(
    gdox_background_host *host,
    gdox_background_host_shutdown_handler handler,
    void *context
);
#endif

#if defined(_WIN32)
/* Converts a confirmed Windows end-session message into the quit event. */
gdox_background_host_event gdox_background_host_windows_session_event(
    unsigned int message,
    uintptr_t parameter
);
#endif

/*
 * Creates the native notification-area host. A null result means the current
 * desktop cannot provide a persistent, user-reachable background entry.
 */
gdox_background_host *gdox_background_host_create(void);
gdox_background_host_event gdox_background_host_poll(
    gdox_background_host *host,
    bool background_only
);
void gdox_background_host_set_status(
    gdox_background_host *host,
    const char *status
);
void gdox_background_host_set_window_visible(
    gdox_background_host *host,
    bool visible
);
void gdox_background_host_complete_shutdown(gdox_background_host *host);
void gdox_background_host_destroy(gdox_background_host *host);

#ifdef __cplusplus
}
#endif

#endif
