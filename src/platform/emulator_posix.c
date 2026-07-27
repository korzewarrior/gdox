#define _POSIX_C_SOURCE 200809L

#include "gdox/emulator.h"

#include "platform/emulator_configuration.h"
#include "platform/portable_sync.h"

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

#if defined(__APPLE__)
#define GDOX_XEMU_CONFIGURATION_RELATIVE \
    "Library/Application Support/xemu/xemu/xemu.toml"
#else
#define GDOX_XEMU_CONFIGURATION_RELATIVE ".local/share/xemu/xemu/xemu.toml"
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern char **environ;

struct gdox_emulator_process {
    pid_t pid;
    bool reaped;
    int exit_code;
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

static bool copy_path(char *output, const char *path, gdox_error *error)
{
    const size_t bytes = strlen(path);
    if (bytes >= GDOX_EMULATOR_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator path is too long");
        return false;
    }
    memcpy(output, path, bytes + 1U);
    return true;
}

static bool join_home(
    const char *home,
    const char *suffix,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    const int result = snprintf(
        output,
        GDOX_EMULATOR_PATH_CAPACITY,
        "%s/%s",
        home,
        suffix
    );
    return result >= 0 && (size_t)result < GDOX_EMULATOR_PATH_CAPACITY;
}

static bool join_path(
    const char *base,
    const char *suffix,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    const int result = snprintf(
        output,
        GDOX_EMULATOR_PATH_CAPACITY,
        "%s/%s",
        base,
        suffix
    );
    return result >= 0 && (size_t)result < GDOX_EMULATOR_PATH_CAPACITY;
}

static bool executable_directory(
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char *slash;
#if defined(__APPLE__)
    uint32_t capacity = (uint32_t)sizeof(executable);
    if (_NSGetExecutablePath(executable, &capacity) != 0) {
        return false;
    }
#else
    const ssize_t bytes = readlink(
        "/proc/self/exe",
        executable,
        sizeof(executable) - 1U
    );
    if (bytes <= 0 || (size_t)bytes >= sizeof(executable)) {
        return false;
    }
    executable[bytes] = '\0';
#endif
    slash = strrchr(executable, '/');
    if (slash == NULL) {
        return false;
    }
    *slash = '\0';
    return snprintf(
        output,
        GDOX_EMULATOR_PATH_CAPACITY,
        "%s",
        executable
    ) >= 0;
}

static const char *bundled_xemu_suffix(void)
{
#if defined(__APPLE__)
    return "xemu/xemu.app/Contents/MacOS/xemu";
#else
    return "xemu/AppDir/AppRun";
#endif
}

static bool runtime_candidate(
    const char *runtime_root,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    return runtime_root != NULL && runtime_root[0] != '\0'
        && join_path(runtime_root, bundled_xemu_suffix(), output)
        && executable_file(output);
}

static bool bundled_xemu(
    const char *home,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    const char *explicit_runtime = getenv("GDOX_RUNTIME_DIR");
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char runtime[GDOX_EMULATOR_PATH_CAPACITY];

    if (runtime_candidate(explicit_runtime, output)) {
        return true;
    }
    if (executable_directory(executable)) {
#if defined(__APPLE__)
        if (join_path(executable, "../Resources/runtime", runtime)
            && runtime_candidate(runtime, output)) {
            return true;
        }
#endif
        if (join_path(executable, "runtime", runtime)
            && runtime_candidate(runtime, output)) {
            return true;
        }
        if (join_path(executable, "../runtime", runtime)
            && runtime_candidate(runtime, output)) {
            return true;
        }
    }
    if (home == NULL || home[0] == '\0') {
        return false;
    }
#if defined(__APPLE__)
    return join_home(
        home,
        "Library/Application Support/org.gdox.gdox/runtime",
        runtime
    ) && runtime_candidate(runtime, output);
#else
    {
        const char *data = getenv("XDG_DATA_HOME");
        if (data != NULL && data[0] == '/'
            && join_path(data, "gdox/runtime", runtime)
            && runtime_candidate(runtime, output)) {
            return true;
        }
        return join_home(home, ".local/share/gdox/runtime", runtime)
            && runtime_candidate(runtime, output);
    }
#endif
}

static bool managed_configuration_path(
    const char *home,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    if (home == NULL || home[0] == '\0') {
        return false;
    }
#if defined(__APPLE__)
    return join_home(
        home,
        "Library/Application Support/org.gdox.gdox/xemu/xemu.toml",
        output
    );
#else
    {
        const char *data = getenv("XDG_DATA_HOME");
        if (data != NULL && data[0] == '/') {
            return join_path(data, "gdox/xemu/xemu.toml", output);
        }
        return join_home(home, ".local/share/gdox/xemu/xemu.toml", output);
    }
#endif
}

static bool managed_configuration(
    const char *home,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    return managed_configuration_path(home, output) && regular_file(output);
}

static bool create_parent_directories(const char *path)
{
    char partial[GDOX_EMULATOR_PATH_CAPACITY];
    size_t index;
    const size_t bytes = strlen(path);

    if (bytes >= sizeof(partial)) {
        return false;
    }
    memcpy(partial, path, bytes + 1U);
    for (index = 1U; index < bytes; ++index) {
        if (partial[index] != '/') {
            continue;
        }
        partial[index] = '\0';
        if (mkdir(partial, 0700) != 0 && errno != EEXIST) {
            return false;
        }
        partial[index] = '/';
    }
    return true;
}

static bool write_private_atomic(
    const char *path,
    const char *text,
    gdox_error *error
);
static bool read_text_file(const char *path, char **text, gdox_error *error);

/*
 * GDOX never edits an external xemu installation's own configuration. The
 * first time one is discovered, its configuration is copied into GDOX's
 * managed location and all later updates apply to the copy.
 */
static bool adopt_external_configuration(
    const char *home,
    const char *source_path,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    char *text = NULL;
    bool copied;

    if (!managed_configuration_path(home, output)
        || !create_parent_directories(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not prepare the managed xemu configuration location"
        );
        return false;
    }
    if (!read_text_file(source_path, &text, error)) {
        return false;
    }
    copied = write_private_atomic(output, text, error);
    free(text);
    return copied;
}

static bool resolve_in_path(
    const char *name,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    const char *path = getenv("PATH");
    const char *cursor;

    if (path == NULL) {
        return false;
    }
    cursor = path;
    while (*cursor != '\0') {
        const char *separator = strchr(cursor, ':');
        const size_t directory_bytes =
            separator != NULL ? (size_t)(separator - cursor) : strlen(cursor);
        const int result = snprintf(
            output,
            GDOX_EMULATOR_PATH_CAPACITY,
            "%.*s%s%s",
            (int)directory_bytes,
            cursor,
            directory_bytes == 0U ? "" : "/",
            name
        );
        if (result >= 0 && (size_t)result < GDOX_EMULATOR_PATH_CAPACITY
            && executable_file(output)) {
            return true;
        }
        if (separator == NULL) {
            break;
        }
        cursor = separator + 1U;
    }
    output[0] = '\0';
    return false;
}

bool gdox_emulator_discover_executable(
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    const char *explicit_xemu = getenv("GDOX_XEMU");
    const char *home = getenv("HOME");
    char candidate[GDOX_EMULATOR_PATH_CAPACITY];

    gdox_error_clear(error);
    if (output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator path output is required");
        return false;
    }
    output[0] = '\0';
    if (explicit_xemu != NULL && executable_file(explicit_xemu)) {
        return copy_path(output, explicit_xemu, error);
    }
    if (bundled_xemu(home, candidate)) {
        return copy_path(output, candidate, error);
    }
    if (home != NULL
        && join_home(home, ".local/bin/xemu", candidate)
        && executable_file(candidate)) {
        return copy_path(output, candidate, error);
    }
    if (resolve_in_path("xemu", output)) {
        return true;
    }
    gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "xemu executable was not found");
    return false;
}

