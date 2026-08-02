#ifndef GDOX_SESSION_H
#define GDOX_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_session_event {
    GDOX_SESSION_LAUNCH_REQUESTED = 0,
    GDOX_SESSION_RESTART_REQUESTED,
    GDOX_SESSION_CLOSE_REQUESTED,
    GDOX_SESSION_EJECT_REQUESTED
} gdox_session_event;

#ifdef __cplusplus
}
#endif

#endif
