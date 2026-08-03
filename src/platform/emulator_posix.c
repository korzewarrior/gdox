#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "gdox/emulator.h"

#include "core/emulator_configuration.h"
#include "platform/portable_sync.h"
#include "platform/session_storage.h"
#include "platform/xemu_runtime_session.h"

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

struct gdox_emulator_process {
    pid_t pid;
    bool reaped;
    int exit_code;
    gdox_session_storage session;
};

static bool cleanup_process_session(
    gdox_emulator_process *process,
    gdox_error *error
)
{
    return !process->session.active
        || gdox_session_storage_cleanup(&process->session, error);
}

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
    return "xemu/xemu";
#endif
}

static bool runtime_candidate(
    const char *runtime_root,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    if (runtime_root == NULL || runtime_root[0] == '\0'
        || !join_path(runtime_root, bundled_xemu_suffix(), output)
        || !executable_file(output)) {
        return false;
    }
#if defined(__APPLE__)
    return true;
#else
    {
        char app_run[GDOX_EMULATOR_PATH_CAPACITY];

        return join_path(runtime_root, "xemu/AppDir/AppRun", app_run)
            && executable_file(app_run);
    }
#endif
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

static bool write_private_atomic(
    const char *path,
    const char *text,
    gdox_error *error
);
static bool read_text_file(const char *path, char **text, gdox_error *error);

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

bool gdox_emulator_discover_configuration(
    const char *executable,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    bool *required,
    gdox_error *error
)
{
    const char *explicit_configuration;
    const char *home;

    gdox_error_clear(error);
    (void)executable;
    if (output == NULL || required == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "emulator configuration outputs are required"
        );
        return false;
    }
    output[0] = '\0';
    *required = false;
    home = getenv("HOME");
    explicit_configuration = getenv("GDOX_XEMU_CONFIG");
    if (explicit_configuration != NULL
        && explicit_configuration[0] != '\0') {
        *required = true;
        return copy_path(output, explicit_configuration, error);
    }
    if (home != NULL
        && join_home(
            home,
            GDOX_XEMU_CONFIGURATION_RELATIVE,
            output
        )
        && regular_file(output)) {
        return true;
    }
    output[0] = '\0';
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        "xemu configuration was not found"
    );
    return false;
}

