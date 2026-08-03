#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "core/xenia_launch.h"
#include "gdox/hash.h"
#include "gdox/xenia.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_remove _unlink
#define gdox_test_rmdir _rmdir
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_remove unlink
#define gdox_test_rmdir rmdir
#endif

#if defined(__linux__)
#include "platform/xenia_bridge_linux.h"
#include "platform/xenia_bridge_tools_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#endif

int gdox_test_failures = 0;

typedef struct xenia_fixture {
    char root[128];
    char payload[192];
#if defined(__linux__)
    char launcher[192];
    char capture[192];
    char nbdfuse[192];
    char fusermount[192];
    char bridge_stop[192];
    char process_ready[192];
    char nested_ready[192];
    char nested_armed[192];
    char nested_stopped[192];
    char interrupt_signal[192];
#endif
    gdox_xenia_runtime runtime;
} xenia_fixture;

static const char fixture_payload[] = "verified xenia fixture\n";
static const char fixture_sha256[] =
    "c16167a926e5ca108c43a77a419d2ac427ac7aa3063746c4fe8b4b1131a0f065";
#if defined(__linux__)
typedef struct injection_environment_value {
    const char *name;
    const char *value;
} injection_environment_value;

#define GDOX_TEST_STRINGIFY_(value) #value
#define GDOX_TEST_STRINGIFY(value) GDOX_TEST_STRINGIFY_(value)

static const char *const removed_injection_environment[] = {
#define GDOX_XENIA_INJECTION_UNSET(name) #name,
#define GDOX_XENIA_INJECTION_SET(name, value)
#include "platform/xenia_injection_environment.def"
#undef GDOX_XENIA_INJECTION_SET
#undef GDOX_XENIA_INJECTION_UNSET
};

static const injection_environment_value fixed_injection_environment[] = {
#define GDOX_XENIA_INJECTION_UNSET(name)
#define GDOX_XENIA_INJECTION_SET(name, value) \
    {#name, GDOX_TEST_STRINGIFY(value)},
#include "platform/xenia_injection_environment.def"
#undef GDOX_XENIA_INJECTION_SET
#undef GDOX_XENIA_INJECTION_UNSET
};

static const char injection_environment_names[] =
#define GDOX_XENIA_INJECTION_UNSET(name) #name ":"
#define GDOX_XENIA_INJECTION_SET(name, value) #name ":"
#include "platform/xenia_injection_environment.def"
#undef GDOX_XENIA_INJECTION_SET
#undef GDOX_XENIA_INJECTION_UNSET
    "";

#undef GDOX_TEST_STRINGIFY
#undef GDOX_TEST_STRINGIFY_

static const char fixture_launcher[] =
    "#!/bin/sh\n"
    "if [ \"$1\" = \"--gdox-preflight\" ]; then exit 0; fi\n"
    "if [ -n \"${GDOX_XENIA_TEST_CAPTURE:-}\" ]; then\n"
    "  : > \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  for argument in \"$@\"; do\n"
    "    printf '%s\\n' \"$argument\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  done\n"
    "  printf 'ENV:MESA_SHADER_CACHE_DISABLE=%s\\n' "
    "\"${MESA_SHADER_CACHE_DISABLE:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:__GL_SHADER_DISK_CACHE=%s\\n' "
    "\"${__GL_SHADER_DISK_CACHE:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:VKD3D_SHADER_CACHE_PATH=%s\\n' "
    "\"${VKD3D_SHADER_CACHE_PATH:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:DXVK_SHADER_CACHE=%s\\n' "
    "\"${DXVK_SHADER_CACHE:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:DXVK_STATE_CACHE=%s\\n' "
    "\"${DXVK_STATE_CACHE:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:DXVK_LOG_PATH=%s\\n' "
    "\"${DXVK_LOG_PATH:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:XDG_CACHE_HOME=%s\\n' "
    "\"${XDG_CACHE_HOME:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:HOME=%s\\n' "
    "\"${HOME:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:XDG_CONFIG_HOME=%s\\n' "
    "\"${XDG_CONFIG_HOME:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:XDG_DATA_HOME=%s\\n' "
    "\"${XDG_DATA_HOME:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:XDG_STATE_HOME=%s\\n' "
    "\"${XDG_STATE_HOME:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:XDG_RUNTIME_DIR=%s\\n' "
    "\"${XDG_RUNTIME_DIR:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:TMPDIR=%s\\n' "
    "\"${TMPDIR:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:STEAM_COMPAT_DATA_PATH=%s\\n' "
    "\"${STEAM_COMPAT_DATA_PATH:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:STEAM_COMPAT_INSTALL_PATH=%s\\n' "
    "\"${STEAM_COMPAT_INSTALL_PATH:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:STEAM_COMPAT_MEDIA_PATH=%s\\n' "
    "\"${STEAM_COMPAT_MEDIA_PATH:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:STEAM_COMPAT_TRANSCODED_MEDIA_PATH=%s\\n' "
    "\"${STEAM_COMPAT_TRANSCODED_MEDIA_PATH:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:PROTON_LOG=%s\\n' "
    "\"${PROTON_LOG:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:PROTON_LOG_DIR=%s\\n' "
    "\"${PROTON_LOG_DIR:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:PROTON_DUMP_DEBUG_COMMANDS=%s\\n' "
    "\"${PROTON_DUMP_DEBUG_COMMANDS:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:PROTON_DEBUG_DIR=%s\\n' "
    "\"${PROTON_DEBUG_DIR:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:PROTON_CRASH_REPORT_DIR=%s\\n' "
    "\"${PROTON_CRASH_REPORT_DIR:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  printf 'ENV:WINEPREFIX=%s\\n' "
    "\"${WINEPREFIX:-}\" >> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "  original_ifs=$IFS\n"
    "  IFS=:\n"
    "  for name in $GDOX_XENIA_TEST_POLICY_NAMES; do\n"
    "    if value=$(printenv \"$name\"); then\n"
    "      printf 'ENV:%s=%s\\n' \"$name\" \"$value\" "
    ">> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "    else\n"
    "      printf 'ENV:%s=absent\\n' \"$name\" "
    ">> \"$GDOX_XENIA_TEST_CAPTURE\"\n"
    "    fi\n"
    "  done\n"
    "  IFS=$original_ifs\n"
    "fi\n"
    "if [ \"${GDOX_XENIA_TEST_HOLD:-0}\" = \"1\" ]; then\n"
    "  if [ -n \"${GDOX_XENIA_TEST_INTERRUPT_SIGNAL:-}\" ]; then\n"
    "    trap 'printf stopped > \"$GDOX_XENIA_TEST_INTERRUPT_SIGNAL\"; "
    "if [ \"${GDOX_XENIA_TEST_NESTED_SECOND_INT:-0}\" = \"1\" ]; then "
    "while [ ! -e \"$GDOX_XENIA_TEST_NESTED_ARMED\" ]; do :; done; fi; "
    "exit 0' INT\n"
    "  fi\n"
    "  if [ \"${GDOX_XENIA_TEST_IGNORE_INT:-0}\" = \"1\" ]; then\n"
    "    trap '' INT\n"
    "  fi\n"
    "  if [ -n \"${GDOX_XENIA_TEST_NESTED_READY:-}\" ]; then\n"
    "    if [ \"${GDOX_XENIA_TEST_NESTED_SECOND_INT:-0}\" = \"1\" ]; then\n"
    "      \"$GDOX_XENIA_TEST_HELPER\" --gdox-xenia-test-second-int &\n"
    "    else\n"
    "      (trap '' INT; while :; do :; done) &\n"
    "      printf '%s\\n' \"$!\" > \"$GDOX_XENIA_TEST_NESTED_READY\"\n"
    "    fi\n"
    "  fi\n"
    "  if [ -n \"${GDOX_XENIA_TEST_READY:-}\" ]; then\n"
    "    : > \"$GDOX_XENIA_TEST_READY\"\n"
    "  fi\n"
    "  while :; do :; done\n"
    "fi\n"
    "exit 23\n";
static const char fixture_nbdfuse[] =
    "#!/bin/sh\n"
    "if [ \"${1:-}\" = \"--version\" ]; then exit 0; fi\n"
    "truncate -s \"$GDOX_XENIA_TEST_BRIDGE_LENGTH\" \"$5\" || exit 1\n"
    "while [ ! -e \"$GDOX_XENIA_TEST_BRIDGE_STOP\" ]; do\n"
    "  sleep 0.01\n"
    "done\n"
    "exit 42\n";
static const char fixture_fusermount[] =
    "#!/bin/sh\n"
    "if [ \"${1:-}\" = \"--version\" ]; then exit 0; fi\n"
    "if [ \"${GDOX_XENIA_TEST_UNMOUNT_FAIL:-0}\" = \"1\" ]; then exit 1; fi\n"
    "exit 0\n";
#endif

static bool write_file(const char *path, const char *data, size_t bytes)
{
    FILE *file = fopen(path, "wb");
    bool success;

    if (file == NULL) {
        return false;
    }
    success = fwrite(data, 1U, bytes, file) == bytes;
    return fclose(file) == 0 && success;
}

#if defined(__linux__)
static bool write_exclusive_executable(
    const char *path,
    const char *data,
    size_t bytes
)
{
    size_t offset = 0U;
    const int file = open(
        path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700
    );
    bool success = file >= 0;

    while (success && offset < bytes) {
        const ssize_t written = write(file, data + offset, bytes - offset);

        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            success = false;
        }
    }
    if (success) {
        success = fchmod(file, 0700) == 0;
    }
    return file >= 0 && close(file) == 0 && success;
}
#endif

