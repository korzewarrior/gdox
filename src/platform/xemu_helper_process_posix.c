#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "platform/xemu_helper_process.h"

#include "gdox/emulator.h"
#include "platform/portable_sync.h"
#include "platform/session_storage.h"
#include "platform/xemu_runtime_session.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    GDOX_XEMU_HELPER_POLL_MS = 10U,
    GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS = 10U,
};

static bool executable_file(const char *path)
{
    struct stat status;

    return path != NULL && path[0] != '\0'
        && stat(path, &status) == 0 && S_ISREG(status.st_mode)
        && access(path, X_OK) == 0;
}

static bool set_nonblocking(int descriptor, gdox_error *error)
{
    const int flags = fcntl(descriptor, F_GETFL);

    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not configure xemu helper output capture"
        );
        return false;
    }
    return true;
}

static bool capture_output(
    int descriptor,
    char output[GDOX_XEMU_HELPER_CAPTURE_BYTES],
    size_t *bytes,
    bool *open,
    bool *overflow,
    gdox_error *error
)
{
    char chunk[256];

    while (*open) {
        const ssize_t count = read(descriptor, chunk, sizeof(chunk));

        if (count > 0) {
            const size_t available = GDOX_XEMU_HELPER_CAPTURE_BYTES - *bytes;
            const size_t copied = (size_t)count < available
                ? (size_t)count : available;

            memcpy(output + *bytes, chunk, copied);
            *bytes += copied;
            *overflow = *overflow || copied != (size_t)count;
            continue;
        }
        if (count == 0) {
            (void)close(descriptor);
            *open = false;
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        gdox_error_set(
            error, GDOX_ERROR_IO, "could not read xemu helper output"
        );
        return false;
    }
    return true;
}

bool gdox_xemu_helper_run(
    const char *path,
    const char *const *arguments,
    uint32_t timeout_ms,
    gdox_xemu_helper_result *output,
    gdox_error *error
)
{
    int output_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    bool output_open = false;
    bool diagnostic_open = false;
    bool actions_ready = false;
    bool attributes_ready = false;
    bool reaped = false;
    pid_t child = 0;
    int status = 0;
    uint32_t elapsed = 0U;
    gdox_session_storage temporary = {0};
    gdox_xemu_environment child_environment = {0};
    gdox_error cleanup_error;
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char *child_arguments[GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS + 2U];
    size_t argument_count = 0U;
    bool success = false;
    int operation_result;

    gdox_error_clear(error);
    if (output != NULL) {
        memset(output, 0, sizeof(*output));
        output->exit_code = -1;
    }
    if (!executable_file(path) || arguments == NULL || output == NULL
        || timeout_ms == 0U
        || timeout_ms > GDOX_XEMU_HELPER_MAXIMUM_TIMEOUT_MS) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "runnable xemu path, helper arguments, timeout, and result are required"
        );
        return false;
    }
    while (arguments[argument_count] != NULL
        && argument_count <= GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS) {
        ++argument_count;
    }
    if (argument_count == 0U
        || argument_count > GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu helper argument count is invalid"
        );
        return false;
    }
    if (realpath(path, executable) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu helper executable path is unavailable"
        );
        return false;
    }
    child_arguments[0] = executable;
    for (size_t index = 0U; index < argument_count; ++index) {
        child_arguments[index + 1U] = (char *)arguments[index];
    }
    child_arguments[argument_count + 1U] = NULL;
    if (!gdox_session_storage_recover(error)
        || !gdox_session_storage_create(&temporary, error)
        || !gdox_xemu_environment_create(
            temporary.root, &child_environment, error
        )) {
        goto cleanup;
    }
    if (pipe(output_pipe) != 0) {
        gdox_error_set(
            error, GDOX_ERROR_IO, "could not create xemu helper pipes"
        );
        goto cleanup;
    }
    output_open = true;
    if (pipe(error_pipe) != 0) {
        gdox_error_set(
            error, GDOX_ERROR_IO, "could not create xemu helper pipes"
        );
        goto cleanup;
    }
    diagnostic_open = true;
    operation_result = posix_spawn_file_actions_init(&actions);
    actions_ready = operation_result == 0;
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_addchdir_np(
            &actions, temporary.root
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_adddup2(
            &actions, output_pipe[1], STDOUT_FILENO
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_adddup2(
            &actions, error_pipe[1], STDERR_FILENO
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_addclose(
            &actions, output_pipe[0]
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_addclose(
            &actions, error_pipe[0]
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_addclose(
            &actions, output_pipe[1]
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawn_file_actions_addclose(
            &actions, error_pipe[1]
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawnattr_init(&attributes);
        attributes_ready = operation_result == 0;
    }
    if (operation_result == 0) {
        operation_result = posix_spawnattr_setflags(
            &attributes, (short)POSIX_SPAWN_SETPGROUP
        );
    }
    if (operation_result == 0) {
        operation_result = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (operation_result != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not configure isolated xemu helper"
        );
        goto cleanup;
    }
    operation_result = posix_spawn(
        &child,
        executable,
        &actions,
        &attributes,
        child_arguments,
        child_environment.values
    );
    if (operation_result != 0) {
        gdox_error_set(error, GDOX_ERROR_IO, "could not start xemu helper");
        goto cleanup;
    }
    (void)close(output_pipe[1]);
    output_pipe[1] = -1;
    (void)close(error_pipe[1]);
    error_pipe[1] = -1;
    if (!set_nonblocking(output_pipe[0], error)
        || !set_nonblocking(error_pipe[0], error)) {
        goto cleanup;
    }
    while (elapsed <= timeout_ms) {
        pid_t waited;

        if (!capture_output(
                output_pipe[0], output->output, &output->output_bytes,
                &output_open, &output->overflow, error
            ) || !capture_output(
                error_pipe[0], output->diagnostics,
                &output->diagnostic_bytes, &diagnostic_open,
                &output->overflow, error
            )) {
            goto cleanup;
        }
        if (!reaped) {
            do {
                waited = waitpid(child, &status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == child) {
                reaped = true;
            } else if (waited < 0) {
                gdox_error_set(
                    error, GDOX_ERROR_IO, "could not wait for xemu helper"
                );
                goto cleanup;
            }
        }
        if (reaped && !output_open && !diagnostic_open) {
            break;
        }
        gdox_sleep_ms(GDOX_XEMU_HELPER_POLL_MS);
        elapsed = timeout_ms - elapsed < GDOX_XEMU_HELPER_POLL_MS
            ? timeout_ms + 1U : elapsed + GDOX_XEMU_HELPER_POLL_MS;
    }
    if (!reaped || output_open || diagnostic_open) {
        output->timed_out = true;
        success = true;
        goto cleanup;
    }
    output->exit_code = WIFEXITED(status)
        ? WEXITSTATUS(status)
        : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    child = 0;
    success = true;

cleanup:
    if (child > 0) {
        (void)kill(-child, SIGKILL);
        (void)kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    }
    if (actions_ready) {
        (void)posix_spawn_file_actions_destroy(&actions);
    }
    if (attributes_ready) {
        (void)posix_spawnattr_destroy(&attributes);
    }
    if (output_pipe[0] >= 0 && output_open) {
        (void)close(output_pipe[0]);
    }
    if (output_pipe[1] >= 0) {
        (void)close(output_pipe[1]);
    }
    if (error_pipe[0] >= 0 && diagnostic_open) {
        (void)close(error_pipe[0]);
    }
    if (error_pipe[1] >= 0) {
        (void)close(error_pipe[1]);
    }
    gdox_xemu_environment_destroy(&child_environment);
    if (!gdox_session_storage_cleanup(&temporary, &cleanup_error)) {
        if (success || !gdox_error_is_set(error)) {
            *error = cleanup_error;
        }
        success = false;
    }
    return success;
}
