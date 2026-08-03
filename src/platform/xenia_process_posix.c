#define _POSIX_C_SOURCE 200809L

#include "gdox/xenia.h"

#include "core/xenia_launch.h"
#include "platform/portable_sync.h"
#if defined(__linux__)
#include "platform/xenia_bridge_linux.h"

#include <dirent.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct gdox_xenia_child_environment {
    char **values;
    char **owned;
    size_t owned_count;
} gdox_xenia_child_environment;

typedef struct gdox_xenia_environment_value {
    const char *name;
    const char *value;
} gdox_xenia_environment_value;

typedef struct gdox_xenia_environment_path {
    const char *name;
    const char *suffix;
} gdox_xenia_environment_path;

#define GDOX_STRINGIFY_VALUE_(value) #value
#define GDOX_STRINGIFY_VALUE(value) GDOX_STRINGIFY_VALUE_(value)

static const gdox_xenia_environment_value gdox_xenia_fixed_environment[] = {
#define GDOX_XENIA_INJECTION_UNSET(name)
#define GDOX_XENIA_INJECTION_SET(name, value) \
    {#name, GDOX_STRINGIFY_VALUE(value)},
#include "platform/xenia_injection_environment.def"
#undef GDOX_XENIA_INJECTION_SET
#undef GDOX_XENIA_INJECTION_UNSET
    {"MESA_SHADER_CACHE_DISABLE", "1"},
    {"__GL_SHADER_DISK_CACHE", "0"},
    {"VKD3D_SHADER_CACHE_PATH", "0"},
    {"DXVK_SHADER_CACHE", "0"},
    {"DXVK_STATE_CACHE", "0"},
    {"DXVK_LOG_PATH", "none"},
    {"PROTON_LOG", "0"},
    {"PROTON_DUMP_DEBUG_COMMANDS", "0"},
};

static const gdox_xenia_environment_path gdox_xenia_path_environment[] = {
    {"HOME", "/home"},
    {"XDG_CACHE_HOME", "/xdg-cache"},
    {"XDG_CONFIG_HOME", "/xdg-config"},
    {"XDG_DATA_HOME", "/xdg-data"},
    {"XDG_STATE_HOME", "/xdg-state"},
    {"XDG_RUNTIME_DIR", "/xdg-runtime"},
    {"TMPDIR", "/tmp"},
    {"STEAM_COMPAT_DATA_PATH", "/proton"},
    {"STEAM_COMPAT_INSTALL_PATH", "/install"},
    {"STEAM_COMPAT_MEDIA_PATH", "/proton-media"},
    {"STEAM_COMPAT_TRANSCODED_MEDIA_PATH", "/proton-transcoded"},
    {"PROTON_LOG_DIR", "/proton-logs"},
    {"PROTON_DEBUG_DIR", "/proton-debug"},
    {"PROTON_CRASH_REPORT_DIR", "/proton-crash"},
    {"WINEPREFIX", "/proton/pfx"},
};

static const char gdox_xenia_discovery_home[] =
    "GDOX_XENIA_DISCOVERY_HOME";

static const char *const gdox_xenia_removed_environment[] = {
#define GDOX_XENIA_INJECTION_UNSET(name) #name,
#define GDOX_XENIA_INJECTION_SET(name, value)
#include "platform/xenia_injection_environment.def"
#undef GDOX_XENIA_INJECTION_SET
#undef GDOX_XENIA_INJECTION_UNSET
};

#undef GDOX_STRINGIFY_VALUE
#undef GDOX_STRINGIFY_VALUE_

static bool environment_has_name(const char *value, const char *name)
{
    const size_t bytes = strlen(name);

    return strncmp(value, name, bytes) == 0 && value[bytes] == '=';
}

static bool replaced_cache_environment(const char *value)
{
    size_t index;

    if (environment_has_name(value, gdox_xenia_discovery_home)) {
        return true;
    }
    for (index = 0U;
         index < sizeof(gdox_xenia_removed_environment)
            / sizeof(gdox_xenia_removed_environment[0]);
         ++index) {
        if (environment_has_name(
                value, gdox_xenia_removed_environment[index]
            )) {
            return true;
        }
    }
    for (index = 0U;
         index < sizeof(gdox_xenia_fixed_environment)
            / sizeof(gdox_xenia_fixed_environment[0]);
         ++index) {
        if (environment_has_name(
                value, gdox_xenia_fixed_environment[index].name
            )) {
            return true;
        }
    }
    for (index = 0U;
         index < sizeof(gdox_xenia_path_environment)
            / sizeof(gdox_xenia_path_environment[0]);
         ++index) {
        if (environment_has_name(
                value, gdox_xenia_path_environment[index].name
            )) {
            return true;
        }
    }
    return false;
}

static char *environment_assignment(
    const char *name,
    const char *value,
    const char *suffix
)
{
    const size_t name_bytes = strlen(name);
    const size_t value_bytes = strlen(value);
    const size_t suffix_bytes = strlen(suffix);
    size_t capacity;
    char *assignment;

    if (name_bytes > SIZE_MAX - value_bytes - suffix_bytes - 2U) {
        return NULL;
    }
    capacity = name_bytes + value_bytes + suffix_bytes + 2U;
    assignment = malloc(capacity);
    if (assignment == NULL) {
        return NULL;
    }
    (void)snprintf(
        assignment,
        capacity,
        "%s=%s%s",
        name,
        value,
        suffix
    );
    return assignment;
}

static void destroy_child_environment(
    gdox_xenia_child_environment *environment
)
{
    if (environment != NULL) {
        size_t index;

        for (index = 0U; index < environment->owned_count; ++index) {
            free(environment->owned[index]);
        }
        free(environment->owned);
        free(environment->values);
        memset(environment, 0, sizeof(*environment));
    }
}

static bool build_child_environment(
    const gdox_xenia_options *options,
    gdox_xenia_child_environment *output,
    gdox_error *error
)
{
    size_t inherited = 0U;
    size_t retained = 0U;
    size_t index;
    size_t output_index = 0U;
    const size_t fixed = sizeof(gdox_xenia_fixed_environment)
        / sizeof(gdox_xenia_fixed_environment[0]);
    const size_t paths = sizeof(gdox_xenia_path_environment)
        / sizeof(gdox_xenia_path_environment[0]);
    const char *original_home = getenv("HOME");
    const size_t discovery = options->runtime->definition->requires_proton
        && original_home != NULL && original_home[0] != '\0' ? 1U : 0U;

    memset(output, 0, sizeof(*output));
    while (environ[inherited] != NULL) {
        if (!replaced_cache_environment(environ[inherited])) {
            ++retained;
        }
        ++inherited;
    }
    if (retained > SIZE_MAX - fixed - paths - discovery - 1U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "Xenia environment is too large"
        );
        return false;
    }
    output->values = calloc(
        retained + fixed + paths + discovery + 1U,
        sizeof(*output->values)
    );
    output->owned = calloc(
        paths + fixed + discovery,
        sizeof(*output->owned)
    );
    if (output->values == NULL || output->owned == NULL) {
        destroy_child_environment(output);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate isolated Xenia environment"
        );
        return false;
    }
    for (index = 0U; index < inherited; ++index) {
        if (!replaced_cache_environment(environ[index])) {
            output->values[output_index++] = environ[index];
        }
    }
    for (index = 0U; index < paths; ++index) {
        char *assignment = environment_assignment(
            gdox_xenia_path_environment[index].name,
            options->cache_root,
            gdox_xenia_path_environment[index].suffix
        );

        if (assignment == NULL) {
            goto allocation_failed;
        }
        output->owned[output->owned_count++] = assignment;
        output->values[output_index++] = assignment;
    }
    for (index = 0U; index < fixed; ++index) {
        char *assignment = environment_assignment(
            gdox_xenia_fixed_environment[index].name,
            gdox_xenia_fixed_environment[index].value,
            ""
        );

        if (assignment == NULL) {
            goto allocation_failed;
        }
        output->owned[output->owned_count++] = assignment;
        output->values[output_index++] = assignment;
    }
    if (discovery != 0U) {
        char *assignment = environment_assignment(
            gdox_xenia_discovery_home,
            original_home,
            ""
        );

        if (assignment == NULL) {
            goto allocation_failed;
        }
        output->owned[output->owned_count++] = assignment;
        output->values[output_index++] = assignment;
    }
    output->values[output_index] = NULL;
    return true;

