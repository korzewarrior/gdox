#include "test.h"

#include "app/background_lifecycle.h"
#if defined(_WIN32)
#include "platform/background_host.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void gdox_test_background_lifecycle(void)
{
    gdox_background_lifecycle lifecycle;
    const char *visible_arguments[] = {"gdox", "--example"};
    const char *hidden_arguments[] = {"gdox", "--background"};

    gdox_background_lifecycle_initialize(&lifecycle, false, true);
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_VISIBLE);
    GDOX_TEST_CHECK(
        !gdox_background_lifecycle_take_window_activation(&lifecycle)
    );
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_OPEN_REQUESTED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_VISIBLE);
    GDOX_TEST_CHECK(
        gdox_background_lifecycle_take_window_activation(&lifecycle)
    );
    GDOX_TEST_CHECK(
        !gdox_background_lifecycle_take_window_activation(&lifecycle)
    );
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_WINDOW_CLOSED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_HIDDEN);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_OPEN_REQUESTED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_VISIBLE);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_QUIT_REQUESTED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_STOPPING);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_OPEN_REQUESTED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_STOPPING);
    GDOX_TEST_CHECK(
        !gdox_background_lifecycle_take_window_activation(&lifecycle)
    );

    gdox_background_lifecycle_initialize(&lifecycle, true, true);
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_HIDDEN);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_FACILITY_UNAVAILABLE
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_VISIBLE);
    GDOX_TEST_CHECK(!lifecycle.background_available);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_WINDOW_CLOSED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_STOPPING);

    gdox_background_lifecycle_initialize(&lifecycle, false, false);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_FACILITY_AVAILABLE
    );
    GDOX_TEST_CHECK(lifecycle.background_available);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_WINDOW_CLOSED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_HIDDEN);

    gdox_background_lifecycle_initialize(&lifecycle, true, false);
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_VISIBLE);
    gdox_background_lifecycle_apply(
        &lifecycle, GDOX_BACKGROUND_WINDOW_CLOSED
    );
    GDOX_TEST_CHECK(lifecycle.state == GDOX_BACKGROUND_STOPPING);

    GDOX_TEST_CHECK(
        !gdox_background_arguments_request_hidden(2, visible_arguments)
    );
    GDOX_TEST_CHECK(
        gdox_background_arguments_request_hidden(2, hidden_arguments)
    );
    GDOX_TEST_CHECK(
        gdox_background_command_line_requests_hidden("--background")
    );
    GDOX_TEST_CHECK(
        gdox_background_command_line_requests_hidden(
            "--example \"--background\""
        )
    );
    GDOX_TEST_CHECK(
        !gdox_background_command_line_requests_hidden(
            "--background-extra"
        )
    );
    GDOX_TEST_CHECK(
        !gdox_background_command_line_requests_hidden(
            "\"--background\"-extra"
        )
    );
#if defined(_WIN32)
    GDOX_TEST_CHECK(
        gdox_background_host_windows_session_event(
            WM_QUERYENDSESSION, 0U
        ) == GDOX_BACKGROUND_HOST_NONE
    );
    GDOX_TEST_CHECK(
        gdox_background_host_windows_session_event(
            WM_ENDSESSION, 1U
        ) == GDOX_BACKGROUND_HOST_QUIT
    );
    GDOX_TEST_CHECK(
        gdox_background_host_windows_session_event(
            WM_ENDSESSION, 0U
        ) == GDOX_BACKGROUND_HOST_NONE
    );
    GDOX_TEST_CHECK(
        gdox_background_host_windows_session_event(
            WM_CLOSE, 0U
        ) == GDOX_BACKGROUND_HOST_NONE
    );
#endif
}