static bool fixture_create(xenia_fixture *fixture)
{
    int written;

    memset(fixture, 0, sizeof(*fixture));
    written = snprintf(
        fixture->root,
        sizeof(fixture->root),
        "gdox-xenia-runtime-%d",
        gdox_test_getpid()
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->root)
        || gdox_test_mkdir(fixture->root) != 0) {
        return false;
    }
#if defined(__linux__)
    {
        char absolute[sizeof(fixture->root)];
        char working[sizeof(fixture->root)];

        if (getcwd(working, sizeof(working)) == NULL) {
            return false;
        }
        written = snprintf(
            absolute,
            sizeof(absolute),
            "%s/%s",
            working,
            fixture->root
        );
        if (written < 0 || (size_t)written >= sizeof(absolute)) {
            return false;
        }
        memcpy(fixture->root, absolute, strlen(absolute) + 1U);
    }
#endif
    written = snprintf(
        fixture->payload,
        sizeof(fixture->payload),
        "%s/xenia_canary.exe",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->payload)
        || !write_file(
            fixture->payload,
            fixture_payload,
            sizeof(fixture_payload) - 1U
        )) {
        return false;
    }
    fixture->runtime = (gdox_xenia_runtime){
        "fixture000",
        "xenia_canary.exe",
        fixture_sha256,
        sizeof(fixture_payload) - 1U,
        GDOX_XENIA_GPU_D3D12,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
    };
#if defined(__linux__)
    written = snprintf(
        fixture->launcher,
        sizeof(fixture->launcher),
        "%s/xenia",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->launcher)) {
        return false;
    }
    written = snprintf(
        fixture->capture,
        sizeof(fixture->capture),
        "%s/arguments.txt",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->capture)
        || !write_file(
            fixture->launcher,
            fixture_launcher,
            sizeof(fixture_launcher) - 1U
        ) || chmod(fixture->launcher, 0700) != 0) {
        return false;
    }
    written = snprintf(
        fixture->nbdfuse,
        sizeof(fixture->nbdfuse),
        "%s/nbdfuse",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->nbdfuse)
        || !write_file(
            fixture->nbdfuse,
            fixture_nbdfuse,
            sizeof(fixture_nbdfuse) - 1U
        ) || chmod(fixture->nbdfuse, 0700) != 0) {
        return false;
    }
    written = snprintf(
        fixture->fusermount,
        sizeof(fixture->fusermount),
        "%s/fusermount3",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->fusermount)
        || !write_file(
            fixture->fusermount,
            fixture_fusermount,
            sizeof(fixture_fusermount) - 1U
        ) || chmod(fixture->fusermount, 0700) != 0) {
        return false;
    }
    written = snprintf(
        fixture->bridge_stop,
        sizeof(fixture->bridge_stop),
        "%s/stop-bridge",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->bridge_stop)) {
        return false;
    }
    written = snprintf(
        fixture->process_ready,
        sizeof(fixture->process_ready),
        "%s/process-ready",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->process_ready)) {
        return false;
    }
    written = snprintf(
        fixture->nested_ready,
        sizeof(fixture->nested_ready),
        "%s/nested-ready",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->nested_ready)) {
        return false;
    }
    written = snprintf(
        fixture->nested_armed,
        sizeof(fixture->nested_armed),
        "%s/nested-armed",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->nested_armed)) {
        return false;
    }
    written = snprintf(
        fixture->nested_stopped,
        sizeof(fixture->nested_stopped),
        "%s/nested-stopped",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->nested_stopped)) {
        return false;
    }
    written = snprintf(
        fixture->interrupt_signal,
        sizeof(fixture->interrupt_signal),
        "%s/interrupt-signal",
        fixture->root
    );
    if (written < 0 || (size_t)written >= sizeof(fixture->interrupt_signal)) {
        return false;
    }
#endif
    return true;
}

static void fixture_destroy(const xenia_fixture *fixture)
{
#if defined(__linux__)
    (void)gdox_test_remove(fixture->interrupt_signal);
    (void)gdox_test_remove(fixture->nested_stopped);
    (void)gdox_test_remove(fixture->nested_armed);
    (void)gdox_test_remove(fixture->nested_ready);
    (void)gdox_test_remove(fixture->process_ready);
    (void)gdox_test_remove(fixture->bridge_stop);
    (void)gdox_test_remove(fixture->capture);
    (void)gdox_test_remove(fixture->fusermount);
    (void)gdox_test_remove(fixture->nbdfuse);
    (void)gdox_test_remove(fixture->launcher);
#endif
    (void)gdox_test_remove(fixture->payload);
    (void)gdox_test_rmdir(fixture->root);
}

static void test_private_uri_validation(void)
{
    static const char valid[] =
        "nbd://127.0.0.1:65535/0123456789abcdef0123456789abcdef";
    static const char *invalid[] = {
        NULL,
        "nbd://localhost:1/0123456789abcdef0123456789abcdef",
        "nbd://127.0.0.1:0/0123456789abcdef0123456789abcdef",
        "nbd://127.0.0.1:01/0123456789abcdef0123456789abcdef",
        "nbd://127.0.0.1:65536/0123456789abcdef0123456789abcdef",
        "nbd://127.0.0.1:1/0123456789abcdef0123456789abcde",
        "nbd://127.0.0.1:1/0123456789abcdef0123456789abcdef0",
        "nbd://127.0.0.1:1/0123456789abcdef0123456789abcdeF",
        "nbd://127.0.0.1:1/0123456789abcdef0123456789abcdef/path",
    };
    gdox_error error;
    size_t index;

    GDOX_TEST_CHECK(gdox_xenia_validate_disc_uri(valid, &error));
    GDOX_TEST_CHECK(!gdox_error_is_set(&error));
    GDOX_TEST_CHECK(gdox_xenia_validate_disc_uri(
        "nbd://127.0.0.1:1/00000000000000000000000000000000",
        &error
    ));
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        GDOX_TEST_CHECK(!gdox_xenia_validate_disc_uri(invalid[index], &error));
        GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    }
}

static void test_target_capabilities(const xenia_fixture *fixture)
{
    gdox_error error;

#if defined(_WIN32)
    GDOX_TEST_CHECK(gdox_xenia_target_supported(
        GDOX_XENIA_TARGET_IMAGE
    ));
    GDOX_TEST_CHECK(gdox_xenia_target_supported(
        GDOX_XENIA_TARGET_PRIVATE_NBD
    ));
    GDOX_TEST_CHECK(gdox_xenia_runtime_target_supported(
        &fixture->runtime, GDOX_XENIA_TARGET_PRIVATE_NBD
    ));
    GDOX_TEST_CHECK(gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_IMAGE, &error
    ));
    GDOX_TEST_CHECK(gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_PRIVATE_NBD, &error
    ));
