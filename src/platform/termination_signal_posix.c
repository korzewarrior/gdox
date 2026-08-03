#define _POSIX_C_SOURCE 200809L

#include "app/termination.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t termination_requested;
static struct sigaction previous_interrupt;
static struct sigaction previous_terminate;
static bool handlers_installed;

static void request_termination(int signal_number)
{
    termination_requested = (sig_atomic_t)signal_number;
}

bool gdox_app_termination_install(gdox_error *error)
{
    struct sigaction action;

    gdox_error_clear(error);
    if (handlers_installed) {
        termination_requested = 0;
        return true;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_termination;
    if (sigemptyset(&action.sa_mask) != 0
        || sigaction(SIGINT, &action, &previous_interrupt) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not install the interrupt shutdown handler"
        );
        return false;
    }
    if (sigaction(SIGTERM, &action, &previous_terminate) != 0) {
        (void)sigaction(SIGINT, &previous_interrupt, NULL);
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not install the termination shutdown handler"
        );
        return false;
    }
    termination_requested = 0;
    handlers_installed = true;
    return true;
}

bool gdox_app_termination_requested(void)
{
    return termination_requested != 0;
}

void gdox_app_termination_uninstall(void)
{
    if (!handlers_installed) {
        return;
    }
    (void)sigaction(SIGTERM, &previous_terminate, NULL);
    (void)sigaction(SIGINT, &previous_interrupt, NULL);
    handlers_installed = false;
    termination_requested = 0;
}
