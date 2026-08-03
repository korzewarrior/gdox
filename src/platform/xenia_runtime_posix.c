#define _POSIX_C_SOURCE 200809L

#include "gdox/xenia.h"

#include "platform/portable_sync.h"
#include "platform/xenia_bridge_linux.h"

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
    GDOX_XENIA_PREFLIGHT_POLL_MS = 50U,
    GDOX_XENIA_PREFLIGHT_TIMEOUT_MS = 10000U,
};

static bool regular_file(const char *path)
{
    struct stat status;

    return path != NULL && path[0] != '\0'
        && stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static bool executable_file(const char *path)
{
    return regular_file(path) && access(path, X_OK) == 0;
}

static bool copy_path(
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    const char *path,
    gdox_error *error
)
{
    const size_t bytes = path != NULL ? strlen(path) : 0U;

    if (bytes == 0U || bytes >= GDOX_EMULATOR_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia runtime path is invalid or too long"
        );
        return false;
    }
    memcpy(output, path, bytes + 1U);
    return true;
}

static bool join_path(
    const char *base,
    const char *suffix,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    const int written = base != NULL && base[0] != '\0'
        ? snprintf(
            output,
            GDOX_EMULATOR_PATH_CAPACITY,
            "%s/%s",
            base,
            suffix
        )
        : -1;

    return written >= 0
        && (size_t)written < GDOX_EMULATOR_PATH_CAPACITY;
}

static bool parent_path(
    const char *path,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    const char *slash;
    size_t bytes;

    if (path == NULL) {
        return false;
    }
    slash = strrchr(path, '/');
    if (slash == NULL || slash == path) {
        return false;
    }
    bytes = (size_t)(slash - path);
    if (bytes >= GDOX_EMULATOR_PATH_CAPACITY) {
        return false;
    }
    memcpy(output, path, bytes);
    output[bytes] = '\0';
    return true;
}

static bool executable_directory(
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    const ssize_t bytes = readlink(
        "/proc/self/exe",
        executable,
        sizeof(executable) - 1U
    );
    if (bytes <= 0 || (size_t)bytes >= sizeof(executable)) {
        return false;
    }
    executable[bytes] = '\0';
    return parent_path(executable, output);
}

static bool finish_preflight(pid_t child, gdox_error *error)
{
    uint32_t waited = 0U;
    int status = 0;

    for (;;) {
        pid_t result;

        do {
            result = waitpid(child, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == child) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return true;
            }
            gdox_error_set(
                error,
                GDOX_ERROR_NOT_FOUND,
                "Xenia runtime preflight failed"
            );
            return false;
        }
        if (result < 0) {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "could not finish Xenia runtime preflight"
            );
            return false;
        }
        if (waited >= GDOX_XENIA_PREFLIGHT_TIMEOUT_MS) {
            break;
        }
        gdox_sleep_ms(GDOX_XENIA_PREFLIGHT_POLL_MS);
        waited += GDOX_XENIA_PREFLIGHT_POLL_MS;
    }

    (void)kill(-child, SIGKILL);
    (void)kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    gdox_error_set(
        error,
        GDOX_ERROR_IO,
        "Xenia runtime preflight timed out"
    );
    return false;
}

static bool preflight_launcher(const char *launcher, gdox_error *error)
{
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    char *arguments[] = {(char *)launcher, "--gdox-preflight", NULL};
    pid_t child;
    int result;
    bool actions_initialized;
    bool attributes_initialized;

    result = posix_spawn_file_actions_init(&actions);
    actions_initialized = result == 0;
    if (result == 0) {
        result = posix_spawn_file_actions_addopen(
            &actions,
            STDOUT_FILENO,
            "/dev/null",
            O_WRONLY,
            0
        );
    }
    if (result == 0) {
        result = posix_spawn_file_actions_addopen(
            &actions,
            STDERR_FILENO,
            "/dev/null",
            O_WRONLY,
            0
        );
    }
    if (result == 0) {
        result = posix_spawnattr_init(&attributes);
    }
    attributes_initialized = result == 0;
    if (result == 0) {
        result = posix_spawnattr_setflags(
            &attributes, (short)POSIX_SPAWN_SETPGROUP
        );
    }
    if (result == 0) {
        result = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (result != 0) {
        if (actions_initialized) {
            (void)posix_spawn_file_actions_destroy(&actions);
        }
        if (attributes_initialized) {
            (void)posix_spawnattr_destroy(&attributes);
        }
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not configure Xenia runtime preflight"
        );
        return false;
    }
    result = posix_spawn(
        &child,
        launcher,
        &actions,
        &attributes,
        arguments,
        environ
    );
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)posix_spawnattr_destroy(&attributes);
    if (result != 0) {
        gdox_error_set(
            error,
            result == ENOENT ? GDOX_ERROR_NOT_FOUND : GDOX_ERROR_IO,
            "could not start Xenia runtime preflight"
        );
        return false;
    }
    return finish_preflight(child, error);
}