bool gdox_emulator_validate_executable(
    const char *path,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (!executable_file(path)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "selected xemu executable is not a runnable file"
        );
        return false;
    }
    return true;
}

bool gdox_emulator_discover(gdox_emulator_paths *paths, gdox_error *error)
{
    const char *explicit_configuration;
    const char *configuration;
    const char *home;
    char candidate[GDOX_EMULATOR_PATH_CAPACITY];

    gdox_error_clear(error);
    if (paths == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator path output is required");
        return false;
    }
    memset(paths, 0, sizeof(*paths));
    home = getenv("HOME");
    if (!gdox_emulator_discover_executable(paths->executable, error)) {
        return false;
    }

    explicit_configuration = getenv("GDOX_XEMU_CONFIG");
    if (explicit_configuration != NULL && regular_file(explicit_configuration)) {
        configuration = explicit_configuration;
    } else if (managed_configuration(home, candidate)) {
        configuration = candidate;
    } else {
        char external[GDOX_EMULATOR_PATH_CAPACITY];

        if (home == NULL
            || !join_home(home, GDOX_XEMU_CONFIGURATION_RELATIVE, external)
            || !regular_file(external)) {
            gdox_error_set(
                error,
                GDOX_ERROR_NOT_FOUND,
                "xemu configuration was not found"
            );
            return false;
        }
        if (!adopt_external_configuration(home, external, candidate, error)) {
            return false;
        }
        configuration = candidate;
    }
    return copy_path(paths->configuration, configuration, error);
}