#elif defined(__linux__)
    const char *current_path = getenv("PATH");
    char *saved_path = current_path != NULL ? strdup(current_path) : NULL;

    GDOX_TEST_CHECK(gdox_xenia_target_supported(
        GDOX_XENIA_TARGET_IMAGE
    ));
    GDOX_TEST_CHECK(gdox_xenia_target_supported(
        GDOX_XENIA_TARGET_PRIVATE_NBD
    ));
    GDOX_TEST_CHECK(gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_IMAGE, &error
    ));
    if (current_path != NULL && saved_path == NULL) {
        GDOX_TEST_CHECK(false);
        return;
    }
    GDOX_TEST_CHECK(setenv("PATH", "/gdox-missing-tools", 1) == 0);
    GDOX_TEST_CHECK(!gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_PRIVATE_NBD, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    GDOX_TEST_CHECK(strstr(error.message, "nbdfuse") != NULL);

    GDOX_TEST_CHECK(setenv("PATH", fixture->root, 1) == 0);
    GDOX_TEST_CHECK(chmod(fixture->fusermount, 0600) == 0);
    GDOX_TEST_CHECK(!gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_PRIVATE_NBD, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    GDOX_TEST_CHECK(strstr(error.message, "fusermount3") != NULL);
    GDOX_TEST_CHECK(chmod(fixture->fusermount, 0700) == 0);
    GDOX_TEST_CHECK(gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_PRIVATE_NBD, &error
    ));
    GDOX_TEST_CHECK(!gdox_error_is_set(&error));
    if (saved_path != NULL) {
        GDOX_TEST_CHECK(setenv("PATH", saved_path, 1) == 0);
    } else {
        GDOX_TEST_CHECK(unsetenv("PATH") == 0);
    }
    free(saved_path);
#else
    gdox_xenia_runtime_descriptor descriptor;

    GDOX_TEST_CHECK(!gdox_xenia_target_supported(
        GDOX_XENIA_TARGET_IMAGE
    ));
    GDOX_TEST_CHECK(!gdox_xenia_target_supported(
        GDOX_XENIA_TARGET_PRIVATE_NBD
    ));
    GDOX_TEST_CHECK(!gdox_xenia_target_preflight(
        GDOX_XENIA_TARGET_IMAGE, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(!gdox_xenia_resolve_runtime(
        &fixture->runtime, NULL, &descriptor, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(descriptor.definition == NULL);
#endif
    GDOX_TEST_CHECK(!gdox_xenia_target_preflight(
        (gdox_xenia_target_kind)99, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
}

#if defined(__linux__)
static void test_bundled_bridge_precedes_path(const xenia_fixture *fixture)
{
    static const char bundled_script[] =
        "#!/bin/sh\n"
        "if [ \"${1:-}\" = \"--version\" ]; then exit 0; fi\n"
        "exit 91\n";
    gdox_xenia_bridge_tools tools;
    gdox_error error;
    const char *current_path = getenv("PATH");
    char *saved_path = current_path != NULL ? strdup(current_path) : NULL;
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char bundled[GDOX_EMULATOR_PATH_CAPACITY];
    char *slash;
    ssize_t bytes;

    bytes = readlink("/proc/self/exe", executable, sizeof(executable) - 1U);
    GDOX_TEST_CHECK(bytes > 0 && (size_t)bytes < sizeof(executable));
    if (bytes <= 0 || (size_t)bytes >= sizeof(executable)) {
        free(saved_path);
        return;
    }
    executable[bytes] = '\0';
    slash = strrchr(executable, '/');
    GDOX_TEST_CHECK(slash != NULL);
    if (slash == NULL) {
        free(saved_path);
        return;
    }
    *slash = '\0';
    GDOX_TEST_CHECK(snprintf(
        bundled, sizeof(bundled), "%s/nbdfuse", executable
    ) > 0);
    GDOX_TEST_CHECK(write_exclusive_executable(
        bundled, bundled_script, sizeof(bundled_script) - 1U
    ));
    GDOX_TEST_CHECK(setenv("PATH", fixture->root, 1) == 0);
    GDOX_TEST_CHECK(gdox_xenia_bridge_tools_resolve(&tools, &error));
    GDOX_TEST_CHECK(strcmp(tools.mount, bundled) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(bundled) == 0);
    if (saved_path != NULL) {
        GDOX_TEST_CHECK(setenv("PATH", saved_path, 1) == 0);
    } else {
        GDOX_TEST_CHECK(unsetenv("PATH") == 0);
    }
    free(saved_path);
}

static void test_bridge_cleanup_failure_is_retriable(
    const xenia_fixture *fixture
)
{
    static const char private_uri[] =
        "nbd://127.0.0.1:65535/0123456789abcdef0123456789abcdef";
    gdox_xenia_options options = {0};
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_PRIVATE_NBD,
        private_uri,
        4096U,
    };
    gdox_xenia_bridge *bridge = NULL;
    gdox_error error;
    const char *current_path = getenv("PATH");
    char *saved_path = current_path != NULL ? strdup(current_path) : NULL;
    char stale[GDOX_EMULATOR_PATH_CAPACITY];
    char helper_path[GDOX_EMULATOR_PATH_CAPACITY];
    bool closed = false;
    bool close_result = true;
    unsigned int attempt;
    int written;

    options.storage_root = fixture->root;
    options.console_output = true;
    (void)gdox_test_remove(fixture->bridge_stop);
    written = snprintf(
        helper_path,
        sizeof(helper_path),
        "%s:/usr/bin:/bin",
        fixture->root
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(helper_path)
    );
    GDOX_TEST_CHECK(setenv("PATH", helper_path, 1) == 0);
    GDOX_TEST_CHECK(setenv(
        "GDOX_XENIA_TEST_BRIDGE_LENGTH", "4096", 1
    ) == 0);
    GDOX_TEST_CHECK(setenv(
        "GDOX_XENIA_TEST_BRIDGE_STOP", fixture->bridge_stop, 1
    ) == 0);
    GDOX_TEST_CHECK(gdox_xenia_bridge_start(
        &options, &target, &bridge, &error
    ));
    for (attempt = 0U; attempt < 120U && close_result; ++attempt) {
        close_result = gdox_xenia_bridge_try_close(
            bridge, &closed, &error
        );
        GDOX_TEST_CHECK(!closed);
    }
    GDOX_TEST_CHECK(!close_result);
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    written = bridge != NULL
        ? snprintf(
            stale,
            sizeof(stale),
            "%s.stale",
            gdox_xenia_bridge_path(bridge)
        )
        : -1;
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(stale));
    GDOX_TEST_CHECK(write_file(stale, "stale\n", 6U));
    GDOX_TEST_CHECK(write_file(fixture->bridge_stop, "stop\n", 5U));
    GDOX_TEST_CHECK(setenv("GDOX_XENIA_TEST_UNMOUNT_FAIL", "1", 1) == 0);
    GDOX_TEST_CHECK(!gdox_xenia_bridge_close(bridge, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_IO);
    GDOX_TEST_CHECK(gdox_test_remove(stale) == 0);
    GDOX_TEST_CHECK(unsetenv("GDOX_XENIA_TEST_UNMOUNT_FAIL") == 0);
    GDOX_TEST_CHECK(gdox_xenia_bridge_close(bridge, &error));
    bridge = NULL;
    GDOX_TEST_CHECK(unsetenv("GDOX_XENIA_TEST_BRIDGE_STOP") == 0);
    GDOX_TEST_CHECK(unsetenv("GDOX_XENIA_TEST_BRIDGE_LENGTH") == 0);
    GDOX_TEST_CHECK(gdox_test_remove(fixture->bridge_stop) == 0);
    if (saved_path != NULL) {
        GDOX_TEST_CHECK(setenv("PATH", saved_path, 1) == 0);
    } else {
        GDOX_TEST_CHECK(unsetenv("PATH") == 0);
    }
    free(saved_path);
    gdox_xenia_bridge_destroy(bridge);
}
#endif

static void test_payload_verification(const xenia_fixture *fixture)
{
    gdox_xenia_runtime runtime;
    gdox_error error;

    GDOX_TEST_CHECK(gdox_xenia_verify_payload(
        fixture->payload,
        &fixture->runtime,
        &error
    ));
    GDOX_TEST_CHECK(!gdox_error_is_set(&error));

    runtime = fixture->runtime;
    ++runtime.payload_size;
    GDOX_TEST_CHECK(!gdox_xenia_verify_payload(
        fixture->payload,
        &runtime,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);

    runtime = fixture->runtime;
    runtime.payload_sha256 =
        "016167a926e5ca108c43a77a419d2ac427ac7aa3063746c4fe8b4b1131a0f065";
    GDOX_TEST_CHECK(!gdox_xenia_verify_payload(
        fixture->payload,
        &runtime,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);

    runtime = fixture->runtime;
    runtime.payload_sha256 =
        "C16167A926E5CA108C43A77A419D2AC427AC7AA3063746C4FE8B4B1131A0F065";
    GDOX_TEST_CHECK(!gdox_xenia_verify_payload(
        fixture->payload,
        &runtime,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
}

static bool plan_contains(
    const gdox_xenia_launch_plan *plan,
    const char *argument
)
{
    size_t index;

    for (index = 0U; index < plan->count; ++index) {
        if (strcmp(plan->arguments[index], argument) == 0) {
            return true;
        }
    }
    return false;
}

static size_t plan_argument_count(
    const gdox_xenia_launch_plan *plan,
    const char *argument
)
{
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < plan->count; ++index) {
        if (strcmp(plan->arguments[index], argument) == 0) {
            ++count;
        }
    }
    return count;
}

static gdox_xenia_launch_policy fixture_policy(
    const gdox_xenia_runtime *runtime
)
{
    return (gdox_xenia_launch_policy){
        .runtime = runtime,
        .launch_module = "scimitar_final.xex",
        .patch_set = GDOX_XENIA_PATCH_SET_NONE,
        .settings = {
            .allow_tearing = false,
            .vsync = true,
            .max_queued_frames = 64U,
            .allow_invalid_fetch_constants = true,
            .occlusion_query = GDOX_XENIA_OCCLUSION_STRICT,
            .occlusion_query_saturation_basis_points = 10000U,
            .readback_resolve = GDOX_XENIA_READBACK_FULL,
            .protect_zero = true,
            .new_xma_decoder = false,
            .use_dedicated_xma_thread = true,
            .async_shader_compilation = true,
            .use_handheld_custom_resolution = true,
        },
    };
}

static gdox_xenia_options fixture_options(
    const gdox_xenia_runtime_descriptor *descriptor,
    const gdox_xenia_launch_policy *policy
)
{
#if defined(_WIN32)
    static const char storage[] = "C:\\fixture-storage";
    static const char content[] = "C:\\fixture-content";
    static const char cache[] = "C:\\fixture-cache";
    static const char log[] = "C:\\fixture-xenia.log";
#else
    static const char storage[] = "/fixture-storage";
    static const char content[] = "/fixture-content";
    static const char cache[] = "/fixture-cache";
    static const char log[] = "/fixture-xenia.log";
#endif
    return (gdox_xenia_options){
        .runtime = descriptor,
        .policy = policy,
        .performance_profile = GDOX_XENIA_PERFORMANCE_DESKTOP,
        .storage_root = storage,
        .content_root = content,
        .cache_root = cache,
        .log_file = log,
        .console_output = false,
        .fullscreen = true,
    };
}

static void test_launch_plan(const xenia_fixture *fixture)
{
    gdox_xenia_runtime_descriptor descriptor = {0};
    gdox_xenia_launch_policy policy = fixture_policy(&fixture->runtime);
    gdox_xenia_runtime unsupported_runtime = fixture->runtime;
    gdox_xenia_launch_policy unsupported_policy;
    gdox_xenia_options options;
    gdox_xenia_launch_plan plan;
    gdox_xenia_target private_target = {
        .kind = GDOX_XENIA_TARGET_PRIVATE_NBD,
        .location = "nbd://127.0.0.1:49152/0123456789abcdef0123456789abcdef",
        .length = UINT64_C(7340032000),
    };
    gdox_error error;

    descriptor.definition = &fixture->runtime;
    (void)snprintf(
        descriptor.launcher,
        sizeof(descriptor.launcher),
        "fixture-launcher"
    );
    options = fixture_options(&descriptor, &policy);
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan.count == 37U);
    GDOX_TEST_CHECK(plan.arguments[plan.count] == NULL);
    GDOX_TEST_CHECK(strcmp(plan.arguments[0], "fixture-launcher") == 0);
    GDOX_TEST_CHECK(plan_contains(
        &plan,
        "--launch_module=scimitar_final.xex"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--gpu=d3d12"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--render_target_path_d3d12=rtv"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--d3d12_queue_priority=1"));
    GDOX_TEST_CHECK(plan_contains(
        &plan,
        "--d3d12_allow_variable_refresh_rate_and_tearing=false"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--occlusion_query=strict"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--occlusion_query_saturation=1.0"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--readback_resolve=full"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--vsync=true"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--apu_max_queued_frames=64"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--async_shader_compilation=true"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--apply_patches=true"));
    GDOX_TEST_CHECK(plan_argument_count(
        &plan, "--gdox_disclaimer_acknowledged=true"
    ) == 1U);
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--gdox_persistent_content_saves_only=true"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--store_shaders=false"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--disable_instruction_infocache=true"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--host_present_from_non_ui_thread=true"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--ignore_thread_affinities=true"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--framerate_limit=60"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--d3d12_pipeline_creation_threads=-1"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--custom_internal_display_resolution_x=0"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--custom_internal_display_resolution_y=0"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--internal_display_resolution=2"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan,
        "--gpu_allow_invalid_fetch_constants=true"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--fullscreen=true"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--mount_cache=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--mount_scratch=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--protect_zero=true"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--xma_decoder=old"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--use_dedicated_xma_thread=true"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--discord=false"));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--log_level=0"));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--flush_log=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, plan.log));
    GDOX_TEST_CHECK(plan_contains(&plan, "--log_to_stdout=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, plan.storage));
    GDOX_TEST_CHECK(plan_contains(&plan, plan.content));
    GDOX_TEST_CHECK(plan_contains(&plan, plan.cache));
    GDOX_TEST_CHECK(strcmp(plan.arguments[plan.count - 1U], "fixture-disc.iso") == 0);

    GDOX_TEST_CHECK(gdox_xenia_build_target_launch_plan(
        &options, &private_target, &plan, &error
    ));
    GDOX_TEST_CHECK(plan.count == 38U);
    GDOX_TEST_CHECK(plan_argument_count(
        &plan, "--gdox_disclaimer_acknowledged=true"
    ) == 1U);
    GDOX_TEST_CHECK(plan_contains(
        &plan,
        "--gdox_disc=nbd://127.0.0.1:49152/0123456789abcdef0123456789abcdef"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--gdox_disc_length=7340032000"
    ));
    GDOX_TEST_CHECK(!plan_contains(&plan, private_target.location));
    private_target.length = 0U;
    GDOX_TEST_CHECK(!gdox_xenia_build_target_launch_plan(
        &options, &private_target, &plan, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    descriptor.definition = &fixture->runtime;
    options = fixture_options(&descriptor, &policy);
    unsupported_runtime = fixture->runtime;
    unsupported_runtime.supports_storage_isolation = false;
    unsupported_policy = fixture_policy(&unsupported_runtime);
    descriptor.definition = &unsupported_runtime;
    options = fixture_options(&descriptor, &unsupported_policy);
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);

    unsupported_runtime = fixture->runtime;
    unsupported_runtime.supports_managed_disclaimer_acknowledgement = false;
    unsupported_policy = fixture_policy(&unsupported_runtime);
    descriptor.definition = &unsupported_runtime;
    options = fixture_options(&descriptor, &unsupported_policy);
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    private_target.length = UINT64_C(7340032000);

    descriptor.definition = gdox_xenia_default_policy()->runtime;
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    descriptor.definition = &fixture->runtime;
    options = fixture_options(&descriptor, &policy);
    options.performance_profile = GDOX_XENIA_PERFORMANCE_HANDHELD;
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--custom_internal_display_resolution_x=720"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--custom_internal_display_resolution_y=480"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--log_level=0"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--flush_log=false"));

    options.performance_profile = GDOX_XENIA_PERFORMANCE_DESKTOP;
    policy.settings.vsync = false;
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--vsync=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--framerate_limit=0"));
    policy.settings.vsync = true;

    policy.settings.occlusion_query = GDOX_XENIA_OCCLUSION_DEFAULT;
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--occlusion_query=fast"));
    policy.settings.occlusion_query = GDOX_XENIA_OCCLUSION_STRICT;

    policy.settings.readback_resolve = GDOX_XENIA_READBACK_FAST;
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--readback_resolve=fast"));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--readback_resolve=full"));
    policy.settings.readback_resolve = GDOX_XENIA_READBACK_FULL;

    options.performance_profile = (gdox_xenia_performance_profile)99;
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    options.performance_profile = GDOX_XENIA_PERFORMANCE_DESKTOP;
    unsupported_runtime = fixture->runtime;
    unsupported_runtime.supports_custom_internal_display_resolution = false;
    unsupported_policy = fixture_policy(&unsupported_runtime);
    descriptor.definition = &unsupported_runtime;
    options = fixture_options(&descriptor, &unsupported_policy);
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    descriptor.definition = &fixture->runtime;
    options = fixture_options(&descriptor, &policy);
    unsupported_runtime = fixture->runtime;
    unsupported_runtime.supports_host_performance_profile = false;
    unsupported_policy = fixture_policy(&unsupported_runtime);
    descriptor.definition = &unsupported_runtime;
    options = fixture_options(&descriptor, &unsupported_policy);
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    descriptor.definition = &fixture->runtime;
    options = fixture_options(&descriptor, &policy);
    policy.settings.max_queued_frames = 3U;
    GDOX_TEST_CHECK(!gdox_xenia_build_launch_plan(
        &options,
        "fixture-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
}

static void test_crackdown_launch_plan(const xenia_fixture *fixture)
{
    const gdox_xenia_title_identity identity = {
        UINT32_C(0x4d5307dc),
        UINT32_C(0x596f9615),
        1U,
        1U,
    };
    const gdox_xenia_launch_policy *selected =
        gdox_xenia_select_policy(&identity);
    gdox_xenia_launch_policy policy = *selected;
    gdox_xenia_runtime_descriptor descriptor = {0};
    gdox_xenia_options options;
    gdox_xenia_launch_plan plan;
    gdox_error error;

    GDOX_TEST_CHECK(selected != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(selected->runtime->revision, "72ce13097") == 0);
    policy.runtime = &fixture->runtime;
    descriptor.definition = &fixture->runtime;
    (void)snprintf(
        descriptor.launcher,
        sizeof(descriptor.launcher),
        "fixture-launcher"
    );
    options = fixture_options(&descriptor, &policy);
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "physical-xgd2-disc.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan.count == 37U);
    GDOX_TEST_CHECK(plan.arguments[plan.count] == NULL);
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--launch_module=default.xex"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--occlusion_query=fast-alt"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--occlusion_query_saturation=1.0"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--readback_resolve=full"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--mount_cache=false"));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--mount_cache=true"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--protect_zero=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--xma_decoder=new"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--use_dedicated_xma_thread=false"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--gpu_allow_invalid_fetch_constants=true"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--gpu_allow_invalid_fetch_constants=false"
    ));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--occlusion_query=strict"));
    GDOX_TEST_CHECK(strcmp(
        plan.arguments[plan.count - 1U], "physical-xgd2-disc.iso"
    ) == 0);
}

static void test_halo_4_launch_plan(const xenia_fixture *fixture)
{
    const gdox_xenia_title_identity identity = {
        UINT32_C(0x4d530919),
        UINT32_C(0x1c9d20bc),
        1U,
        1U,
    };
    const gdox_xenia_launch_policy *selected =
        gdox_xenia_select_policy(&identity);
    gdox_xenia_launch_policy policy = *selected;
    gdox_xenia_runtime_descriptor descriptor = {0};
    gdox_xenia_options options;
    gdox_xenia_launch_plan plan;
    gdox_error error;

    GDOX_TEST_CHECK(selected != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(selected->runtime->revision, "7d8be7f17") == 0);
    policy.runtime = &fixture->runtime;
    descriptor.definition = &fixture->runtime;
    (void)snprintf(
        descriptor.launcher,
        sizeof(descriptor.launcher),
        "fixture-launcher"
    );
    options = fixture_options(&descriptor, &policy);
    options.performance_profile = GDOX_XENIA_PERFORMANCE_HANDHELD;
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "physical-halo-4-xgd3.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan.arguments[plan.count] == NULL);
    GDOX_TEST_CHECK(plan_contains(&plan, "--gpu=d3d12"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--render_target_path_d3d12=rtv"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--occlusion_query=fast"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--readback_resolve=fast"));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--readback_resolve=none"));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--readback_resolve=full"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--vsync=true"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--async_shader_compilation=true"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--gpu_allow_invalid_fetch_constants=false"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--log_level=0"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--flush_log=false"));
    GDOX_TEST_CHECK(strcmp(
        plan.arguments[plan.count - 1U], "physical-halo-4-xgd3.iso"
    ) == 0);
}

static void test_mass_effect_launch_plan(const xenia_fixture *fixture)
{
    const gdox_xenia_title_identity identity = {
        UINT32_C(0x4d5307e8),
        UINT32_C(0x572ba75d),
        1U,
        1U,
    };
    const gdox_xenia_launch_policy *selected =
        gdox_xenia_select_policy(&identity);
    gdox_xenia_launch_policy policy = *selected;
    gdox_xenia_runtime_descriptor descriptor = {0};
    gdox_xenia_options options;
    gdox_xenia_launch_plan plan;
    gdox_error error;

    GDOX_TEST_CHECK(selected != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(selected->runtime->revision, "72ce13097") == 0);
    GDOX_TEST_CHECK(
        selected->patch_set
            == GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1
    );
    policy.runtime = &fixture->runtime;
    descriptor.definition = &fixture->runtime;
    (void)snprintf(
        descriptor.launcher,
        sizeof(descriptor.launcher),
        "fixture-launcher"
    );
    options = fixture_options(&descriptor, &policy);
    options.performance_profile = GDOX_XENIA_PERFORMANCE_HANDHELD;
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options,
        "physical-mass-effect-xgd2.iso",
        &plan,
        &error
    ));
    GDOX_TEST_CHECK(plan.count == 39U);
    GDOX_TEST_CHECK(plan.arguments[plan.count] == NULL);
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--launch_module=default.xex"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--occlusion_query=strict"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--occlusion_query_saturation=0.75"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--async_shader_compilation=false"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--async_shader_compilation=true"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--custom_internal_display_resolution_x=0"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--custom_internal_display_resolution_y=0"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--custom_internal_display_resolution_x=720"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--custom_internal_display_resolution_y=480"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--apply_patches=true"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--gdox_persistent_content_saves_only=true"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--store_shaders=false"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--log_level=0"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--flush_log=false"));
    GDOX_TEST_CHECK(strcmp(
        plan.arguments[plan.count - 1U],
        "physical-mass-effect-xgd2.iso"
    ) == 0);
}

