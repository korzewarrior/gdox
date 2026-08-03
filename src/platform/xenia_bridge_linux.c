#define _POSIX_C_SOURCE 200809L

#include "platform/xenia_bridge_linux.h"

#include "platform/portable_sync.h"
#include "platform/xenia_bridge_tools_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <signal.h>
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
    GDOX_XENIA_BRIDGE_CLOSE_MAX_POLLS = 120U,
};

struct gdox_xenia_bridge {
    pid_t child;
    bool unmount_requested;
    unsigned int close_polls;
    char file[GDOX_EMULATOR_PATH_CAPACITY];
    char directory[GDOX_EMULATOR_PATH_CAPACITY];
    char unmount_helper[GDOX_EMULATOR_PATH_CAPACITY];
};

bool gdox_xenia_bridge_preflight(gdox_error *error)
{
    gdox_xenia_bridge_tools tools;

    return gdox_xenia_bridge_tools_resolve(&tools, error);
}

static bool child_finished(pid_t *child, bool *finished)
{
    int status;
    pid_t result;

    if (child == NULL || *child <= 0) {
        *finished = true;
        return true;
    }
    do {
        result = waitpid(*child, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == *child || (result < 0 && errno == ECHILD)) {
        *child = 0;
        *finished = true;
        return true;
    }
    if (result == 0) {
        *finished = false;
        return true;
    }
    return false;
}

static bool force_reap(pid_t *child)
{
    int status;

    if (child == NULL || *child <= 0) {
        return true;
    }
    (void)kill(*child, SIGKILL);
    for (;;) {
        const pid_t result = waitpid(*child, &status, 0);

        if (result == *child || (result < 0 && errno == ECHILD)) {
            *child = 0;
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return false;
        }
    }
}

static bool stop_child(pid_t *child)
{
    unsigned int attempt;
    bool finished = false;

    if (child == NULL || *child <= 0) {
        return true;
    }
    if (child_finished(child, &finished) && finished) {
        return true;
    }
    (void)kill(*child, SIGTERM);
    for (attempt = 0U; attempt < 30U; ++attempt) {
        gdox_sleep_ms(100U);
        if (child_finished(child, &finished) && finished) {
            return true;
        }
    }
    return force_reap(child);
}

static bool unmount_bridge(
    const char *helper,
    const char *directory,
    unsigned int attempts
)
{
    char *arguments[] = {
        (char *)helper,
        "-q",
        "-u",
        (char *)directory,
        NULL,
    };
    pid_t child;
    unsigned int attempt;
    int status;

    if (helper == NULL || helper[0] == '\0'
        || directory == NULL || directory[0] == '\0'
        || posix_spawn(
            &child,
            arguments[0],
            NULL,
            NULL,
            arguments,
            environ
        ) != 0) {
        return false;
    }
    for (attempt = 0U; attempt < attempts; ++attempt) {
        const pid_t result = waitpid(child, &status, WNOHANG);

        if (result == child || (result < 0 && errno == ECHILD)) {
            return result < 0
                || (WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        gdox_sleep_ms(10U);
    }
    (void)force_reap(&child);
    return false;
}

static bool remove_bridge_files(gdox_xenia_bridge *bridge)
{
    const bool file_removed = unlink(bridge->file) == 0 || errno == ENOENT;
    const bool directory_removed = rmdir(bridge->directory) == 0
        || errno == ENOENT;

    return file_removed && directory_removed;
}

bool gdox_xenia_bridge_try_close(
    gdox_xenia_bridge *bridge,
    bool *closed,
    gdox_error *error
)
{
    bool finished = false;

    gdox_error_clear(error);
    if (closed == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private Xbox 360 disc view close output is required"
        );
        return false;
    }
    *closed = bridge == NULL;
    if (bridge == NULL) {
        return true;
    }
    if (!bridge->unmount_requested) {
        if (!unmount_bridge(
                bridge->unmount_helper, bridge->directory, 25U
            )) {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not request private Xbox 360 disc view shutdown"
            );
            return false;
        }
        bridge->unmount_requested = true;
    }
    if (!child_finished(&bridge->child, &finished)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not monitor private Xbox 360 disc view shutdown"
        );
        return false;
    }
    if (!finished) {
        ++bridge->close_polls;
        if (bridge->close_polls >= GDOX_XENIA_BRIDGE_CLOSE_MAX_POLLS) {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "private Xbox 360 disc view did not exit after unmount"
            );
            return false;
        }
        return true;
    }
    if (!remove_bridge_files(bridge)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not remove the private Xbox 360 disc view"
        );
        return false;
    }
    free(bridge);
    *closed = true;
    return true;
}

bool gdox_xenia_bridge_close(
    gdox_xenia_bridge *bridge,
    gdox_error *error
)
{
    bool stopped = true;
    bool files_removed = true;

    gdox_error_clear(error);
    if (bridge == NULL) {
        return true;
    }
    if (bridge->directory[0] != '\0') {
        (void)unmount_bridge(
            bridge->unmount_helper, bridge->directory, 100U
        );
        stopped = stop_child(&bridge->child);
        (void)unmount_bridge(
            bridge->unmount_helper, bridge->directory, 100U
        );
        files_removed = remove_bridge_files(bridge);
    }
    if (!stopped || !files_removed) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not completely close the private Xbox 360 disc view"
        );
        return false;
    }
    free(bridge);
    return true;
}

void gdox_xenia_bridge_destroy(gdox_xenia_bridge *bridge)
{
    gdox_error error;

    if (!gdox_xenia_bridge_close(bridge, &error)) {
        (void)fprintf(stderr, "GDOX: %s\n", error.message);
    }
}