static bool accept_candidate(
    const char *launcher,
    const char *payload,
    const gdox_xenia_runtime *runtime,
    gdox_xenia_runtime_origin origin,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    if (!executable_file(launcher) || !regular_file(payload)) {
        return false;
    }
    if (!gdox_xenia_verify_payload(payload, runtime, error)
        || !preflight_launcher(launcher, error)
        || !copy_path(output->launcher, launcher, error)
        || !copy_path(output->payload, payload, error)) {
        return false;
    }
    output->definition = runtime;
    output->origin = origin;
    return true;
}

static bool runtime_candidate(
    const char *root,
    const gdox_xenia_runtime *runtime,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    char directory[GDOX_EMULATOR_PATH_CAPACITY];
    char launcher[GDOX_EMULATOR_PATH_CAPACITY];
    char payload[GDOX_EMULATOR_PATH_CAPACITY];
    char suffix[96];
    int written;

    written = runtime != NULL && runtime->revision != NULL
        ? snprintf(suffix, sizeof(suffix), "xenia/%s", runtime->revision)
        : -1;
    return written >= 0 && (size_t)written < sizeof(suffix)
        && join_path(root, suffix, directory)
        && join_path(directory, "xenia", launcher)
        && join_path(directory, runtime->payload_name, payload)
        && accept_candidate(
            launcher,
            payload,
            runtime,
            GDOX_XENIA_RUNTIME_BUNDLED,
            output,
            error
        );
}

static bool override_candidate(
    const char *override,
    const gdox_xenia_runtime *runtime,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    char directory[GDOX_EMULATOR_PATH_CAPACITY];
    char payload[GDOX_EMULATOR_PATH_CAPACITY];

    return parent_path(override, directory)
        && join_path(directory, runtime->payload_name, payload)
        && accept_candidate(
            override,
            payload,
            runtime,
            GDOX_XENIA_RUNTIME_OVERRIDE,
            output,
            error
        );
}

bool gdox_xenia_resolve_runtime(
    const gdox_xenia_runtime *runtime,
    const char *override,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    const char *runtime_root = getenv("GDOX_RUNTIME_DIR");
    const char *home = getenv("HOME");
    const char *data = getenv("XDG_DATA_HOME");
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char root[GDOX_EMULATOR_PATH_CAPACITY];

    gdox_error_clear(error);
    if (output != NULL) {
        memset(output, 0, sizeof(*output));
    }
    if (runtime == NULL || output == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "reviewed Xenia runtime and descriptor output are required"
        );
        return false;
    }
    if (override != NULL && override[0] != '\0') {
        if (override_candidate(override, runtime, output, error)) {
            return true;
        }
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "selected Xenia launcher is incomplete"
            );
        }
        return false;
    }
    if (runtime_candidate(runtime_root, runtime, output, error)) {
        return true;
    }
    gdox_error_clear(error);
    if (executable_directory(executable)
        && join_path(executable, "runtime", root)
        && runtime_candidate(root, runtime, output, error)) {
        return true;
    }
    gdox_error_clear(error);
    if (executable_directory(executable)
        && join_path(executable, "../runtime", root)
        && runtime_candidate(root, runtime, output, error)) {
        return true;
    }
    gdox_error_clear(error);
    if (data != NULL && data[0] == '/'
        && join_path(data, "gdox/runtime", root)
        && runtime_candidate(root, runtime, output, error)) {
        return true;
    }
    gdox_error_clear(error);
    if (home != NULL && home[0] != '\0'
        && join_path(home, ".local/share/gdox/runtime", root)
        && runtime_candidate(root, runtime, output, error)) {
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        runtime->requires_proton
            ? "verified Xenia runtime and Proton Experimental were not found"
            : "verified native Xenia runtime was not found"
    );
    return false;
}

bool gdox_xenia_target_supported(gdox_xenia_target_kind kind)
{
    return kind == GDOX_XENIA_TARGET_IMAGE
        || kind == GDOX_XENIA_TARGET_PRIVATE_NBD;
}

bool gdox_xenia_runtime_target_supported(
    const gdox_xenia_runtime *runtime,
    gdox_xenia_target_kind kind
)
{
    return runtime != NULL && gdox_xenia_target_supported(kind);
}

bool gdox_xenia_target_preflight(
    gdox_xenia_target_kind kind,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (kind != GDOX_XENIA_TARGET_IMAGE
        && kind != GDOX_XENIA_TARGET_PRIVATE_NBD) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia target kind is invalid"
        );
        return false;
    }
    if (!gdox_xenia_target_supported(kind)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            kind == GDOX_XENIA_TARGET_PRIVATE_NBD
                ? "live Xbox 360 playback requires the Linux read-only bridge"
                : "Xbox 360 playback is unavailable on this platform"
        );
        return false;
    }
    if (kind == GDOX_XENIA_TARGET_PRIVATE_NBD) {
        return gdox_xenia_bridge_preflight(error);
    }
    return true;
}
