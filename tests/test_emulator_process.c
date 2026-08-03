#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gdox/emulator.h"

#include "core/xemu_capabilities.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_access _access
#define gdox_test_getcwd _getcwd
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_rmdir _rmdir
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define gdox_test_access access
#define gdox_test_getcwd getcwd
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_rmdir rmdir
#endif

static int failures = 0;

#if !defined(_WIN32)
static bool join_test_path(
    char *output,
    size_t capacity,
    const char *directory,
    const char *name
)
{
    const size_t directory_length = strlen(directory);
    const size_t name_length = strlen(name);

    if (directory_length >= capacity
        || name_length >= capacity - directory_length
        || name_length + 1U >= capacity - directory_length) {
        return false;
    }
    memcpy(output, directory, directory_length);
    output[directory_length] = '/';
    memcpy(
        output + directory_length + 1U,
        name,
        name_length + 1U
    );
    return true;
}
#endif

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    const size_t bytes = strlen(text);
    bool success;

    if (file == NULL) {
        return false;
    }
    success = fwrite(text, 1U, bytes, file) == bytes;
    return fclose(file) == 0 && success;
}

static void delay_briefly(void)
{
#if defined(_WIN32)
    Sleep(10U);
#else
    const struct timespec delay = {0, 10L * 1000L * 1000L};

    (void)nanosleep(&delay, NULL);
#endif
}

static bool set_ready_environment(const char *path)
{
#if defined(_WIN32)
    return _putenv_s("GDOX_EMULATOR_TEST_READY", path) == 0;
#else
    return setenv("GDOX_EMULATOR_TEST_READY", path, 1) == 0;
#endif
}

static bool clear_ready_environment(void)
{
#if defined(_WIN32)
    return _putenv_s("GDOX_EMULATOR_TEST_READY", "") == 0;
#else
    return unsetenv("GDOX_EMULATOR_TEST_READY") == 0;
#endif
}

static bool set_test_environment(const char *name, const char *value)
{
#if defined(_WIN32)
    return _putenv_s(name, value) == 0;
#else
    return setenv(name, value, 1) == 0;
#endif
}

static bool clear_test_environment(const char *name)
{
#if defined(_WIN32)
    return _putenv_s(name, "") == 0;
#else
    return unsetenv(name) == 0;
#endif
}

static bool same_path(const char *left, const char *right)
{
#if defined(_WIN32)
    return _stricmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

static bool absolute_path(const char *path)
{
#if defined(_WIN32)
    return path != NULL
        && ((((path[0] >= 'A' && path[0] <= 'Z')
              || (path[0] >= 'a' && path[0] <= 'z'))
             && path[1] == ':'
             && (path[2] == '/' || path[2] == '\\'))
            || (path[0] == '\\' && path[1] == '\\'));
#else
    return path != NULL && path[0] == '/';
#endif
}

static bool set_poison_environment(const char *path)
{
    static const char *const names[] = {
        "HOME",
        "MESA_SHADER_CACHE_DIR",
        "MESA_SHADER_CACHE_DISABLE",
        "TMPDIR",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "__GL_SHADER_DISK_CACHE",
        "__GL_SHADER_DISK_CACHE_PATH",
#if defined(_WIN32)
        "APPDATA",
        "LOCALAPPDATA",
        "TEMP",
        "TMP",
        "USERPROFILE",
#endif
    };
    size_t index;

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (!set_test_environment(names[index], path)) {
            return false;
        }
    }
    return true;
}

static bool clear_poison_environment(void)
{
    static const char *const names[] = {
        "HOME",
        "MESA_SHADER_CACHE_DIR",
        "MESA_SHADER_CACHE_DISABLE",
        "TMPDIR",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "__GL_SHADER_DISK_CACHE",
        "__GL_SHADER_DISK_CACHE_PATH",
#if defined(_WIN32)
        "APPDATA",
        "LOCALAPPDATA",
        "TEMP",
        "TMP",
        "USERPROFILE",
#endif
    };
    size_t index;
    bool success = true;

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        success = clear_test_environment(names[index]) && success;
    }
    return success;
}

static bool read_text(
    const char *path,
    char *output,
    size_t capacity
)
{
    FILE *file = fopen(path, "rb");
    size_t bytes;
    bool complete;

    if (file == NULL || capacity == 0U) {
        return false;
    }
    bytes = fread(output, 1U, capacity - 1U, file);
    complete = bytes != capacity - 1U || feof(file) != 0;
    if (ferror(file) != 0 || fclose(file) != 0) {
        return false;
    }
    output[bytes] = '\0';
    return complete;
}