static void test_native_vulkan_launch_plan(const xenia_fixture *fixture)
{
    const gdox_xenia_title_identity identity = {
        UINT32_C(0x555308ae),
        UINT32_C(0x6d9f552e),
        1U,
        2U,
    };
    const gdox_xenia_launch_policy *selected =
        gdox_xenia_select_policy(&identity);
    gdox_xenia_runtime runtime = fixture->runtime;
    gdox_xenia_runtime_descriptor descriptor = {0};
    gdox_xenia_launch_policy policy;
    gdox_xenia_options options;
    gdox_xenia_launch_plan plan;
    gdox_error error;

    runtime.gpu = GDOX_XENIA_GPU_VULKAN;
    runtime.requires_proton = false;
    runtime.supports_max_queued_frames = false;
    GDOX_TEST_CHECK(selected != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(selected->runtime->revision, "7d8be7f17") == 0);
    policy = *selected;
    policy.runtime = &runtime;
    descriptor.definition = &runtime;
    (void)snprintf(
        descriptor.launcher,
        sizeof(descriptor.launcher),
        "fixture-native-launcher"
    );
    options = fixture_options(&descriptor, &policy);
    GDOX_TEST_CHECK(gdox_xenia_build_launch_plan(
        &options, "physical-xgd3-disc.iso", &plan, &error
    ));
    GDOX_TEST_CHECK(plan.count == 36U);
    GDOX_TEST_CHECK(plan_contains(&plan, "--gpu=vulkan"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--vulkan_allow_present_mode_immediate=false"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--render_target_path_vulkan=fbo"
    ));
    GDOX_TEST_CHECK(!plan_contains(&plan, "--gpu=d3d12"));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--render_target_path_d3d12=rtv"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--apu_max_queued_frames=64"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--d3d12_allow_variable_refresh_rate_and_tearing=false"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--vsync=true"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--vulkan_allow_present_mode_fifo_relaxed=false"
    ));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--vulkan_pipeline_creation_threads=-1"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--d3d12_pipeline_creation_threads=-1"
    ));
    GDOX_TEST_CHECK(plan_contains(&plan, "--occlusion_query=strict"));
    GDOX_TEST_CHECK(plan_contains(&plan, "--readback_resolve=none"));
    GDOX_TEST_CHECK(plan_contains(
        &plan, "--gpu_allow_invalid_fetch_constants=true"
    ));
    GDOX_TEST_CHECK(!plan_contains(
        &plan, "--gpu_allow_invalid_fetch_constants=false"
    ));
}