allocation_failed:
    destroy_child_environment(output);
    gdox_error_set(
        error,
        GDOX_ERROR_INTERNAL,
        "could not allocate isolated Xenia environment"
    );
    return false;
}

struct gdox_xenia_process {
    pid_t child;
    pid_t process_group;
    bool reaped;
    int exit_code;
#if defined(__linux__)
    gdox_xenia_bridge *bridge;
#endif
};

static int status_exit_code(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

enum {
    GDOX_XENIA_STOP_POLL_MS = 50U,
    GDOX_XENIA_FORCED_REAP_MS = 5000U,
    GDOX_XENIA_TERMINAL_CLEANUP_MS = 1000U,
    GDOX_XENIA_MONITOR_FAILURE_GRACE_MS = 250U,
};

static bool release_bridge(
    gdox_xenia_process *process,
    gdox_error *error
)
{
#if defined(__linux__)
    if (!gdox_xenia_bridge_close(process->bridge, error)) {
        return false;
    }
    process->bridge = NULL;
    return true;
#else
    (void)process;
    gdox_error_clear(error);
    return true;
#endif
}

static void mark_reaped(gdox_xenia_process *process, int exit_code)
{
    process->child = 0;
    process->reaped = true;
    process->exit_code = exit_code;
}

#if defined(__linux__)
typedef enum gdox_linux_process_state {
    GDOX_LINUX_PROCESS_MISSING = 0,
    GDOX_LINUX_PROCESS_OTHER_GROUP,
    GDOX_LINUX_PROCESS_TERMINAL,
    GDOX_LINUX_PROCESS_ACTIVE,
    GDOX_LINUX_PROCESS_UNKNOWN,
} gdox_linux_process_state;

static bool parse_linux_pid(const char *text, pid_t *output)
{
    char *end;
    long value;

    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return false;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || value <= 0 || *end != '\0'
        || (long)(pid_t)value != value) {
        return false;
    }
    *output = (pid_t)value;
    return true;
}

