#define _POSIX_C_SOURCE 200809L

#include "platform/xenia_bridge_tools_linux.h"

#include "platform/portable_sync.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

enum {
    GDOX_XENIA_HELPER_POLL_MS = 10U,
    GDOX_XENIA_HELPER_TIMEOUT_MS = 2000U,
};

static bool helper_probe(const char *helper)
{
    posix_spawn_file_actions_t actions;
    char *arguments[] = {(char *)helper, "--version", NULL};
    pid_t child;
    uint32_t waited = 0U;
    int status = 0;
    int result = posix_spawn_file_actions_init(&actions);
    const bool actions_initialized = result == 0;

    if (result == 0) {
        result = posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0
        );
    }
    if (result == 0) {
        result = posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0
        );
    }
    if (result != 0) {
        if (actions_initialized) {
            (void)posix_spawn_file_actions_destroy(&actions);
        }
        return false;
    }
    result = posix_spawn(
        &child, helper, &actions, NULL, arguments, environ
    );
    (void)posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        return false;
    }
    for (;;) {
        pid_t waited_child;

        do {
            waited_child = waitpid(child, &status, WNOHANG);
        } while (waited_child < 0 && errno == EINTR);
        if (waited_child == child) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (waited_child < 0 || waited >= GDOX_XENIA_HELPER_TIMEOUT_MS) {
            break;
        }
        gdox_sleep_ms(GDOX_XENIA_HELPER_POLL_MS);
        waited += GDOX_XENIA_HELPER_POLL_MS;
    }
    (void)kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return false;
}

static bool executable_file(const char *path)
{
    struct stat status;

    return path != NULL && path[0] != '\0'
        && stat(path, &status) == 0 && S_ISREG(status.st_mode)
        && access(path, X_OK) == 0;
}

static bool executable_directory(
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char *slash;
    const ssize_t bytes = readlink(
        "/proc/self/exe", executable, sizeof(executable) - 1U
    );

    if (bytes <= 0 || (size_t)bytes >= sizeof(executable)) {
        return false;
    }
    executable[bytes] = '\0';
    slash = strrchr(executable, '/');
    if (slash == NULL || slash == executable) {
        return false;
    }
    *slash = '\0';
    return snprintf(
        output, GDOX_EMULATOR_PATH_CAPACITY, "%s", executable
    ) >= 0;
}

static bool bundled_helper(
    const char *name,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    char directory[GDOX_EMULATOR_PATH_CAPACITY];
    int written;

    if (!executable_directory(directory)) {
        return false;
    }
    written = snprintf(
        output, GDOX_EMULATOR_PATH_CAPACITY, "%s/%s", directory, name
    );
    return written >= 0 && (size_t)written < GDOX_EMULATOR_PATH_CAPACITY
        && executable_file(output);
}

static bool path_helper(
    const char *name,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    static const char default_path[] = "/bin:/usr/bin";
    const char *path = getenv("PATH");
    const char *cursor = path != NULL ? path : default_path;

    for (;;) {
        const char *separator = strchr(cursor, ':');
        const size_t directory_bytes = separator != NULL
            ? (size_t)(separator - cursor)
            : strlen(cursor);
        int written;

        if (directory_bytes != 0U && cursor[0] == '/') {
            written = directory_bytes + strlen(name) + 2U
                    <= GDOX_EMULATOR_PATH_CAPACITY
                ? snprintf(
                    output,
                    GDOX_EMULATOR_PATH_CAPACITY,
                    "%.*s/%s",
                    (int)directory_bytes,
                    cursor,
                    name
                )
                : -1;
            if (written >= 0
                && (size_t)written < GDOX_EMULATOR_PATH_CAPACITY
                && executable_file(output)) {
                return true;
            }
        }
        if (separator == NULL) {
            output[0] = '\0';
            return false;
        }
        cursor = separator + 1;
    }
}

static bool resolve_helper(
    const char *name,
    bool prefer_bundled,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    output[0] = '\0';
    return (prefer_bundled && bundled_helper(name, output))
        || path_helper(name, output);
}

bool gdox_xenia_bridge_tools_resolve(
    gdox_xenia_bridge_tools *tools,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (tools == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xbox 360 disc bridge tool output is required"
        );
        return false;
    }
    memset(tools, 0, sizeof(*tools));
    if (!resolve_helper("nbdfuse", true, tools->mount)) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "the Xbox 360 disc bridge (nbdfuse) is unavailable"
        );
        return false;
    }
    if (!resolve_helper("fusermount3", false, tools->unmount)) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "the host FUSE mount helper (fusermount3) is unavailable"
        );
        return false;
    }
    if (!helper_probe(tools->mount) || !helper_probe(tools->unmount)) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "the Xbox 360 disc bridge failed its runtime check"
        );
        return false;
    }
    return true;
}