#if defined(__linux__)
static char *read_text(const char *path, char *output, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    size_t bytes;
    bool success;

    if (file == NULL || capacity == 0U) {
        return NULL;
    }
    bytes = fread(output, 1U, capacity - 1U, file);
    success = ferror(file) == 0;
    if (fclose(file) != 0 || !success) {
        return NULL;
    }
    output[bytes] = '\0';
    return output;
}

static void wait_briefly(void)
{
    const struct timespec delay = {0, 10L * 1000L * 1000L};

    (void)nanosleep(&delay, NULL);
}

typedef struct environment_snapshot {
    const char *name;
    char *value;
    bool present;
} environment_snapshot;

static const char *const isolated_environment_names[] = {
    "HOME",
    "XDG_CACHE_HOME",
    "XDG_CONFIG_HOME",
    "XDG_DATA_HOME",
    "XDG_STATE_HOME",
    "XDG_RUNTIME_DIR",
    "TMPDIR",
    "STEAM_COMPAT_DATA_PATH",
    "STEAM_COMPAT_INSTALL_PATH",
    "STEAM_COMPAT_MEDIA_PATH",
    "STEAM_COMPAT_TRANSCODED_MEDIA_PATH",
    "PROTON_LOG",
    "PROTON_LOG_DIR",
    "PROTON_DUMP_DEBUG_COMMANDS",
    "PROTON_DEBUG_DIR",
    "PROTON_CRASH_REPORT_DIR",
    "WINEPREFIX",
    "MESA_SHADER_CACHE_DISABLE",
    "__GL_SHADER_DISK_CACHE",
    "VKD3D_SHADER_CACHE_PATH",
    "DXVK_SHADER_CACHE",
    "DXVK_STATE_CACHE",
    "DXVK_LOG_PATH",
#define GDOX_XENIA_INJECTION_UNSET(name) #name,
#define GDOX_XENIA_INJECTION_SET(name, value) #name,
#include "platform/xenia_injection_environment.def"
#undef GDOX_XENIA_INJECTION_SET
#undef GDOX_XENIA_INJECTION_UNSET
};

static bool poison_isolated_environment(
    environment_snapshot *snapshots
)
{
    size_t index;

    for (index = 0U;
         index < sizeof(isolated_environment_names)
            / sizeof(isolated_environment_names[0]);
         ++index) {
        const char *value;

        snapshots[index].name = isolated_environment_names[index];
        value = getenv(snapshots[index].name);
        snapshots[index].present = value != NULL;
        if (value != NULL) {
            snapshots[index].value = strdup(value);
            if (snapshots[index].value == NULL) {
                return false;
            }
        }
        if (setenv(
                snapshots[index].name,
                "/gdox-poison-must-not-be-used",
                1
            ) != 0) {
            return false;
        }
    }
    return true;
}

