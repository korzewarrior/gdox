#define _POSIX_C_SOURCE 200809L

#include "platform/portable_sync.h"

#include <stdlib.h>

typedef struct gdox_thread_launch {
    gdox_thread_function function;
    void *context;
} gdox_thread_launch;

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>

typedef struct gdox_thread_implementation {
    HANDLE handle;
} gdox_thread_implementation;

static unsigned __stdcall thread_entry(void *raw_start)
{
    gdox_thread_launch *start = raw_start;
    const gdox_thread_function function = start->function;
    void *context = start->context;
    free(start);
    function(context);
    return 0U;
}

bool gdox_thread_start(
    gdox_thread *thread,
    gdox_thread_function function,
    void *context
)
{
    gdox_thread_implementation *implementation;
    gdox_thread_launch *start;
    uintptr_t handle;

    if (thread == NULL || function == NULL) {
        return false;
    }
    thread->implementation = NULL;
    implementation = calloc(1U, sizeof(*implementation));
    start = malloc(sizeof(*start));
    if (implementation == NULL || start == NULL) {
        free(implementation);
        free(start);
        return false;
    }
    start->function = function;
    start->context = context;
    handle = _beginthreadex(NULL, 0U, thread_entry, start, 0U, NULL);
    if (handle == 0U) {
        free(start);
        free(implementation);
        return false;
    }
    implementation->handle = (HANDLE)handle;
    thread->implementation = implementation;
    return true;
}

bool gdox_thread_join(gdox_thread *thread)
{
    gdox_thread_implementation *implementation;
    DWORD wait_result;

    if (thread == NULL || thread->implementation == NULL) {
        return false;
    }
    implementation = thread->implementation;
    wait_result = WaitForSingleObject(implementation->handle, INFINITE);
    if (wait_result != WAIT_OBJECT_0
        || CloseHandle(implementation->handle) == 0) {
        return false;
    }
    free(implementation);
    thread->implementation = NULL;
    return true;
}

#else

#include <pthread.h>

typedef struct gdox_thread_implementation {
    pthread_t handle;
} gdox_thread_implementation;

static void *thread_entry(void *raw_start)
{
    gdox_thread_launch *start = raw_start;
    const gdox_thread_function function = start->function;
    void *context = start->context;
    free(start);
    function(context);
    return NULL;
}

bool gdox_thread_start(
    gdox_thread *thread,
    gdox_thread_function function,
    void *context
)
{
    gdox_thread_implementation *implementation;
    gdox_thread_launch *start;

    if (thread == NULL || function == NULL) {
        return false;
    }
    thread->implementation = NULL;
    implementation = calloc(1U, sizeof(*implementation));
    start = malloc(sizeof(*start));
    if (implementation == NULL || start == NULL) {
        free(implementation);
        free(start);
        return false;
    }
    start->function = function;
    start->context = context;
    if (pthread_create(&implementation->handle, NULL, thread_entry, start)
        != 0) {
        free(start);
        free(implementation);
        return false;
    }
    thread->implementation = implementation;
    return true;
}

bool gdox_thread_join(gdox_thread *thread)
{
    gdox_thread_implementation *implementation;

    if (thread == NULL || thread->implementation == NULL) {
        return false;
    }
    implementation = thread->implementation;
    if (pthread_join(implementation->handle, NULL) != 0) {
        return false;
    }
    free(implementation);
    thread->implementation = NULL;
    return true;
}

#endif
