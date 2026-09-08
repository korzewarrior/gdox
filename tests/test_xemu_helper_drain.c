#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include <string.h>

static unsigned int pipe_reads;

/* Model a pipe that stays readable regardless of scheduler timing. The
 * ceiling lets the regression fail cleanly with the old unbounded loop. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static BOOL WINAPI readable_pipe(HANDLE pipe, LPVOID buffer, DWORD capacity,
    LPDWORD read_bytes, LPDWORD available, LPDWORD remaining)
{
    (void)pipe; (void)buffer; (void)capacity; (void)read_bytes; (void)remaining;
    *available = 256U;
    return TRUE;
}

static BOOL WINAPI continuous_read(HANDLE pipe, LPVOID output, DWORD capacity,
    LPDWORD bytes, LPOVERLAPPED overlapped)
{
    (void)pipe; (void)overlapped;
    if (++pipe_reads > 1024U) {
        SetLastError(ERROR_BROKEN_PIPE);
        return FALSE;
    }
    memset(output, 'x', capacity);
    *bytes = capacity;
    return TRUE;
}

#define PeekNamedPipe readable_pipe
#define ReadFile continuous_read
#define gdox_xemu_helper_run gdox_test_drain_unused_helper_run
#include "platform/xemu_helper_process_windows.c"
#undef gdox_xemu_helper_run
#undef ReadFile
#undef PeekNamedPipe
#else
#include <errno.h>
#include <unistd.h>

static ssize_t continuous_read(int descriptor, void *output, size_t capacity)
{
    (void)descriptor;
    if (++pipe_reads > 1024U) {
        errno = EAGAIN;
        return -1;
    }
    memset(output, 'x', capacity);
    return (ssize_t)capacity;
}

#define read continuous_read
#define gdox_xemu_helper_run gdox_test_drain_unused_helper_run
/* glibc may raise this level while loading the headers above. */
#undef _POSIX_C_SOURCE
#include "platform/xemu_helper_process_posix.c"
#undef gdox_xemu_helper_run
#undef read
#endif

void gdox_test_xemu_helper_drain(void)
{
    char output[GDOX_XEMU_HELPER_CAPTURE_BYTES];
    size_t bytes = 0U;
    bool open = true;
    bool overflow = false;
    gdox_error error;

    for (unsigned int poll = 0U; poll < 2U; ++poll) {
        pipe_reads = 0U;
#if defined(_WIN32)
        GDOX_TEST_CHECK(capture_pipe_output(NULL, output, &bytes,
            &open, &overflow, &error));
#else
        GDOX_TEST_CHECK(capture_output(-1, output, &bytes,
            &open, &overflow, &error));
#endif
        GDOX_TEST_CHECK(open && pipe_reads > 0U && pipe_reads <= 64U);
    }
    GDOX_TEST_CHECK(bytes == sizeof(output) && overflow);
}