static bool restore_isolated_environment(
    environment_snapshot *snapshots
)
{
    size_t index;
    bool success = true;

    for (index = 0U;
         index < sizeof(isolated_environment_names)
            / sizeof(isolated_environment_names[0]);
         ++index) {
        if (snapshots[index].name != NULL) {
            const int result = snapshots[index].present
                ? setenv(
                    snapshots[index].name,
                    snapshots[index].value,
                    1
                )
                : unsetenv(snapshots[index].name);

            if (result != 0) {
                success = false;
            }
        }
        free(snapshots[index].value);
        snapshots[index].value = NULL;
    }
    return success;
}

static bool wait_for_path(const char *path)
{
    unsigned int attempt;

    for (attempt = 0U; attempt < 200U; ++attempt) {
        if (access(path, F_OK) == 0) {
            return true;
        }
        wait_briefly();
    }
    return false;
}

static bool wait_for_recorded_process_exit(const char *path)
{
    char identifier[64];
    char *end = NULL;
    char process_status_path[64];
    long value;
    unsigned int attempt;
    int written;

    if (read_text(path, identifier, sizeof(identifier)) == NULL) {
        return false;
    }
    errno = 0;
    value = strtol(identifier, &end, 10);
    if (errno != 0 || value <= 0 || end == identifier
        || (*end != '\0' && *end != '\n')) {
        return false;
    }
    written = snprintf(
        process_status_path,
        sizeof(process_status_path),
        "/proc/%ld/stat",
        value
    );
    if (written < 0 || (size_t)written >= sizeof(process_status_path)) {
        return false;
    }
    for (attempt = 0U; attempt < 200U; ++attempt) {
        char status[256];
        char *name_end;

        errno = 0;
        if (kill((pid_t)value, 0) != 0 && errno == ESRCH) {
            return true;
        }
        if (read_text(process_status_path, status, sizeof(status)) != NULL) {
            name_end = strrchr(status, ')');
            if (name_end != NULL && name_end[1] == ' '
                && (name_end[2] == 'Z' || name_end[2] == 'X')) {
                return true;
            }
        }
        wait_briefly();
    }
    return false;
}

static volatile sig_atomic_t gdox_test_interrupt_count = 0;

static void record_linux_test_interrupt(int signal_number)
{
    (void)signal_number;
    if (gdox_test_interrupt_count < 2) {
        ++gdox_test_interrupt_count;
    }
}

static int linux_child_mode(int argc, char **argv)
{
    struct sigaction action;
    sigset_t blocked;
    sigset_t previous;
    sigset_t waiting;
    const char *armed;
    const char *ready;
    const char *stopped;
    char identifier[64];
    int written;
    int index;
    bool selected = false;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--gdox-xenia-test-second-int") == 0) {
            selected = true;
        }
    }
    if (!selected) {
        return -1;
    }
    ready = getenv("GDOX_XENIA_TEST_NESTED_READY");
    armed = getenv("GDOX_XENIA_TEST_NESTED_ARMED");
    stopped = getenv("GDOX_XENIA_TEST_NESTED_STOPPED");
    if (ready == NULL || armed == NULL || stopped == NULL) {
        return 2;
    }
    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGINT) != 0
        || sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        return 2;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = record_linux_test_interrupt;
    if (sigemptyset(&action.sa_mask) != 0
        || sigaction(SIGINT, &action, NULL) != 0) {
        return 2;
    }
    gdox_test_interrupt_count = 0;
    (void)write_file(
        "/proc/self/comm", "xenia ) child\n", sizeof("xenia ) child\n") - 1U
    );
    written = snprintf(
        identifier, sizeof(identifier), "%ld\n", (long)getpid()
    );
    if (written < 0 || (size_t)written >= sizeof(identifier)
        || !write_file(ready, identifier, (size_t)written)) {
        return 2;
    }
    waiting = previous;
    if (sigdelset(&waiting, SIGINT) != 0) {
        return 2;
    }
    while (gdox_test_interrupt_count < 1) {
        (void)sigsuspend(&waiting);
    }
    if (!write_file(armed, "armed\n", 6U)) {
        return 2;
    }
    while (gdox_test_interrupt_count < 2) {
        (void)sigsuspend(&waiting);
    }
    if (!write_file(stopped, "stopped\n", 8U)) {
        return 2;
    }
    return 0;
}

static void test_linux_override_process(const xenia_fixture *fixture)
{
    gdox_xenia_runtime_descriptor descriptor;
    gdox_xenia_launch_policy policy = fixture_policy(&fixture->runtime);
    gdox_xenia_options options;
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_IMAGE,
        "fixture-disc.iso",
        0U,
    };
    gdox_xenia_process *process = NULL;
    gdox_error error;
    char captured[8192];
    environment_snapshot snapshots[
        sizeof(isolated_environment_names)
            / sizeof(isolated_environment_names[0])
    ] = {{0}};
    bool running = true;
    bool capture_environment = false;
    bool policy_names_set = false;
    bool environment_poisoned = false;
    int exit_code = -1;
    unsigned int attempt;
    size_t index;
    char expected_environment[256];

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
            ++gdox_test_failures;                                              \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_CAPTURE",
        fixture->capture,
        1
    ) == 0);
    capture_environment = true;
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_POLICY_NAMES",
        injection_environment_names,
        1
    ) == 0);
    policy_names_set = true;
    environment_poisoned = true;
    PROCESS_CHECK(poison_isolated_environment(snapshots));
    PROCESS_CHECK(gdox_xenia_resolve_runtime(
        &fixture->runtime,
        fixture->launcher,
        &descriptor,
        &error
    ));
    PROCESS_CHECK(descriptor.definition == &fixture->runtime);
    PROCESS_CHECK(descriptor.origin == GDOX_XENIA_RUNTIME_OVERRIDE);
    PROCESS_CHECK(strcmp(descriptor.launcher, fixture->launcher) == 0);
    PROCESS_CHECK(strcmp(descriptor.payload, fixture->payload) == 0);

    options = fixture_options(&descriptor, &policy);
    options.console_output = true;
    PROCESS_CHECK(gdox_xenia_launch(
        &options,
        &target,
        &process,
        &error
    ));
    PROCESS_CHECK(process != NULL);
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_xenia_poll(
            process,
            &running,
            &exit_code,
            &error
        ));
        if (running) {
            wait_briefly();
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 23);
    PROCESS_CHECK(gdox_xenia_poll(
        process,
        &running,
        &exit_code,
        &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 23);
    gdox_xenia_process_destroy(process);
    process = NULL;

    PROCESS_CHECK(read_text(
        fixture->capture,
        captured,
        sizeof(captured)
    ) != NULL);
    PROCESS_CHECK(strstr(captured, "--gpu=d3d12\n") != NULL);
    PROCESS_CHECK(strstr(captured, "--discord=false\n") != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:MESA_SHADER_CACHE_DISABLE=1\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:__GL_SHADER_DISK_CACHE=0\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:VKD3D_SHADER_CACHE_PATH=0\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:DXVK_SHADER_CACHE=0\n"
    ) != NULL);
    PROCESS_CHECK(strstr(captured, "ENV:DXVK_LOG_PATH=none\n") != NULL);
    PROCESS_CHECK(strstr(captured, "ENV:DXVK_STATE_CACHE=0\n") != NULL);
    PROCESS_CHECK(strstr(captured, "ENV:HOME=/fixture-cache/home\n") != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:XDG_CACHE_HOME=/fixture-cache/xdg-cache\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:XDG_CONFIG_HOME=/fixture-cache/xdg-config\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:XDG_DATA_HOME=/fixture-cache/xdg-data\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:XDG_STATE_HOME=/fixture-cache/xdg-state\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:XDG_RUNTIME_DIR=/fixture-cache/xdg-runtime\n"
    ) != NULL);
    PROCESS_CHECK(strstr(captured, "ENV:TMPDIR=/fixture-cache/tmp\n") != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:STEAM_COMPAT_DATA_PATH=/fixture-cache/proton\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:STEAM_COMPAT_INSTALL_PATH=/fixture-cache/install\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured,
        "ENV:STEAM_COMPAT_MEDIA_PATH=/fixture-cache/proton-media\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured,
        "ENV:STEAM_COMPAT_TRANSCODED_MEDIA_PATH="
        "/fixture-cache/proton-transcoded\n"
    ) != NULL);
    PROCESS_CHECK(strstr(captured, "ENV:PROTON_LOG=0\n") != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:PROTON_LOG_DIR=/fixture-cache/proton-logs\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:PROTON_DUMP_DEBUG_COMMANDS=0\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:PROTON_DEBUG_DIR=/fixture-cache/proton-debug\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured,
        "ENV:PROTON_CRASH_REPORT_DIR=/fixture-cache/proton-crash\n"
    ) != NULL);
    PROCESS_CHECK(strstr(
        captured, "ENV:WINEPREFIX=/fixture-cache/proton/pfx\n"
    ) != NULL);
    for (index = 0U;
         index < sizeof(removed_injection_environment)
            / sizeof(removed_injection_environment[0]);
         ++index) {
        PROCESS_CHECK(snprintf(
            expected_environment,
            sizeof(expected_environment),
            "ENV:%s=absent\n",
            removed_injection_environment[index]
        ) > 0);
        PROCESS_CHECK(strstr(captured, expected_environment) != NULL);
    }
    for (index = 0U;
         index < sizeof(fixed_injection_environment)
            / sizeof(fixed_injection_environment[0]);
         ++index) {
        PROCESS_CHECK(snprintf(
            expected_environment,
            sizeof(expected_environment),
            "ENV:%s=%s\n",
            fixed_injection_environment[index].name,
            fixed_injection_environment[index].value
        ) > 0);
        PROCESS_CHECK(strstr(captured, expected_environment) != NULL);
    }
    PROCESS_CHECK(strstr(captured, "/gdox-poison-must-not-be-used") == NULL);
    PROCESS_CHECK(strstr(
        captured,
        "--launch_module=scimitar_final.xex\n"
    ) != NULL);
    PROCESS_CHECK(strstr(captured, "fixture-disc.iso\n") != NULL);