#if !defined(_WIN32)
static bool set_test_executable_environment(const char *path)
{
    return setenv("GDOX_EMULATOR_TEST_EXECUTABLE", path, 1) == 0;
}

static bool clear_test_executable_environment(void)
{
    return unsetenv("GDOX_EMULATOR_TEST_EXECUTABLE") == 0;
}
#endif

static bool wait_for_ready(const char *path, unsigned long *identifier)
{
    unsigned int attempt;

    for (attempt = 0U; attempt < 200U; ++attempt) {
        FILE *file = fopen(path, "rb");

        if (file != NULL) {
            const bool parsed = fscanf(file, "%lu", identifier) == 1;

            (void)fclose(file);
            if (parsed) {
                return true;
            }
        }
        delay_briefly();
    }
    return false;
}

static bool process_is_terminal(unsigned long identifier)
{
#if defined(_WIN32)
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)identifier);
    bool terminal;

    if (process == NULL) {
        return true;
    }
    terminal = WaitForSingleObject(process, 0U) == WAIT_OBJECT_0;
    (void)CloseHandle(process);
    return terminal;
#else
    errno = 0;
    return kill((pid_t)identifier, 0) != 0 && errno == ESRCH;
#endif
}

static int isolated_child_mode(int argc, char **argv)
{
    static const char *const names[] = {
        "TMPDIR",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
#if defined(_WIN32)
        "APPDATA",
        "LOCALAPPDATA",
        "TEMP",
        "TMP",
        "USERPROFILE",
#endif
    };
    const char *home = getenv("HOME");
    const char *capture = getenv("GDOX_EMULATOR_TEST_SESSION_CAPTURE");
    const char *configuration = NULL;
    const char *save_vault = NULL;
    char current[4096];
    char environment_marker[4096];
    FILE *file;
    size_t index;
    int argument;

    if (home == NULL || capture == NULL
        || gdox_test_getcwd(current, sizeof(current)) == NULL
        || !same_path(current, home)) {
        return 25;
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *value = getenv(names[index]);

        if (value == NULL || !same_path(value, home)) {
            return 26;
        }
    }
    if (getenv("MESA_SHADER_CACHE_DISABLE") == NULL
        || strcmp(getenv("MESA_SHADER_CACHE_DISABLE"), "1") != 0
        || getenv("__GL_SHADER_DISK_CACHE") == NULL
        || strcmp(getenv("__GL_SHADER_DISK_CACHE"), "0") != 0
        || getenv("MESA_SHADER_CACHE_DIR") != NULL
        || getenv("__GL_SHADER_DISK_CACHE_PATH") != NULL) {
        return 26;
    }
#if defined(_WIN32)
    {
        wchar_t *block = GetEnvironmentStringsW();
        const wchar_t *cursor;

        if (block == NULL) {
            return 27;
        }
        for (cursor = block; *cursor != L'\0'; cursor += wcslen(cursor) + 1U) {
            if (cursor[0] == L'=') {
                (void)FreeEnvironmentStringsW(block);
                return 27;
            }
        }
        (void)FreeEnvironmentStringsW(block);
    }
#endif
    for (argument = 1; argument + 1 < argc; ++argument) {
        if (strcmp(argv[argument], "-config_path") == 0) {
            configuration = argv[argument + 1];
        } else if (strcmp(argv[argument], "--gdox-save-vault") == 0) {
            save_vault = argv[argument + 1];
        }
    }
    if (!absolute_path(configuration)
        || !absolute_path(save_vault)
        || same_path(save_vault, home)
        || (file = fopen(configuration, "rb")) == NULL) {
        return 28;
    }
    (void)fclose(file);
    if (!write_text("runtime-output.marker", "session cwd")) {
        return 29;
    }
    if (snprintf(
            environment_marker,
            sizeof(environment_marker),
            "%s/environment-output.marker",
            getenv("XDG_CACHE_HOME")
        ) < 0
        || !write_text(environment_marker, "session environment")) {
        return 30;
    }
    return write_text(capture, home) ? 0 : 31;
}

static int child_mode(int argc, char **argv)
{
    int index;

    if (argc == 2 && strcmp(argv[1], "--gdox-capabilities") == 0) {
        const char *mode = getenv("GDOX_TEST_XEMU_CAPABILITY_MODE");

        (void)puts(
            mode != NULL && strcmp(mode, "save-export") == 0
                ? GDOX_XEMU_CAPABILITIES_TRUE_RESPONSE
                : GDOX_XEMU_CAPABILITIES_FALSE_RESPONSE
        );
        return 0;
    }

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--gdox-emulator-test-isolation") == 0) {
            return isolated_child_mode(argc, argv);
        }
        if (strcmp(argv[index], "--gdox-emulator-test-exit") == 0) {
            return 23;
        }
