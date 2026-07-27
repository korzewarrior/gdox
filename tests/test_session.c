#include "test.h"

#include "gdox/session.h"

void gdox_test_session(void)
{
    gdox_session session;

    gdox_session_initialize(&session, true, true);
    GDOX_TEST_CHECK(
        gdox_session_apply(&session, GDOX_SESSION_EMULATOR_EXITED) == GDOX_SESSION_ACTION_READY
    );
    GDOX_TEST_CHECK(
        gdox_session_apply(&session, GDOX_SESSION_LAUNCH_REQUESTED) == GDOX_SESSION_ACTION_LAUNCH
    );
    GDOX_TEST_CHECK(
        gdox_session_apply(&session, GDOX_SESSION_CLOSE_REQUESTED) == GDOX_SESSION_ACTION_CLOSE
    );
    GDOX_TEST_CHECK(
        gdox_session_apply(&session, GDOX_SESSION_RESTART_REQUESTED) == GDOX_SESSION_ACTION_LAUNCH
    );
    GDOX_TEST_CHECK(
        gdox_session_apply(&session, GDOX_SESSION_EXPORT_FAILED) == GDOX_SESSION_ACTION_FAIL
    );
}
