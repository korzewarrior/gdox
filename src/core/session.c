#include "gdox/session.h"

#include <stddef.h>

void gdox_session_initialize(gdox_session *session, bool interactive, bool emulator_running)
{
    if (session == NULL) {
        return;
    }
    session->interactive = interactive;
    session->emulator_running = emulator_running;
}

gdox_session_action gdox_session_apply(gdox_session *session, gdox_session_event event)
{
    if (session == NULL) {
        return GDOX_SESSION_ACTION_FAIL;
    }

    switch (event) {
        case GDOX_SESSION_LAUNCH_REQUESTED:
            if (!session->emulator_running) {
                session->emulator_running = true;
                return GDOX_SESSION_ACTION_LAUNCH;
            }
            return GDOX_SESSION_ACTION_NONE;
        case GDOX_SESSION_RESTART_REQUESTED:
            if (session->emulator_running) {
                return GDOX_SESSION_ACTION_RESTART;
            }
            session->emulator_running = true;
            return GDOX_SESSION_ACTION_LAUNCH;
        case GDOX_SESSION_CLOSE_REQUESTED:
            if (session->emulator_running) {
                session->emulator_running = false;
                return GDOX_SESSION_ACTION_CLOSE;
            }
            return GDOX_SESSION_ACTION_NONE;
        case GDOX_SESSION_EMULATOR_EXITED:
            session->emulator_running = false;
            return session->interactive ? GDOX_SESSION_ACTION_READY : GDOX_SESSION_ACTION_FINISH;
        case GDOX_SESSION_EJECT_REQUESTED:
            session->emulator_running = false;
            return GDOX_SESSION_ACTION_EJECT;
        case GDOX_SESSION_CANCELLED:
        case GDOX_SESSION_MEDIA_REMOVED:
            session->emulator_running = false;
            return GDOX_SESSION_ACTION_FINISH;
        case GDOX_SESSION_EXPORT_FAILED:
            session->emulator_running = false;
            return GDOX_SESSION_ACTION_FAIL;
    }
    return GDOX_SESSION_ACTION_FAIL;
}
