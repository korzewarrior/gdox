#ifndef GDOX_SESSION_H
#define GDOX_SESSION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_session_event {
    GDOX_SESSION_LAUNCH_REQUESTED = 0,
    GDOX_SESSION_RESTART_REQUESTED,
    GDOX_SESSION_CLOSE_REQUESTED,
    GDOX_SESSION_EJECT_REQUESTED,
    GDOX_SESSION_EMULATOR_EXITED,
    GDOX_SESSION_CANCELLED,
    GDOX_SESSION_MEDIA_REMOVED,
    GDOX_SESSION_EXPORT_FAILED
} gdox_session_event;

typedef enum gdox_session_action {
    GDOX_SESSION_ACTION_NONE = 0,
    GDOX_SESSION_ACTION_LAUNCH,
    GDOX_SESSION_ACTION_RESTART,
    GDOX_SESSION_ACTION_CLOSE,
    GDOX_SESSION_ACTION_READY,
    GDOX_SESSION_ACTION_FINISH,
    GDOX_SESSION_ACTION_EJECT,
    GDOX_SESSION_ACTION_FAIL
} gdox_session_action;

typedef struct gdox_session {
    bool interactive;
    bool emulator_running;
} gdox_session;

void gdox_session_initialize(gdox_session *session, bool interactive, bool emulator_running);
gdox_session_action gdox_session_apply(gdox_session *session, gdox_session_event event);

#ifdef __cplusplus
}
#endif

#endif