static bool read_text_file(const char *path, char **text, gdox_error *error)
{
    struct stat status;
    int file;
    char *data;
    size_t completed = 0U;

    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode)
        || status.st_size < 0
        || status.st_size > (off_t)16 * 1024 * 1024) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "xemu configuration is unavailable or too large");
        return false;
    }
    data = malloc((size_t)status.st_size + 1U);
    if (data == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate xemu configuration");
        return false;
    }
    file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        free(data);
        gdox_error_set(error, GDOX_ERROR_IO, "could not open xemu configuration");
        return false;
    }
    while (completed < (size_t)status.st_size) {
        const ssize_t received = read(
            file,
            data + completed,
            (size_t)status.st_size - completed
        );
        if (received > 0) {
            completed += (size_t)received;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            (void)close(file);
            free(data);
            gdox_error_set(error, GDOX_ERROR_IO, "could not read xemu configuration");
            return false;
        }
    }
    (void)close(file);
    data[completed] = '\0';
    *text = data;
    return true;
}

static bool write_private_atomic(
    const char *path,
    const char *text,
    gdox_error *error
)
{
    char temporary[GDOX_EMULATOR_PATH_CAPACITY + 48U];
    int file;
    size_t bytes = strlen(text);
    size_t completed = 0U;
    int formatted;

    formatted = snprintf(
        temporary,
        sizeof(temporary),
        "%s.gdox.%ld.tmp",
        path,
        (long)getpid()
    );
    if (formatted < 0 || (size_t)formatted >= sizeof(temporary)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "xemu configuration path is too long");
        return false;
    }
    (void)unlink(temporary);
    file = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file < 0) {
        gdox_error_set(error, GDOX_ERROR_IO, "could not create private xemu configuration update");
        return false;
    }
    while (completed < bytes) {
        const ssize_t written = write(file, text + completed, bytes - completed);
        if (written > 0) {
            completed += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            (void)close(file);
            (void)unlink(temporary);
            gdox_error_set(error, GDOX_ERROR_IO, "could not write xemu configuration");
            return false;
        }
    }
    if (fsync(file) != 0 || close(file) != 0 || rename(temporary, path) != 0) {
        (void)unlink(temporary);
        gdox_error_set(error, GDOX_ERROR_IO, "could not commit xemu configuration");
        return false;
    }
    return true;
}

bool gdox_emulator_prepare(
    const gdox_emulator_options *options,
    gdox_error *error
)
{
    char *original = NULL;
    char *updated = NULL;
    bool success = false;

    gdox_error_clear(error);
    if (options == NULL || !executable_file(options->executable)
        || !regular_file(options->configuration)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid xemu paths are required");
        return false;
    }
    if (!read_text_file(options->configuration, &original, error)
        || !gdox_emulator_configuration_update(
            options,
            original,
            &updated,
            error
        )) {
        goto cleanup;
    }
    if (strcmp(original, updated) != 0
        && !write_private_atomic(options->configuration, updated, error)) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(original);
    free(updated);
    return success;
}