static bool parse_linux_process_stat(
    const char *text,
    pid_t expected_pid,
    pid_t expected_group,
    char *state,
    bool *group_member
)
{
    const char *name_end;
    const char *cursor;
    char *end;
    long pid_value;
    long parent_value;
    long group_value;

    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return false;
    }
    errno = 0;
    pid_value = strtol(text, &end, 10);
    if (errno != 0 || (long)(pid_t)pid_value != pid_value
        || (pid_t)pid_value != expected_pid || end[0] != ' '
        || end[1] != '(') {
        return false;
    }
    name_end = strrchr(end + 1, ')');
    if (name_end == NULL || name_end[1] != ' '
        || name_end[2] == '\0' || name_end[2] == ' '
        || name_end[3] != ' ') {
        return false;
    }
    *state = name_end[2];
    cursor = name_end + 4;
    errno = 0;
    parent_value = strtol(cursor, &end, 10);
    if (errno != 0 || end == cursor || parent_value < 0
        || (long)(pid_t)parent_value != parent_value || *end != ' ') {
        return false;
    }
    cursor = end + 1;
    errno = 0;
    group_value = strtol(cursor, &end, 10);
    if (errno != 0 || end == cursor || group_value <= 0
        || (long)(pid_t)group_value != group_value
        || (*end != ' ' && *end != '\n' && *end != '\0')) {
        return false;
    }
    *group_member = (pid_t)group_value == expected_group;
    return true;
}

static gdox_linux_process_state query_linux_process_state(
    pid_t pid,
    pid_t process_group
)
{
    char path[64];
    char stat_text[4096];
    char state;
    bool group_member;
    ssize_t bytes;
    int read_error;
    int descriptor;
    int written;

    written = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return GDOX_LINUX_PROCESS_UNKNOWN;
    }
    descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return errno == ENOENT
            ? GDOX_LINUX_PROCESS_MISSING
            : GDOX_LINUX_PROCESS_UNKNOWN;
    }
    do {
        bytes = read(descriptor, stat_text, sizeof(stat_text) - 1U);
    } while (bytes < 0 && errno == EINTR);
    read_error = bytes < 0 ? errno : 0;
    if (close(descriptor) != 0 && bytes >= 0) {
        return GDOX_LINUX_PROCESS_UNKNOWN;
    }
    if (bytes <= 0) {
        return bytes < 0 && read_error == ENOENT
            ? GDOX_LINUX_PROCESS_MISSING
            : GDOX_LINUX_PROCESS_UNKNOWN;
    }
    stat_text[bytes] = '\0';
    if (!parse_linux_process_stat(
            stat_text, pid, process_group, &state, &group_member
        )) {
        return GDOX_LINUX_PROCESS_UNKNOWN;
    }
    if (!group_member) {
        return GDOX_LINUX_PROCESS_OTHER_GROUP;
    }
    return state == 'Z' || state == 'X'
        ? GDOX_LINUX_PROCESS_TERMINAL
        : GDOX_LINUX_PROCESS_ACTIVE;
}