cleanup:
    gdox_xenia_process_destroy(process);
    if (environment_poisoned
        && !restore_isolated_environment(snapshots)) {
        (void)fputs("could not restore Xenia isolation test environment\n", stderr);
        ++gdox_test_failures;
    }
    if (capture_environment
        && unsetenv("GDOX_XENIA_TEST_CAPTURE") != 0) {
        (void)fputs("could not clear Xenia test environment\n", stderr);
        ++gdox_test_failures;
    }
    if (policy_names_set
        && unsetenv("GDOX_XENIA_TEST_POLICY_NAMES") != 0) {
        (void)fputs("could not clear Xenia policy test environment\n", stderr);
        ++gdox_test_failures;
    }
#undef PROCESS_CHECK
}

static void test_linux_graceful_stop(const xenia_fixture *fixture)
{
    gdox_xenia_runtime_descriptor descriptor;
    gdox_xenia_launch_policy policy = fixture_policy(&fixture->runtime);
    gdox_xenia_options options;
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_IMAGE,
        "fixture-disc.iso",
        0U,
    };
    gdox_xenia_process *process = NULL;
    gdox_error error;
    bool environment_set = false;
    bool running = true;
    int exit_code = -1;

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
            ++gdox_test_failures;                                              \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(setenv("GDOX_XENIA_TEST_HOLD", "1", 1) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_READY", fixture->process_ready, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_INTERRUPT_SIGNAL", fixture->interrupt_signal, 1
    ) == 0);
    environment_set = true;
    PROCESS_CHECK(gdox_xenia_resolve_runtime(
        &fixture->runtime,
        fixture->launcher,
        &descriptor,
        &error
    ));
    options = fixture_options(&descriptor, &policy);
    options.console_output = true;
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    PROCESS_CHECK(wait_for_path(fixture->process_ready));
    PROCESS_CHECK(gdox_xenia_stop(process, 1000U, &exit_code, &error));
    PROCESS_CHECK(!gdox_error_is_set(&error));
    PROCESS_CHECK(exit_code == 0);
    PROCESS_CHECK(wait_for_path(fixture->interrupt_signal));
    PROCESS_CHECK(gdox_xenia_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 0);

cleanup:
    gdox_xenia_process_destroy(process);
    (void)gdox_test_remove(fixture->interrupt_signal);
    (void)gdox_test_remove(fixture->process_ready);
    if (environment_set) {
        if (unsetenv("GDOX_XENIA_TEST_INTERRUPT_SIGNAL") != 0
            || unsetenv("GDOX_XENIA_TEST_READY") != 0
            || unsetenv("GDOX_XENIA_TEST_HOLD") != 0) {
            (void)fputs("could not clear Xenia test environment\n", stderr);
            ++gdox_test_failures;
        }
    }
#undef PROCESS_CHECK
}

static void test_linux_reaped_leader_group_stop(
    const xenia_fixture *fixture,
    const char *test_executable
)
{
    gdox_xenia_runtime_descriptor descriptor;
    gdox_xenia_launch_policy policy = fixture_policy(&fixture->runtime);
    gdox_xenia_options options;
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_IMAGE,
        "fixture-disc.iso",
        0U,
    };
    gdox_xenia_process *process = NULL;
    gdox_error error;
    bool environment_set = false;
    bool running = true;
    int exit_code = -1;

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
            ++gdox_test_failures;                                              \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(setenv("GDOX_XENIA_TEST_HOLD", "1", 1) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_READY", fixture->process_ready, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_INTERRUPT_SIGNAL", fixture->interrupt_signal, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_NESTED_READY", fixture->nested_ready, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_NESTED_ARMED", fixture->nested_armed, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_NESTED_STOPPED", fixture->nested_stopped, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_HELPER", test_executable, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_NESTED_SECOND_INT", "1", 1
    ) == 0);
    environment_set = true;
    PROCESS_CHECK(gdox_xenia_resolve_runtime(
        &fixture->runtime,
        fixture->launcher,
        &descriptor,
        &error
    ));
    options = fixture_options(&descriptor, &policy);
    options.console_output = true;
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    PROCESS_CHECK(wait_for_path(fixture->process_ready));
    PROCESS_CHECK(wait_for_path(fixture->nested_ready));
    PROCESS_CHECK(gdox_xenia_stop(process, 1000U, &exit_code, &error));
    PROCESS_CHECK(!gdox_error_is_set(&error));
    PROCESS_CHECK(exit_code == 0);
    PROCESS_CHECK(wait_for_path(fixture->interrupt_signal));
    PROCESS_CHECK(wait_for_path(fixture->nested_armed));
    PROCESS_CHECK(wait_for_path(fixture->nested_stopped));
    PROCESS_CHECK(wait_for_recorded_process_exit(fixture->nested_ready));
    PROCESS_CHECK(gdox_xenia_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 0);

cleanup:
    gdox_xenia_process_destroy(process);
    (void)gdox_test_remove(fixture->nested_stopped);
    (void)gdox_test_remove(fixture->nested_armed);
    (void)gdox_test_remove(fixture->nested_ready);
    (void)gdox_test_remove(fixture->interrupt_signal);
    (void)gdox_test_remove(fixture->process_ready);
    if (environment_set) {
        if (unsetenv("GDOX_XENIA_TEST_NESTED_SECOND_INT") != 0
            || unsetenv("GDOX_XENIA_TEST_HELPER") != 0
            || unsetenv("GDOX_XENIA_TEST_NESTED_STOPPED") != 0
            || unsetenv("GDOX_XENIA_TEST_NESTED_ARMED") != 0
            || unsetenv("GDOX_XENIA_TEST_NESTED_READY") != 0
            || unsetenv("GDOX_XENIA_TEST_INTERRUPT_SIGNAL") != 0
            || unsetenv("GDOX_XENIA_TEST_READY") != 0
            || unsetenv("GDOX_XENIA_TEST_HOLD") != 0) {
            (void)fputs("could not clear Xenia test environment\n", stderr);
            ++gdox_test_failures;
        }
    }
#undef PROCESS_CHECK
}

static void test_linux_forced_stop(const xenia_fixture *fixture)
{
    gdox_xenia_runtime_descriptor descriptor;
    gdox_xenia_launch_policy policy = fixture_policy(&fixture->runtime);
    gdox_xenia_options options;
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_IMAGE,
        "fixture-disc.iso",
        0U,
    };
    gdox_xenia_process *process = NULL;
    gdox_error error;
    bool environment_set = false;
    bool running = true;
    int exit_code = -1;

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
            ++gdox_test_failures;                                              \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(setenv("GDOX_XENIA_TEST_HOLD", "1", 1) == 0);
    PROCESS_CHECK(setenv("GDOX_XENIA_TEST_IGNORE_INT", "1", 1) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_READY", fixture->process_ready, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_NESTED_READY", fixture->nested_ready, 1
    ) == 0);
    environment_set = true;
    PROCESS_CHECK(gdox_xenia_resolve_runtime(
        &fixture->runtime,
        fixture->launcher,
        &descriptor,
        &error
    ));
    options = fixture_options(&descriptor, &policy);
    options.console_output = true;
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    PROCESS_CHECK(wait_for_path(fixture->process_ready));
    PROCESS_CHECK(wait_for_path(fixture->nested_ready));
    PROCESS_CHECK(gdox_xenia_stop(process, 250U, &exit_code, &error));
    PROCESS_CHECK(!gdox_error_is_set(&error));
    PROCESS_CHECK(exit_code == 137);
    PROCESS_CHECK(gdox_xenia_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 137);
    PROCESS_CHECK(wait_for_recorded_process_exit(fixture->nested_ready));

cleanup:
    gdox_xenia_process_destroy(process);
    (void)gdox_test_remove(fixture->nested_ready);
    (void)gdox_test_remove(fixture->process_ready);
    if (environment_set) {
        if (unsetenv("GDOX_XENIA_TEST_NESTED_READY") != 0
            || unsetenv("GDOX_XENIA_TEST_READY") != 0
            || unsetenv("GDOX_XENIA_TEST_IGNORE_INT") != 0
            || unsetenv("GDOX_XENIA_TEST_HOLD") != 0) {
            (void)fputs("could not clear Xenia test environment\n", stderr);
            ++gdox_test_failures;
        }
    }
#undef PROCESS_CHECK
}