#if defined(_WIN32)
        if (strcmp(
                argv[index],
                "--gdox-emulator-test-still-active-exit"
            ) == 0) {
            return STILL_ACTIVE;
        }
#endif
        if (strcmp(argv[index], "--gdox-emulator-test-hold") == 0) {
            const char *ready = getenv("GDOX_EMULATOR_TEST_READY");
            const char *capture = getenv(
                "GDOX_EMULATOR_TEST_SESSION_CAPTURE"
            );
            const char *home = getenv("HOME");
            char identifier[64];

#if defined(_WIN32)
            (void)snprintf(
                identifier,
                sizeof(identifier),
                "%lu",
                (unsigned long)GetCurrentProcessId()
            );
#else
            (void)signal(SIGTERM, SIG_IGN);
            (void)snprintf(
                identifier,
                sizeof(identifier),
                "%lu",
                (unsigned long)getpid()
            );
#endif
            if (ready == NULL || capture == NULL || home == NULL
                || !write_text(capture, home)
                || !write_text(ready, identifier)) {
                return 3;
            }
            for (;;) {
#if defined(_WIN32)
                Sleep(1000U);
#else
                pause();
#endif
            }
        }
    }
    return -1;
}

static void test_process_lifecycle(const char *executable)
{
    static const char configuration_text[] = "fixture = true\n";
    gdox_emulator_options options = {
        executable,
        NULL,
        NULL,
        1U,
        GDOX_EMULATOR_ASPECT_AUTOMATIC,
        GDOX_EMULATOR_FIT_SCALE,
        false,
        true,
        1280U,
        720U,
    };
    gdox_emulator_process *process = NULL;
    gdox_error error;
    char working[4096];
    char configuration[4096];
    char save_vault[4096];
    char ready[4096];
    char poison[4096];
    char capture[4096];
    char cwd_marker[4096];
    char poison_marker[4096];
    char session_root[4096];
#if !defined(_WIN32)
    char poison_session_parent[4096] = "";
    char poison_session_parent_name[64];
    char wrapper[128] = "";
    bool executable_environment_set = false;
#endif
    bool running = true;
    bool environment_set = false;
    bool save_vault_created = false;
    bool poison_created = false;
    bool poison_set = false;
    bool capture_set = false;
    bool capability_set = false;
    bool save_export = true;
    int exit_code = -1;
    unsigned int attempt;
    unsigned long identifier = 0UL;
    int written;

#define PROCESS_CHECK(expression)                                               \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(                                                      \
                stderr,                                                        \
                "%s:%d: check failed: %s\n",                                  \
                __FILE__,                                                      \
                __LINE__,                                                      \
                #expression                                                    \
            );                                                                 \
            ++failures;                                                        \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(gdox_test_getcwd(working, sizeof(working)) != NULL);
    written = snprintf(
        save_vault,
        sizeof(save_vault),
        "%s/gdox emulator saves %d",
        working,
        gdox_test_getpid()
    );
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(save_vault));
    (void)gdox_test_rmdir(save_vault);
    PROCESS_CHECK(gdox_test_mkdir(save_vault) == 0);
    save_vault_created = true;
    options.save_vault = save_vault;
    written = snprintf(
        configuration,
        sizeof(configuration),
        "%s/gdox-emulator-process-%d.toml",
        working,
        gdox_test_getpid()
    );
    PROCESS_CHECK(
        written >= 0 && (size_t)written < sizeof(configuration)
    );
    written = snprintf(
        ready,
        sizeof(ready),
        "%s/gdox-emulator-process-%d.ready",
        working,
        gdox_test_getpid()
    );
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(ready));
    written = snprintf(
        poison,
        sizeof(poison),
        "%s/gdox-emulator-poison-%d",
        working,
        gdox_test_getpid()
    );
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(poison));
#if !defined(_WIN32)
    written = snprintf(
        poison_session_parent_name,
        sizeof(poison_session_parent_name),
        "gdox-session-%lu",
        (unsigned long)geteuid()
    );
    PROCESS_CHECK(
        written >= 0
        && (size_t)written < sizeof(poison_session_parent_name)
        && join_test_path(
            poison_session_parent,
            sizeof(poison_session_parent),
            poison,
            poison_session_parent_name
        )
    );