static bool linux_process_group_drained(pid_t process_group)
{
    struct dirent *entry;
    DIR *processes;
    bool reliable = true;
    bool terminal_member = false;
    int directory_error;

    processes = opendir("/proc");
    if (processes == NULL) {
        return false;
    }
    errno = 0;
    while ((entry = readdir(processes)) != NULL) {
        gdox_linux_process_state state;
        pid_t pid;

        if (!parse_linux_pid(entry->d_name, &pid)) {
            continue;
        }
        state = query_linux_process_state(pid, process_group);
        if (state == GDOX_LINUX_PROCESS_ACTIVE) {
            (void)closedir(processes);
            return false;
        }
        if (state == GDOX_LINUX_PROCESS_TERMINAL) {
            terminal_member = true;
        } else if (state == GDOX_LINUX_PROCESS_UNKNOWN) {
            reliable = false;
        }
        errno = 0;
    }
    directory_error = errno;
    if (closedir(processes) != 0 || directory_error != 0) {
        reliable = false;
    }
    return reliable && terminal_member;
}
#endif

static bool query_process_group(
    gdox_xenia_process *process,
    bool *running,
    gdox_error *error
)
{
    if (process->process_group <= 0) {
        *running = false;
        return true;
    }
    if (kill(-process->process_group, 0) == 0 || errno == EPERM) {
#if defined(__linux__)
        if (process->reaped
            && linux_process_group_drained(process->process_group)) {
            process->process_group = 0;
            *running = false;
            return true;
        }
#endif
        *running = true;
        return true;
    }
    if (errno == ESRCH) {
        process->process_group = 0;
        *running = false;
        return true;
    }
    gdox_error_set(
        error, GDOX_ERROR_IO, "could not query Xenia process group"
    );
    return false;
}

static bool query_child(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    bool group_running;
    int status;
    pid_t result;

    if (!process->reaped) {
        if (process->child <= 0) {
            mark_reaped(process, 1);
        } else {
            do {
                result = waitpid(process->child, &status, WNOHANG);
            } while (result < 0 && errno == EINTR);
            if (result == process->child) {
                mark_reaped(process, status_exit_code(status));
            } else if (result < 0 && errno == ECHILD) {
                mark_reaped(process, 1);
            } else if (result < 0) {
                gdox_error_set(
                    error, GDOX_ERROR_IO, "could not query Xenia process"
                );
                return false;
            }
        }
    }
    if (!query_process_group(process, &group_running, error)) {
        return false;
    }
    *running = group_running || !process->reaped;
    *exit_code = *running ? 0 : process->exit_code;
    return true;
}

static void remember_error(gdox_error *saved, const gdox_error *candidate)
{
    if (!gdox_error_is_set(saved) && gdox_error_is_set(candidate)) {
        *saved = *candidate;
    }
}

static void request_signal(
    gdox_xenia_process *process,
    int signal_number,
    const char *message,
    gdox_error *saved_error
);

static bool wait_for_exit(
    gdox_xenia_process *process,
    uint32_t timeout_ms,
    bool repeat_interrupt_after_leader_exit,
    int *exit_code,
    gdox_error *saved_error
)
{
    bool interrupt_repeated = false;
    uint32_t waited = 0U;

    for (;;) {
        gdox_error query_error;
        bool running = true;
        const bool leader_was_reaped = process->reaped;
        int observed_exit = -1;

        gdox_error_clear(&query_error);
        if (query_child(
                process, &running, &observed_exit, &query_error
            )) {
            if (!running) {
                *exit_code = observed_exit;
                return true;
            }
            if (repeat_interrupt_after_leader_exit
                && !interrupt_repeated && !leader_was_reaped
                && process->reaped && process->process_group > 0) {
                request_signal(
                    process,
                    SIGINT,
                    "could not repeat Xenia shutdown request",
                    saved_error
                );
                interrupt_repeated = true;
            }
        } else {
            remember_error(saved_error, &query_error);
        }
        if (waited >= timeout_ms) {
            return false;
        }
        {
            const uint32_t remaining = timeout_ms - waited;
            const uint32_t delay = remaining < GDOX_XENIA_STOP_POLL_MS
                ? remaining
                : GDOX_XENIA_STOP_POLL_MS;
            gdox_sleep_ms(delay);
            waited += delay;
        }
    }
}

