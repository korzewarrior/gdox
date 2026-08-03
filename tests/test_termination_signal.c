#include "app/termination.h"

#include "test.h"

#include <signal.h>

int gdox_test_failures = 0;

static void catches_signal(int signal_number)
{
    gdox_error error;

    GDOX_TEST_CHECK(gdox_app_termination_install(&error));
    GDOX_TEST_CHECK(!gdox_app_termination_requested());
    GDOX_TEST_CHECK(raise(signal_number) == 0);
    GDOX_TEST_CHECK(gdox_app_termination_requested());
    gdox_app_termination_uninstall();
}

int main(void)
{
    catches_signal(SIGINT);
    catches_signal(SIGTERM);
    return gdox_test_failures == 0 ? 0 : 1;
}