#endif
    written = snprintf(
        capture,
        sizeof(capture),
        "%s/gdox-emulator-session-%d.txt",
        working,
        gdox_test_getpid()
    );
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(capture));
    written = snprintf(
        cwd_marker, sizeof(cwd_marker), "%s/runtime-output.marker", working
    );
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(cwd_marker));
    written = snprintf(
        poison_marker,
        sizeof(poison_marker),
        "%s/environment-output.marker",
        poison
    );
    PROCESS_CHECK(
        written >= 0 && (size_t)written < sizeof(poison_marker)
    );
    (void)remove(configuration);
    (void)remove(ready);
    (void)remove(capture);
    (void)remove(cwd_marker);
    (void)remove(poison_marker);
    (void)gdox_test_rmdir(poison);
    PROCESS_CHECK(write_text(configuration, configuration_text));
    options.configuration = configuration;
    PROCESS_CHECK(set_test_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export"
    ));
    capability_set = true;

    PROCESS_CHECK(gdox_emulator_launch(
        &options,
        "--gdox-emulator-test-exit",
        &process,
        &error
    ));
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_emulator_poll(
            process, &running, &exit_code, &error
        ));
        if (running) {
            delay_briefly();
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 23);
    running = true;
    exit_code = -1;
    PROCESS_CHECK(gdox_emulator_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 23);
    gdox_emulator_process_destroy(process);
    process = NULL;

    PROCESS_CHECK(gdox_test_mkdir(poison) == 0);
    poison_created = true;
    PROCESS_CHECK(set_poison_environment(poison));
    poison_set = true;
    PROCESS_CHECK(set_test_environment(
        "GDOX_EMULATOR_TEST_SESSION_CAPTURE", capture
    ));
    capture_set = true;
    running = true;
    exit_code = -1;
    PROCESS_CHECK(gdox_emulator_launch(
        &options,
        "--gdox-emulator-test-isolation",
        &process,
        &error
    ));
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_emulator_poll(
            process, &running, &exit_code, &error
        ));
        if (running) {
            delay_briefly();
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 0);
    gdox_emulator_process_destroy(process);
    process = NULL;
    PROCESS_CHECK(read_text(capture, session_root, sizeof(session_root)));
    PROCESS_CHECK(absolute_path(session_root));
    PROCESS_CHECK(gdox_test_access(session_root, 0) != 0);
    PROCESS_CHECK(gdox_test_access(cwd_marker, 0) != 0);
    PROCESS_CHECK(gdox_test_access(poison_marker, 0) != 0);
    PROCESS_CHECK(clear_test_environment(
        "GDOX_EMULATOR_TEST_SESSION_CAPTURE"
    ));
    capture_set = false;
    PROCESS_CHECK(clear_poison_environment());
    poison_set = false;
    PROCESS_CHECK(remove(capture) == 0);
#if !defined(_WIN32)
    if (gdox_test_rmdir(poison_session_parent) != 0) {
        PROCESS_CHECK(errno == ENOENT);
    }
#endif
    PROCESS_CHECK(gdox_test_rmdir(poison) == 0);
    poison_created = false;

#if defined(_WIN32)
    running = true;
    exit_code = -1;
    PROCESS_CHECK(gdox_emulator_launch(
        &options,
        "--gdox-emulator-test-still-active-exit",
        &process,
        &error
    ));
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_emulator_poll(
            process, &running, &exit_code, &error
        ));
        if (running) {
            delay_briefly();
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == STILL_ACTIVE);
    gdox_emulator_process_destroy(process);
    process = NULL;
#endif