static bool read_text_file(const char *path, char **text, gdox_error *error)
{
    struct stat status;
    int file;
    char *data;
    size_t completed = 0U;

    file = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (file < 0) {
        gdox_error_set(error, GDOX_ERROR_IO, "could not open xemu configuration");
        return false;
    }
    if (fstat(file, &status) != 0 || !S_ISREG(status.st_mode)
        || status.st_size < 0
        || status.st_size > (off_t)16 * 1024 * 1024) {
        (void)close(file);
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "xemu configuration is unavailable or too large");
        return false;
    }
    data = malloc((size_t)status.st_size + 1U);
    if (data == NULL) {
        (void)close(file);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate xemu configuration");
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
    bool persistent_save_export;
    bool success = false;

    gdox_error_clear(error);
    if (options == NULL || !executable_file(options->executable)
        || options->configuration == NULL
        || options->configuration[0] == '\0') {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid xemu paths are required");
        return false;
    }
    if (!gdox_emulator_query_storage_capabilities(
            options->executable, &persistent_save_export, error
        )) {
        goto cleanup;
    }
    if (!persistent_save_export) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "xemu does not provide persistent logical save export"
        );
        goto cleanup;
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
    gdox_emulator_process *process = NULL;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    gdox_xemu_environment environment = {0};
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char save_vault[GDOX_EMULATOR_PATH_CAPACITY];
    char *arguments[11];
    size_t argument = 0U;
    int spawn_result = 0;
    int actions_result = 0;
    bool actions_initialized = false;
    bool attributes_initialized = false;
    bool success = false;

    gdox_error_clear(error);
    if (process_output != NULL) {
        *process_output = NULL;
    }
    if (options == NULL || options->save_vault == NULL
        || options->save_vault[0] == '\0'
        || dvd_uri == NULL || dvd_uri[0] == '\0'
        || process_output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator options, save vault, disc URI, and process output are required");
        return false;
    }
    if (!gdox_emulator_prepare(options, error)) {
        return false;
    }
    if (realpath(options->executable, executable) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "xemu executable path is unavailable"
        );
        return false;
    }
    if (realpath(options->configuration, configuration) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "xemu configuration path is unavailable"
        );
        return false;
    }
    if (realpath(options->save_vault, save_vault) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "xemu save vault is unavailable"
        );
        return false;
    }
    {
        struct stat status;
        struct stat named;

        if (lstat(options->save_vault, &named) != 0
            || !S_ISDIR(named.st_mode) || named.st_uid != geteuid()
            || (named.st_mode & 077U) != 0U
            || stat(save_vault, &status) != 0 || !S_ISDIR(status.st_mode)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "xemu save vault is not a private current-user directory"
            );
            return false;
        }
    }
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate emulator process");
        return false;
    }
    if (!gdox_xemu_runtime_session_open(&process->session, error)
        || !gdox_xemu_environment_create(
            process->session.root, &environment, error
        )) {
        goto cleanup;
    }
    arguments[argument++] = executable;
    arguments[argument++] = "--gdox-runtime";
    arguments[argument++] = "--gdox-save-vault";
    arguments[argument++] = save_vault;
    arguments[argument++] = "-config_path";
    arguments[argument++] = configuration;
    if (options->fullscreen) {
        arguments[argument++] = "-full-screen";
    }
    arguments[argument++] = "-dvd_path";
    arguments[argument++] = (char *)dvd_uri;
    arguments[argument] = NULL;

    actions_result = posix_spawn_file_actions_init(&actions);
    actions_initialized = actions_result == 0;
    if (actions_result != 0) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize xemu process actions");
        goto cleanup;
    }
    actions_result = posix_spawn_file_actions_addchdir_np(
        &actions, process->session.root
    );
    if (actions_result == 0 && !options->console_output) {
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
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not configure isolated xemu process actions"
        );
        goto cleanup;
    }
    actions_result = posix_spawnattr_init(&attributes);
    attributes_initialized = actions_result == 0;
    if (actions_result == 0) {
        actions_result = posix_spawnattr_setflags(
            &attributes, (short)POSIX_SPAWN_SETPGROUP
        );
    }
    if (actions_result == 0) {
        actions_result = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (actions_result != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not isolate the xemu process group"
        );
        goto cleanup;
    }
    spawn_result = posix_spawn(
        &process->pid,
        executable,
        &actions,
        &attributes,
        arguments,
        environment.values
    );
    if (spawn_result != 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        (void)snprintf(message, sizeof(message), "could not launch xemu: %s", strerror(spawn_result));
        gdox_error_set(error, GDOX_ERROR_IO, message);
        goto cleanup;
    }
    success = true;
    *process_output = process;

cleanup:
    if (attributes_initialized) {
        (void)posix_spawnattr_destroy(&attributes);
    }
    if (actions_initialized) {
        (void)posix_spawn_file_actions_destroy(&actions);
    }
    gdox_xemu_environment_destroy(&environment);
    if (!success && process != NULL) {
        gdox_error cleanup_error;

        if (!cleanup_process_session(process, &cleanup_error)) {
            char message[GDOX_ERROR_MESSAGE_CAPACITY];

            (void)snprintf(
                message,
                sizeof(message),
                "%.96s; xemu session cleanup failed: %.96s",
                gdox_error_is_set(error) ? error->message : "xemu launch failed",
                cleanup_error.message
            );
            gdox_error_set(error, GDOX_ERROR_IO, message);
        }
        free(process);
    }
    return success;
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

enum {
    GDOX_EMULATOR_STOP_POLL_MS = 50U,
    GDOX_EMULATOR_FORCED_REAP_MS = 5000U,
};

static void mark_reaped(gdox_emulator_process *process, int exit_code)
{
    process->pid = 0;
    process->reaped = true;
    process->exit_code = exit_code;
}