bool gdox_emulator_launch(
    const gdox_emulator_options *options,
    const char *dvd_uri,
    gdox_emulator_process **process_output,
    gdox_error *error
)
{
    gdox_emulator_process *process;
    posix_spawn_file_actions_t actions;
    char *arguments[9];
    size_t argument = 0U;
    int spawn_result;
    int actions_result;

    gdox_error_clear(error);
    if (options == NULL || dvd_uri == NULL || dvd_uri[0] == '\0'
        || process_output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator options, disc URI, and process output are required");
        return false;
    }
    if (!gdox_emulator_prepare(options, error)) {
        return false;
    }
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate emulator process");
        return false;
    }
    arguments[argument++] = (char *)options->executable;
    arguments[argument++] = "-config_path";
    arguments[argument++] = (char *)options->configuration;
    if (options->fullscreen) {
        arguments[argument++] = "-full-screen";
    }
    arguments[argument++] = "-dvd_path";
    arguments[argument++] = (char *)dvd_uri;
    arguments[argument] = NULL;

    actions_result = posix_spawn_file_actions_init(&actions);
    if (actions_result != 0) {
        free(process);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize xemu process actions");
        return false;
    }
    if (!options->console_output) {
        actions_result = posix_spawn_file_actions_addopen(
            &actions,
            STDOUT_FILENO,
            "/dev/null",
            O_WRONLY,
            0
        );
        if (actions_result == 0) {
            actions_result = posix_spawn_file_actions_addopen(
                &actions,
                STDERR_FILENO,
                "/dev/null",
                O_WRONLY,
                0
            );
        }
    }
    if (actions_result != 0) {
        (void)posix_spawn_file_actions_destroy(&actions);
        free(process);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not configure xemu process output");
        return false;
    }
    spawn_result = posix_spawn(
        &process->pid,
        options->executable,
        &actions,
        NULL,
        arguments,
        environ
    );
    (void)posix_spawn_file_actions_destroy(&actions);
    if (spawn_result != 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        (void)snprintf(message, sizeof(message), "could not launch xemu: %s", strerror(spawn_result));
        free(process);
        gdox_error_set(error, GDOX_ERROR_IO, message);
        return false;
    }
    *process_output = process;
    return true;
}

static int status_exit_code(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

bool gdox_emulator_poll(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    int status;
    pid_t result;

    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and status outputs are required");
        return false;
    }
    if (process->reaped) {
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    result = waitpid(process->pid, &status, WNOHANG);
    if (result == 0) {
        *running = true;
        *exit_code = 0;
        return true;
    }
    if (result == process->pid) {
        process->reaped = true;
        process->exit_code = status_exit_code(status);
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    gdox_error_set(error, GDOX_ERROR_IO, "could not query xemu process");
    return false;
}

bool gdox_emulator_stop(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    uint32_t waited = 0U;
    bool running;

    gdox_error_clear(error);
    if (process == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and exit code are required");
        return false;
    }
    if (!gdox_emulator_poll(process, &running, exit_code, error) || !running) {
        return !gdox_error_is_set(error);
    }
    if (kill(process->pid, SIGTERM) != 0 && errno != ESRCH) {
        gdox_error_set(error, GDOX_ERROR_IO, "could not request xemu shutdown");
        return false;
    }
    while (waited < grace_ms) {
        gdox_sleep_ms(50U);
        waited += 50U;
        if (!gdox_emulator_poll(process, &running, exit_code, error) || !running) {
            return !gdox_error_is_set(error);
        }
    }
    if (kill(process->pid, SIGKILL) != 0 && errno != ESRCH) {
        gdox_error_set(error, GDOX_ERROR_IO, "could not stop xemu");
        return false;
    }
    for (;;) {
        if (!gdox_emulator_poll(process, &running, exit_code, error) || !running) {
            return !gdox_error_is_set(error);
        }
        gdox_sleep_ms(10U);
    }
}

void gdox_emulator_process_destroy(gdox_emulator_process *process)
{
    if (process != NULL) {
        int exit_code;
        gdox_error ignored;
        if (!process->reaped) {
            (void)gdox_emulator_stop(process, 1000U, &exit_code, &ignored);
        }
        free(process);
    }
}