#if !defined(_WIN32)
    written = snprintf(
        wrapper,
        sizeof(wrapper),
        "gdox-emulator-process-%d.sh",
        gdox_test_getpid()
    );
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(wrapper));
    (void)remove(wrapper);
    PROCESS_CHECK(write_text(
        wrapper,
        "#!/bin/sh\n"
        "exec \"${GDOX_EMULATOR_TEST_EXECUTABLE}\" \"$@\"\n"
    ));
    PROCESS_CHECK(chmod(wrapper, S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    PROCESS_CHECK(set_test_executable_environment(executable));
    executable_environment_set = true;
    options.executable = wrapper;
    running = true;
    exit_code = -1;
    PROCESS_CHECK(gdox_emulator_launch(
        &options,
        "--gdox-emulator-test-exit",
        &process,
        &error
    ));
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_emulator_poll(
            process, &running, &exit_code, &error
        ));
        if (running) {
            delay_briefly();
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 23);
    gdox_emulator_process_destroy(process);
    process = NULL;
    options.executable = executable;
#endif

    PROCESS_CHECK(set_ready_environment(ready));
    environment_set = true;
    PROCESS_CHECK(set_test_environment(
        "GDOX_EMULATOR_TEST_SESSION_CAPTURE", capture
    ));
    capture_set = true;
    PROCESS_CHECK(gdox_emulator_launch(
        &options,
        "--gdox-emulator-test-hold",
        &process,
        &error
    ));
    PROCESS_CHECK(wait_for_ready(ready, &identifier));
    PROCESS_CHECK(read_text(capture, session_root, sizeof(session_root)));
    PROCESS_CHECK(gdox_test_access(session_root, 0) == 0);
    PROCESS_CHECK(clear_test_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE"
    ));
    capability_set = false;
    PROCESS_CHECK(gdox_emulator_query_storage_capabilities(
        options.executable, &save_export, &error
    ));
    PROCESS_CHECK(!save_export);
    PROCESS_CHECK(set_test_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export"
    ));
    capability_set = true;
    PROCESS_CHECK(gdox_test_access(session_root, 0) == 0);
    PROCESS_CHECK(gdox_emulator_stop(process, 0U, &exit_code, &error));
    PROCESS_CHECK(!gdox_error_is_set(&error));
#if defined(_WIN32)
    PROCESS_CHECK(exit_code == 1);
#else
    PROCESS_CHECK(exit_code == 137);
#endif
    PROCESS_CHECK(gdox_emulator_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    gdox_emulator_process_destroy(process);
    process = NULL;
    PROCESS_CHECK(process_is_terminal(identifier));
    PROCESS_CHECK(read_text(capture, session_root, sizeof(session_root)));
    PROCESS_CHECK(gdox_test_access(session_root, 0) != 0);
    PROCESS_CHECK(remove(capture) == 0);
    PROCESS_CHECK(clear_test_environment(
        "GDOX_EMULATOR_TEST_SESSION_CAPTURE"
    ));
    capture_set = false;

    (void)remove(ready);
    identifier = 0UL;
    PROCESS_CHECK(set_test_environment(
        "GDOX_EMULATOR_TEST_SESSION_CAPTURE", capture
    ));
    capture_set = true;
    PROCESS_CHECK(gdox_emulator_launch(
        &options,
        "--gdox-emulator-test-hold",
        &process,
        &error
    ));
    PROCESS_CHECK(wait_for_ready(ready, &identifier));
    gdox_emulator_process_destroy(process);
    process = NULL;
    PROCESS_CHECK(process_is_terminal(identifier));
    PROCESS_CHECK(read_text(capture, session_root, sizeof(session_root)));
    PROCESS_CHECK(gdox_test_access(session_root, 0) != 0);
    PROCESS_CHECK(remove(capture) == 0);
    PROCESS_CHECK(clear_test_environment(
        "GDOX_EMULATOR_TEST_SESSION_CAPTURE"
    ));
    capture_set = false;

cleanup:
    gdox_emulator_process_destroy(process);
    if (capability_set && !clear_test_environment(
            "GDOX_TEST_XEMU_CAPABILITY_MODE"
        )) {
        ++failures;
    }
    if (capture_set && !clear_test_environment(
            "GDOX_EMULATOR_TEST_SESSION_CAPTURE"
        )) {
        ++failures;
    }
    if (poison_set && !clear_poison_environment()) {
        ++failures;
    }
    (void)remove(capture);
    (void)remove(cwd_marker);
    (void)remove(poison_marker);
#if !defined(_WIN32)
    if (poison_session_parent[0] != '\0') {
        (void)gdox_test_rmdir(poison_session_parent);
    }
#endif
    if (poison_created) {
        (void)gdox_test_rmdir(poison);
    }
#if !defined(_WIN32)
    if (executable_environment_set
        && !clear_test_executable_environment()) {
        (void)fputs("could not clear executable test environment\n", stderr);
        ++failures;
    }
    if (wrapper[0] != '\0') {
        (void)remove(wrapper);
    }
#endif
    if (environment_set && !clear_ready_environment()) {
        (void)fputs("could not clear emulator test environment\n", stderr);
        ++failures;
    }
    (void)remove(ready);
    (void)remove(configuration);
    if (save_vault_created && gdox_test_rmdir(save_vault) != 0) {
        ++failures;
    }
#undef PROCESS_CHECK
}

int main(int argc, char **argv)
{
    const int child_result = child_mode(argc, argv);

    if (child_result >= 0) {
        return child_result;
    }
    test_process_lifecycle(argv[0]);
    return failures == 0 ? 0 : 1;
}