static void request_signal(
    gdox_xenia_process *process,
    int signal_number,
    const char *message,
    gdox_error *saved_error
)
{
    gdox_error signal_error;
    int signal_error_number;

    if (process->process_group > 0) {
        if (kill(-process->process_group, signal_number) == 0) {
            return;
        }
        signal_error_number = errno;
        if (signal_error_number != ESRCH) {
            gdox_error_set(&signal_error, GDOX_ERROR_IO, message);
            remember_error(saved_error, &signal_error);
            return;
        }
    }
    if (!process->reaped && process->child > 0
        && kill(process->child, signal_number) != 0 && errno != ESRCH) {
        gdox_error_set(&signal_error, GDOX_ERROR_IO, message);
        remember_error(saved_error, &signal_error);
    }
}

static bool terminate_owned_child(
    gdox_xenia_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error saved_error;

    gdox_error_clear(&saved_error);
    if (wait_for_exit(process, 0U, false, exit_code, &saved_error)) {
        return release_bridge(process, error);
    }
    request_signal(
        process,
        SIGINT,
        "could not request Xenia shutdown",
        &saved_error
    );
    if (wait_for_exit(process, grace_ms, true, exit_code, &saved_error)) {
        return release_bridge(process, error);
    }
    request_signal(
        process, SIGKILL, "could not force Xenia to stop", &saved_error
    );
    if (wait_for_exit(
            process,
            GDOX_XENIA_FORCED_REAP_MS,
            false,
            exit_code,
            &saved_error
        )) {
        return release_bridge(process, error);
    }
    if (gdox_error_is_set(&saved_error)) {
        *error = saved_error;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "Xenia did not exit after forced termination"
        );
    }
    *exit_code = -1;
    return false;
}

static bool force_terminal_cleanup(gdox_xenia_process *process)
{
    gdox_error saved_error;
    int exit_code = -1;

    gdox_error_clear(&saved_error);
    request_signal(
        process,
        SIGKILL,
        "could not force Xenia to stop",
        &saved_error
    );
    if (wait_for_exit(
            process,
            GDOX_XENIA_TERMINAL_CLEANUP_MS,
            false,
            &exit_code,
            &saved_error
        )) {
        return true;
    }
    if (gdox_error_is_set(&saved_error)) {
        (void)fprintf(stderr, "GDOX: %s\n", saved_error.message);
    } else {
        (void)fputs(
            "GDOX: Xenia process group remained active after forced "
            "termination\n",
            stderr
        );
    }
    return false;
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
            "could not configure Xenia process output"
        );
        return false;
    }
    return true;
}

static bool configure_process_group(
    posix_spawnattr_t *attributes,
    gdox_error *error
)
{
    int result = posix_spawnattr_init(attributes);
    const bool initialized = result == 0;

    if (result == 0) {
        result = posix_spawnattr_setflags(
            attributes, (short)POSIX_SPAWN_SETPGROUP
        );
    }
    if (result == 0) {
        result = posix_spawnattr_setpgroup(attributes, 0);
    }
    if (result != 0) {
        if (initialized) {
            (void)posix_spawnattr_destroy(attributes);
        }
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not isolate the Xenia process group"
        );
        return false;
    }
    return true;
}

bool gdox_xenia_launch(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_process **output,
    gdox_error *error
)
{
    gdox_xenia_process *process;
    gdox_xenia_launch_plan plan;
    gdox_xenia_child_environment child_environment;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    char *arguments[GDOX_XENIA_MAX_ARGUMENTS];
    const char *disc_path;
    size_t index;
    int result;

    gdox_error_clear(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (options == NULL || options->runtime == NULL
        || options->runtime->definition == NULL || target == NULL
        || target->location == NULL || target->location[0] == '\0'
        || output == NULL
        || !gdox_xenia_target_preflight(target->kind, error)
        || !gdox_xenia_verify_payload(
            options->runtime->payload,
            options->runtime->definition,
            error
        )) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "verified Xenia launch options and target are required"
            );
        }
        return false;
    }
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Xenia process"
        );
        return false;
    }
    disc_path = target->location;
    if (target->kind == GDOX_XENIA_TARGET_PRIVATE_NBD) {
#if defined(__linux__)
        if (!gdox_xenia_bridge_start(
                options,
                target,
                &process->bridge,
                error
            )) {
            free(process);
            return false;
        }
        disc_path = gdox_xenia_bridge_path(process->bridge);
#else
        free(process);
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "live Xbox 360 playback requires the Linux read-only bridge"
        );
        return false;