static void test_linux_bridge_failure(const xenia_fixture *fixture)
{
    static const char private_uri[] =
        "nbd://127.0.0.1:65535/0123456789abcdef0123456789abcdef";
    gdox_xenia_runtime_descriptor descriptor;
    gdox_xenia_launch_policy policy = fixture_policy(&fixture->runtime);
    gdox_xenia_options options;
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_PRIVATE_NBD,
        private_uri,
        4096U,
    };
    gdox_xenia_process *process = NULL;
    gdox_error error;
    const char *current_path = getenv("PATH");
    char *saved_path = current_path != NULL ? strdup(current_path) : NULL;
    char path[4096];
    bool environment_set = false;
    bool running = true;
    bool poll_failed = false;
    int exit_code = -1;
    unsigned int attempt;
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
            ++gdox_test_failures;                                              \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(current_path == NULL || saved_path != NULL);
    written = current_path != NULL
        ? snprintf(path, sizeof(path), "%s:%s", fixture->root, current_path)
        : snprintf(path, sizeof(path), "%s", fixture->root);
    PROCESS_CHECK(written >= 0 && (size_t)written < sizeof(path));
    PROCESS_CHECK(setenv("PATH", path, 1) == 0);
    PROCESS_CHECK(setenv("GDOX_XENIA_TEST_HOLD", "1", 1) == 0);
    PROCESS_CHECK(setenv("GDOX_XENIA_TEST_IGNORE_INT", "1", 1) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_READY", fixture->process_ready, 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_BRIDGE_LENGTH", "4096", 1
    ) == 0);
    PROCESS_CHECK(setenv(
        "GDOX_XENIA_TEST_BRIDGE_STOP", fixture->bridge_stop, 1
    ) == 0);
    environment_set = true;
    PROCESS_CHECK(gdox_xenia_resolve_runtime(
        &fixture->runtime,
        fixture->launcher,
        &descriptor,
        &error
    ));
    options = fixture_options(&descriptor, &policy);
    options.storage_root = fixture->root;
    options.console_output = true;
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    PROCESS_CHECK(wait_for_path(fixture->process_ready));
    PROCESS_CHECK(write_file(fixture->bridge_stop, "stop\n", 5U));
    for (attempt = 0U; attempt < 200U && !poll_failed; ++attempt) {
        if (!gdox_xenia_poll(process, &running, &exit_code, &error)) {
            poll_failed = true;
        } else {
            PROCESS_CHECK(running);
            wait_briefly();
        }
    }
    PROCESS_CHECK(poll_failed);
    PROCESS_CHECK(error.code == GDOX_ERROR_IO);
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 137);
    PROCESS_CHECK(gdox_xenia_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 137);

cleanup:
    gdox_xenia_process_destroy(process);
    (void)gdox_test_remove(fixture->process_ready);
    (void)gdox_test_remove(fixture->bridge_stop);
    if (environment_set) {
        if (unsetenv("GDOX_XENIA_TEST_BRIDGE_STOP") != 0
            || unsetenv("GDOX_XENIA_TEST_BRIDGE_LENGTH") != 0
            || unsetenv("GDOX_XENIA_TEST_READY") != 0
            || unsetenv("GDOX_XENIA_TEST_IGNORE_INT") != 0
            || unsetenv("GDOX_XENIA_TEST_HOLD") != 0) {
            (void)fputs("could not clear Xenia test environment\n", stderr);
            ++gdox_test_failures;
        }
    }
    if (saved_path != NULL) {
        if (setenv("PATH", saved_path, 1) != 0) {
            (void)fputs("could not restore test PATH\n", stderr);
            ++gdox_test_failures;
        }
    } else if (unsetenv("PATH") != 0) {
        (void)fputs("could not restore test PATH\n", stderr);
        ++gdox_test_failures;
    }
    free(saved_path);
#undef PROCESS_CHECK
}
#endif

#if defined(_WIN32)
static int windows_child_mode(int argc, char **argv)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--gdox-xenia-test-child") == 0) {
            return 23;
        }
        if (strcmp(
                argv[index],
                "--gdox-xenia-test-still-active-exit"
            ) == 0) {
            return STILL_ACTIVE;
        }
        if (strcmp(argv[index], "--gdox-xenia-test-hold") == 0) {
            for (;;) {
                Sleep(1000U);
            }
        }
    }
    return -1;
}

static void test_windows_process(const char *executable)
{
    gdox_hashes hashes;
    uint64_t length = 0U;
    char digest[GDOX_SHA256_BYTES * 2U + 1U];
    gdox_xenia_runtime runtime;
    gdox_xenia_runtime_descriptor descriptor = {0};
    gdox_xenia_launch_policy policy;
    gdox_xenia_options options;
    gdox_xenia_target target = {
        GDOX_XENIA_TARGET_IMAGE,
        "--gdox-xenia-test-child",
        0U,
    };
    gdox_xenia_process *process = NULL;
    gdox_error error;
    bool running = true;
    int exit_code = -1;
    unsigned int attempt;
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
            ++gdox_test_failures;                                              \
            goto cleanup;                                                      \
        }                                                                       \
    } while (false)

    PROCESS_CHECK(gdox_hash_file(executable, &hashes, &length, &error));
    gdox_hash_hex(
        hashes.sha256, sizeof(hashes.sha256), false, digest
    );
    runtime = (gdox_xenia_runtime){
        "fixturewin",
        "fixture.exe",
        digest,
        length,
        GDOX_XENIA_GPU_D3D12,
        false,
        true,
        true,
        true,
        true,
        false,
        true,
    };
    descriptor.definition = &runtime;
    descriptor.origin = GDOX_XENIA_RUNTIME_OVERRIDE;
    written = snprintf(
        descriptor.launcher, sizeof(descriptor.launcher), "%s", executable
    );
    PROCESS_CHECK(
        written >= 0 && (size_t)written < sizeof(descriptor.launcher)
    );
    written = snprintf(
        descriptor.payload, sizeof(descriptor.payload), "%s", executable
    );
    PROCESS_CHECK(
        written >= 0 && (size_t)written < sizeof(descriptor.payload)
    );
    policy = fixture_policy(&runtime);
    options = fixture_options(&descriptor, &policy);
    options.console_output = false;
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_xenia_poll(
            process, &running, &exit_code, &error
        ));
        if (running) {
            Sleep(10U);
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 23);
    gdox_xenia_process_destroy(process);
    process = NULL;

    running = true;
    exit_code = -1;
    target.location = "--gdox-xenia-test-still-active-exit";
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    for (attempt = 0U; attempt < 200U && running; ++attempt) {
        PROCESS_CHECK(gdox_xenia_poll(
            process, &running, &exit_code, &error
        ));
        if (running) {
            Sleep(10U);
        }
    }
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == STILL_ACTIVE);
    gdox_xenia_process_destroy(process);
    process = NULL;

    target.location = "--gdox-xenia-test-hold";
    options.console_output = true;
    PROCESS_CHECK(gdox_xenia_launch(&options, &target, &process, &error));
    PROCESS_CHECK(gdox_xenia_stop(process, 0U, &exit_code, &error));
    PROCESS_CHECK(!gdox_error_is_set(&error));
    PROCESS_CHECK(exit_code == 1);
    PROCESS_CHECK(gdox_xenia_poll(
        process, &running, &exit_code, &error
    ));
    PROCESS_CHECK(!running);
    PROCESS_CHECK(exit_code == 1);

cleanup:
    gdox_xenia_process_destroy(process);
#undef PROCESS_CHECK
}
#endif

int main(int argc, char **argv)
{
    xenia_fixture fixture;

#if defined(__linux__)
    {
        const int child_result = linux_child_mode(argc, argv);

        if (child_result >= 0) {
            return child_result;
        }
    }
#elif defined(_WIN32)
    {
        const int child_result = windows_child_mode(argc, argv);

        if (child_result >= 0) {
            return child_result;
        }
    }
#else
    (void)argc;
    (void)argv;
#endif
    if (!fixture_create(&fixture)) {
        (void)fputs("could not create Xenia runtime test fixture\n", stderr);
        fixture_destroy(&fixture);
        return 1;
    }
    test_private_uri_validation();
    test_target_capabilities(&fixture);
    test_payload_verification(&fixture);
    test_launch_plan(&fixture);
    test_crackdown_launch_plan(&fixture);
    test_halo_4_launch_plan(&fixture);
    test_mass_effect_launch_plan(&fixture);
    test_native_vulkan_launch_plan(&fixture);
#if defined(__linux__)
    test_bundled_bridge_precedes_path(&fixture);
    test_bridge_cleanup_failure_is_retriable(&fixture);
    test_linux_override_process(&fixture);
    test_linux_graceful_stop(&fixture);
    test_linux_reaped_leader_group_stop(&fixture, argv[0]);
    test_linux_forced_stop(&fixture);
    test_linux_bridge_failure(&fixture);
#elif defined(_WIN32)
    test_windows_process(argv[0]);
#endif
    fixture_destroy(&fixture);
    return gdox_test_failures == 0 ? 0 : 1;
}