static bool configure_output(
    posix_spawn_file_actions_t *actions,
    bool console_output,
    gdox_error *error
)
{
    int result = posix_spawn_file_actions_init(actions);
    const bool initialized = result == 0;

    if (result == 0 && !console_output) {
        result = posix_spawn_file_actions_addopen(
            actions,
            STDOUT_FILENO,
            "/dev/null",
            O_WRONLY,
            0
        );
    }
    if (result == 0 && !console_output) {
        result = posix_spawn_file_actions_addopen(
            actions,
            STDERR_FILENO,
            "/dev/null",
            O_WRONLY,
            0
        );
    }
    if (result != 0) {
        if (initialized) {
            (void)posix_spawn_file_actions_destroy(actions);
        }
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not configure the private Xenia disc view"
        );
        return false;
    }
    return true;
}

bool gdox_xenia_bridge_start(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_bridge **output,
    gdox_error *error
)
{
    gdox_xenia_bridge *bridge;
    posix_spawn_file_actions_t actions;
    char template_path[GDOX_EMULATOR_PATH_CAPACITY];
    gdox_xenia_bridge_tools tools;
    char *arguments[8];
    unsigned int attempt;
    int written;
    int spawn_result;

    gdox_error_clear(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (options == NULL || options->storage_root == NULL
        || target == NULL || output == NULL
        || target->kind != GDOX_XENIA_TARGET_PRIVATE_NBD
        || target->length == 0U
        || target->length > (uint64_t)INT64_MAX
        || !gdox_xenia_validate_disc_uri(target->location, error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "a bounded private Xbox 360 disc target is required"
            );
        }
        return false;
    }
    bridge = calloc(1U, sizeof(*bridge));
    if (bridge == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate the private Xenia disc view"
        );
        return false;
    }
    if (!gdox_xenia_bridge_tools_resolve(&tools, error)) {
        gdox_xenia_bridge_destroy(bridge);
        return false;
    }
    memcpy(
        bridge->unmount_helper,
        tools.unmount,
        strlen(tools.unmount) + 1U
    );
    written = snprintf(
        template_path,
        sizeof(template_path),
        "%s/.gdox-xenia-XXXXXX",
        options->storage_root
    );
    if (written < 0 || (size_t)written >= sizeof(template_path)
        || mkdtemp(template_path) == NULL
        || strlen(template_path) >= sizeof(bridge->directory)) {
        gdox_xenia_bridge_destroy(bridge);
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not create the private Xenia disc view"
        );
        return false;
    }
    memcpy(bridge->directory, template_path, strlen(template_path) + 1U);
    written = snprintf(
        bridge->file,
        sizeof(bridge->file),
        "%s/disc.iso",
        bridge->directory
    );
    if (written < 0 || (size_t)written >= sizeof(bridge->file)
        || !configure_output(&actions, options->console_output, error)) {
        gdox_xenia_bridge_destroy(bridge);
        return false;
    }
    arguments[0] = tools.mount;
    arguments[1] = "-C";
    arguments[2] = "1";
    arguments[3] = "-s";
    arguments[4] = "-r";
    arguments[5] = bridge->file;
    arguments[6] = (char *)target->location;
    arguments[7] = NULL;
    spawn_result = posix_spawn(
        &bridge->child,
        arguments[0],
        &actions,
        NULL,
        arguments,
        environ
    );
    (void)posix_spawn_file_actions_destroy(&actions);
    if (spawn_result != 0) {
        gdox_xenia_bridge_destroy(bridge);
        gdox_error_set(
            error,
            spawn_result == ENOENT ? GDOX_ERROR_NOT_FOUND : GDOX_ERROR_IO,
            spawn_result == ENOENT
                ? "the Xbox 360 disc bridge is unavailable"
                : "could not start the private Xenia disc view"
        );
        return false;
    }
    for (attempt = 0U; attempt < 150U; ++attempt) {
        struct stat status;
        int child_status;
        const pid_t result = waitpid(bridge->child, &child_status, WNOHANG);

        if (result == bridge->child || (result < 0 && errno == ECHILD)) {
            bridge->child = 0;
            break;
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        if (stat(bridge->file, &status) == 0 && S_ISREG(status.st_mode)
            && status.st_size >= 0
            && (uint64_t)status.st_size == target->length) {
            *output = bridge;
            return true;
        }
        gdox_sleep_ms(100U);
    }
    gdox_xenia_bridge_destroy(bridge);
    gdox_error_set(
        error,
        GDOX_ERROR_IO,
        "private Xenia disc view did not become ready"
    );
    return false;
}

const char *gdox_xenia_bridge_path(const gdox_xenia_bridge *bridge)
{
    return bridge != NULL ? bridge->file : NULL;
}

bool gdox_xenia_bridge_alive(
    gdox_xenia_bridge *bridge,
    bool *alive,
    gdox_error *error
)
{
    int status;
    pid_t result;

    gdox_error_clear(error);
    if (bridge == NULL || alive == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private Xenia disc view and status output are required"
        );
        return false;
    }
    if (bridge->child <= 0) {
        *alive = false;
        return true;
    }
    result = waitpid(bridge->child, &status, WNOHANG);
    if (result == 0) {
        *alive = true;
        return true;
    }
    if (result == bridge->child || (result < 0 && errno == ECHILD)) {
        bridge->child = 0;
        *alive = false;
        return true;
    }
    if (result < 0 && errno == EINTR) {
        *alive = true;
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_IO,
        "could not monitor the private Xbox 360 disc view"
    );
    return false;
}