#endif
    } else if (target->kind != GDOX_XENIA_TARGET_IMAGE) {
        gdox_xenia_process_destroy(process);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia target kind is invalid"
        );
        return false;
    }
    if (!gdox_xenia_build_launch_plan(options, disc_path, &plan, error)
        || !configure_output(&actions, options->console_output, error)) {
        gdox_xenia_process_destroy(process);
        return false;
    }
    if (!configure_process_group(&attributes, error)) {
        (void)posix_spawn_file_actions_destroy(&actions);
        gdox_xenia_process_destroy(process);
        return false;
    }
    if (!build_child_environment(options, &child_environment, error)) {
        (void)posix_spawnattr_destroy(&attributes);
        (void)posix_spawn_file_actions_destroy(&actions);
        gdox_xenia_process_destroy(process);
        return false;
    }
    for (index = 0U; index <= plan.count; ++index) {
        arguments[index] = (char *)plan.arguments[index];
    }
    result = posix_spawn(
        &process->child,
        plan.arguments[0],
        &actions,
        &attributes,
        arguments,
        child_environment.values
    );
    destroy_child_environment(&child_environment);
    (void)posix_spawnattr_destroy(&attributes);
    (void)posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        gdox_xenia_process_destroy(process);
        gdox_error_set(
            error,
            result == ENOENT ? GDOX_ERROR_NOT_FOUND : GDOX_ERROR_IO,
            "could not launch the verified Xenia runtime"
        );
        return false;
    }
    process->process_group = process->child;
    *output = process;
    return true;
}

bool gdox_xenia_poll(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia process and status outputs are required"
        );
        return false;
    }
    if (!query_child(process, running, exit_code, error)) {
        gdox_error monitor_error = *error;
        gdox_error stop_error;
        int stopped_exit = -1;

        if (!terminate_owned_child(
                process,
                GDOX_XENIA_MONITOR_FAILURE_GRACE_MS,
                &stopped_exit,
                &stop_error
            )) {
            *error = stop_error;
        } else {
            *error = monitor_error;
        }
        *running = process->process_group > 0 || !process->reaped;
        *exit_code = *running ? -1 : process->exit_code;
        return false;
    }
    if (!*running) {
#if defined(__linux__)
        if (process->bridge != NULL) {
            bool closed = false;

            if (!gdox_xenia_bridge_try_close(
                    process->bridge, &closed, error
                )) {
                return false;
            }
            if (closed) {
                process->bridge = NULL;
            } else {
                *running = true;
                *exit_code = 0;
            }
        }
#endif
        return true;
    }
#if defined(__linux__)
    if (process->bridge != NULL) {
        bool alive;
        gdox_error bridge_error;
        gdox_error stop_error;
        int stopped_exit = -1;

        if (gdox_xenia_bridge_alive(
                process->bridge, &alive, &bridge_error
            ) && alive) {
            return true;
        }
        if (!gdox_error_is_set(&bridge_error)) {
            gdox_error_set(
                &bridge_error,
                GDOX_ERROR_IO,
                "private Xbox 360 disc view stopped"
            );
        }
        if (!terminate_owned_child(
                process,
                GDOX_XENIA_MONITOR_FAILURE_GRACE_MS,
                &stopped_exit,
                &stop_error
            )) {
            *error = stop_error;
        } else {
            *error = bridge_error;
        }
        *running = process->process_group > 0 || !process->reaped;
        *exit_code = *running ? -1 : process->exit_code;
        return false;
    }
#endif
    return true;
}

bool gdox_xenia_stop(
    gdox_xenia_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || exit_code == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia process and exit status are required"
        );
        return false;
    }
    return terminate_owned_child(process, grace_ms, exit_code, error);
}

void gdox_xenia_process_destroy(gdox_xenia_process *process)
{
    if (process != NULL) {
        bool safe_to_release_bridge = true;
        int exit_code;
        gdox_error error;

        if (process->process_group > 0
            || (!process->reaped && process->child > 0)) {
            if (!terminate_owned_child(
                    process, 1000U, &exit_code, &error
                )) {
                safe_to_release_bridge = force_terminal_cleanup(process);
            }
        }
        if (safe_to_release_bridge) {
            gdox_error bridge_error;

            if (!release_bridge(process, &bridge_error)) {
                (void)fprintf(stderr, "GDOX: %s\n", bridge_error.message);
            }
#if defined(__linux__)
        } else if (process->bridge != NULL) {
            (void)fputs(
                "GDOX: retaining the Xbox 360 disc bridge while Xenia is "
                "still active\n",
                stderr
            );
#endif
        }
        free(process);
    }
}
