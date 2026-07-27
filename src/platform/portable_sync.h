#ifndef GDOX_PORTABLE_SYNC_H
#define GDOX_PORTABLE_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*gdox_thread_function)(void *context);

typedef struct gdox_thread {
    void *implementation;
} gdox_thread;

bool gdox_thread_start(
    gdox_thread *thread,
    gdox_thread_function function,
    void *context
);
bool gdox_thread_join(gdox_thread *thread);

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef CRITICAL_SECTION gdox_mutex;

static inline bool gdox_mutex_init(gdox_mutex *mutex)
{
    InitializeCriticalSection(mutex);
    return true;
}

static inline bool gdox_mutex_lock(gdox_mutex *mutex)
{
    EnterCriticalSection(mutex);
    return true;
}

static inline void gdox_mutex_unlock(gdox_mutex *mutex)
{
    LeaveCriticalSection(mutex);
}

static inline void gdox_mutex_destroy(gdox_mutex *mutex)
{
    DeleteCriticalSection(mutex);
}

static inline void gdox_sleep_ms(uint32_t milliseconds)
{
    Sleep(milliseconds);
}

static inline uint64_t gdox_monotonic_ms(void)
{
    return (uint64_t)GetTickCount64();
}

#else

#include <errno.h>
#include <pthread.h>
#include <time.h>

typedef pthread_mutex_t gdox_mutex;

static inline bool gdox_mutex_init(gdox_mutex *mutex)
{
    return pthread_mutex_init(mutex, NULL) == 0;
}

static inline bool gdox_mutex_lock(gdox_mutex *mutex)
{
    return pthread_mutex_lock(mutex) == 0;
}

static inline void gdox_mutex_unlock(gdox_mutex *mutex)
{
    (void)pthread_mutex_unlock(mutex);
}

static inline void gdox_mutex_destroy(gdox_mutex *mutex)
{
    (void)pthread_mutex_destroy(mutex);
}

static inline void gdox_sleep_ms(uint32_t milliseconds)
{
    struct timespec duration;
    duration.tv_sec = (time_t)(milliseconds / 1000U);
    duration.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
    }
}

static inline uint64_t gdox_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000)
        + (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

#endif

#endif
