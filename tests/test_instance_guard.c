#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/instance_guard.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int check(bool condition, const char *expression, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(
        stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression
    );
    return 1;
}

#define CHECK(expression) \
    do { \
        if (check((expression), #expression, __LINE__) != 0) { \
            return 1; \
        } \
    } while (false)

int main(void)
{
    gdox_instance_guard *owner;
    bool already_running = false;

#if !defined(_WIN32)
    char runtime_directory[] = "/tmp/gdox-instance-test-XXXXXX";
    char activation_path[512];
    char lock_path[512];
    int child_status;
    pid_t child;

    CHECK(mkdtemp(runtime_directory) != NULL);
    CHECK(setenv("XDG_RUNTIME_DIR", runtime_directory, 1) == 0);
    CHECK(snprintf(
        activation_path,
        sizeof(activation_path),
        "%s/gdox-desktop.activate",
        runtime_directory
    ) > 0);
    CHECK(snprintf(
        lock_path,
        sizeof(lock_path),
        "%s/gdox-desktop.lock",
        runtime_directory
    ) > 0);
#endif

    owner = gdox_instance_guard_acquire(&already_running);
    CHECK(owner != NULL);
    CHECK(!already_running);

#if defined(_WIN32)
    {
        gdox_instance_guard *contender =
            gdox_instance_guard_acquire(&already_running);

        CHECK(contender == NULL);
        CHECK(already_running);
        CHECK(gdox_instance_guard_activate_existing());
    }
#else
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        gdox_instance_guard *contender =
            gdox_instance_guard_acquire(&already_running);
        const bool passed = contender == NULL
            && already_running
            && gdox_instance_guard_activate_existing();

        gdox_instance_guard_release(contender);
        _exit(passed ? 0 : 1);
    }
    CHECK(waitpid(child, &child_status, 0) == child);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
#endif

    CHECK(gdox_instance_guard_take_activation(owner));
    CHECK(!gdox_instance_guard_take_activation(owner));
    gdox_instance_guard_release(owner);

#if !defined(_WIN32)
    CHECK(access(activation_path, F_OK) != 0 && errno == ENOENT);
#endif

    already_running = true;
    owner = gdox_instance_guard_acquire(&already_running);
    CHECK(owner != NULL);
    CHECK(!already_running);
    gdox_instance_guard_release(owner);

#if !defined(_WIN32)
    CHECK(unlink(lock_path) == 0);
    CHECK(rmdir(runtime_directory) == 0);
#endif
    return 0;
}
