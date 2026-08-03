#include "platform/instance_guard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

struct gdox_instance_guard {
    HANDLE mutex;
    HANDLE activation_event;
};

static const char instance_mutex_name[] = "Local\\GDOX.Desktop.Instance";
static const char activation_event_name[] = "Local\\GDOX.Desktop.Activate";

gdox_instance_guard *gdox_instance_guard_acquire(bool *already_running)
{
    gdox_instance_guard *guard;
    HANDLE activation_event;
    HANDLE mutex;

    if (already_running != NULL) {
        *already_running = false;
    }
    mutex = CreateMutexA(NULL, FALSE, instance_mutex_name);
    if (mutex == NULL) {
        return NULL;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (already_running != NULL) {
            *already_running = true;
        }
        (void)CloseHandle(mutex);
        return NULL;
    }
    activation_event = CreateEventA(
        NULL, FALSE, FALSE, activation_event_name
    );
    if (activation_event == NULL) {
        (void)CloseHandle(mutex);
        return NULL;
    }
    guard = malloc(sizeof(*guard));
    if (guard == NULL) {
        (void)CloseHandle(activation_event);
        (void)CloseHandle(mutex);
        return NULL;
    }
    guard->mutex = mutex;
    guard->activation_event = activation_event;
    return guard;
}

bool gdox_instance_guard_activate_existing(void)
{
    HANDLE activation_event;
    bool activated;
    unsigned int attempt;

    for (attempt = 0U; attempt < 20U; ++attempt) {
        activation_event = OpenEventA(
            EVENT_MODIFY_STATE, FALSE, activation_event_name
        );
        if (activation_event != NULL) {
            activated = SetEvent(activation_event) != 0;
            (void)CloseHandle(activation_event);
            return activated;
        }
        Sleep(10U);
    }
    return false;
}

bool gdox_instance_guard_take_activation(gdox_instance_guard *guard)
{
    return guard != NULL
        && WaitForSingleObject(guard->activation_event, 0U)
            == WAIT_OBJECT_0;
}

void gdox_instance_guard_report_conflict(void)
{
    (void)MessageBoxA(
        NULL,
        "GDOX is already running, but its window could not be opened.",
        "GDOX",
        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND
    );
}

void gdox_instance_guard_report_failure(void)
{
    (void)MessageBoxA(
        NULL,
        "GDOX could not establish its private desktop-instance lock.",
        "GDOX",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND
    );
}

void gdox_instance_guard_release(gdox_instance_guard *guard)
{
    if (guard == NULL) {
        return;
    }
    (void)CloseHandle(guard->activation_event);
    (void)CloseHandle(guard->mutex);
    free(guard);
}
