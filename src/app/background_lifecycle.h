#ifndef GDOX_APP_BACKGROUND_LIFECYCLE_H
#define GDOX_APP_BACKGROUND_LIFECYCLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_background_state {
    GDOX_BACKGROUND_VISIBLE = 0,
    GDOX_BACKGROUND_HIDDEN,
    GDOX_BACKGROUND_STOPPING,
} gdox_background_state;

typedef enum gdox_background_action {
    GDOX_BACKGROUND_NO_ACTION = 0,
    GDOX_BACKGROUND_WINDOW_CLOSED,
    GDOX_BACKGROUND_OPEN_REQUESTED,
    GDOX_BACKGROUND_QUIT_REQUESTED,
    GDOX_BACKGROUND_FACILITY_AVAILABLE,
    GDOX_BACKGROUND_FACILITY_UNAVAILABLE,
} gdox_background_action;

typedef struct gdox_background_lifecycle {
    gdox_background_state state;
    bool background_available;
    bool window_activation_pending;
} gdox_background_lifecycle;

void gdox_background_lifecycle_initialize(
    gdox_background_lifecycle *lifecycle,
    bool start_hidden,
    bool background_available
);
void gdox_background_lifecycle_apply(
    gdox_background_lifecycle *lifecycle,
    gdox_background_action action
);
bool gdox_background_lifecycle_take_window_activation(
    gdox_background_lifecycle *lifecycle
);
bool gdox_background_arguments_request_hidden(
    int argument_count,
    const char *const *arguments
);
bool gdox_background_command_line_requests_hidden(const char *command_line);

#ifdef __cplusplus
}
#endif

#endif