static bool query_process(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    int status;
    pid_t result;

    if (process->reaped) {
        if (!cleanup_process_session(process, error)) {
            return false;
        }
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    if (process->pid <= 0) {
        mark_reaped(process, -1);
        if (!cleanup_process_session(process, error)) {
            return false;
        }
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    do {
        result = waitpid(process->pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        *running = true;
        *exit_code = 0;
        return true;
    }
    if (result == process->pid) {
        mark_reaped(process, status_exit_code(status));
        if (!cleanup_process_session(process, error)) {
            return false;
        }
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    if (result < 0 && errno == ECHILD) {
        mark_reaped(process, -1);
        if (!cleanup_process_session(process, error)) {
            return false;
        }
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    gdox_error_set(error, GDOX_ERROR_IO, "could not query xemu process");
    return false;
}

bool gdox_emulator_poll(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and status outputs are required");
        return false;
    }
    return query_process(process, running, exit_code, error);
}

static void remember_error(gdox_error *saved, const gdox_error *candidate)
{
    if (!gdox_error_is_set(saved) && gdox_error_is_set(candidate)) {
        *saved = *candidate;
    }
}

static bool wait_for_exit(
    gdox_emulator_process *process,
    uint32_t timeout_ms,
    int *exit_code,
    gdox_error *saved_error
)
{
    uint32_t waited = 0U;

    for (;;) {
        gdox_error query_error;
        bool running = true;
        int observed_exit = -1;

        gdox_error_clear(&query_error);
        if (query_process(
                process, &running, &observed_exit, &query_error
            )) {
            if (!running) {
                *exit_code = observed_exit;
                return true;
            }
        } else {
            remember_error(saved_error, &query_error);
        }
        if (waited >= timeout_ms) {
            return false;
        }
        {
            const uint32_t remaining = timeout_ms - waited;
            const uint32_t delay = remaining < GDOX_EMULATOR_STOP_POLL_MS
                ? remaining
                : GDOX_EMULATOR_STOP_POLL_MS;

            gdox_sleep_ms(delay);
            waited += delay;
        }
    }
}

static void request_signal(
    gdox_emulator_process *process,
    int signal_number,
    const char *message,
    gdox_error *saved_error
)
{
    gdox_error signal_error;
    int group_error;

    if (process->reaped || process->pid <= 0) {
        return;
    }
    if (kill(-process->pid, signal_number) == 0) {
        return;
    }
    group_error = errno;
    if (group_error == ESRCH
        && (kill(process->pid, signal_number) == 0 || errno == ESRCH)) {
        return;
    }
    gdox_error_set(&signal_error, GDOX_ERROR_IO, message);
    remember_error(saved_error, &signal_error);
}

static bool terminate_owned_process(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error saved_error;

    gdox_error_clear(&saved_error);
    if (wait_for_exit(process, 0U, exit_code, &saved_error)) {
        gdox_error_clear(error);
        return true;
    }
    request_signal(
        process,
        SIGTERM,
        "could not request xemu shutdown",
        &saved_error
    );
    if (wait_for_exit(process, grace_ms, exit_code, &saved_error)) {
        gdox_error_clear(error);
        return true;
    }
    request_signal(
        process, SIGKILL, "could not force xemu to stop", &saved_error
    );
    if (wait_for_exit(
            process,
            GDOX_EMULATOR_FORCED_REAP_MS,
            exit_code,
            &saved_error
        )) {
        gdox_error_clear(error);
        return true;
    }
    if (gdox_error_is_set(&saved_error)) {
        *error = saved_error;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "xemu did not exit after forced termination"
        );
    }
    *exit_code = -1;
    return false;
}

static void force_terminal_cleanup(gdox_emulator_process *process)
{
    int status;

    if (process->reaped || process->pid <= 0) {
        return;
    }
    (void)kill(-process->pid, SIGKILL);
    (void)kill(process->pid, SIGKILL);
    for (;;) {
        const pid_t result = waitpid(process->pid, &status, 0);

        if (result == process->pid) {
            mark_reaped(process, status_exit_code(status));
            return;
        }
        if (result < 0 && errno == ECHILD) {
            mark_reaped(process, -1);
            return;
        }
        if (result < 0 && errno != EINTR) {
            (void)kill(-process->pid, SIGKILL);
            (void)kill(process->pid, SIGKILL);
            gdox_sleep_ms(10U);
        }
    }
}

bool gdox_emulator_stop(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and exit code are required");
        return false;
    }
    return terminate_owned_process(process, grace_ms, exit_code, error);
}

void gdox_emulator_process_destroy(gdox_emulator_process *process)
{
    if (process != NULL) {
        int exit_code;
        gdox_error ignored;
        if (!process->reaped && process->pid > 0
            && !terminate_owned_process(
                process, 1000U, &exit_code, &ignored
            )) {
            force_terminal_cleanup(process);
        }
        (void)cleanup_process_session(process, &ignored);
        free(process);
    }
}
