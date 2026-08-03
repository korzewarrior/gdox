#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "core/xemu_capabilities.h"
#include "gdox/emulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_remove _unlink
#define gdox_test_rmdir _rmdir
static bool set_environment(const char *name, const char *value)
{
    return _putenv_s(name, value) == 0
        && SetEnvironmentVariableA(name, value) != 0;
}

static void clear_environment(const char *name)
{
    (void)_putenv_s(name, "");
    (void)SetEnvironmentVariableA(name, NULL);
}
#else
#include <sys/stat.h>
#include <unistd.h>
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_remove unlink
#define gdox_test_rmdir rmdir
static bool set_environment(const char *name, const char *value)
{
    return setenv(name, value, 1) == 0;
}

static void clear_environment(const char *name)
{
    (void)unsetenv(name);
}
#endif

static char *save_environment(const char *name)
{
    const char *value = getenv(name);
    char *saved;

    if (value == NULL) {
        return NULL;
    }
    saved = malloc(strlen(value) + 1U);
    if (saved != NULL) {
        memcpy(saved, value, strlen(value) + 1U);
    }
    return saved;
}

static void restore_environment(const char *name, char *saved)
{
    if (saved != NULL) {
        (void)set_environment(name, saved);
    } else {
        clear_environment(name);
    }
    free(saved);
}

static bool path_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

#if defined(_WIN32)
static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    const size_t bytes = strlen(text);

    return file != NULL
        && fwrite(text, 1U, bytes, file) == bytes
        && fclose(file) == 0;
}
#endif

#define CAPABILITY_CHECK(expression)                                      \
    do {                                                                  \
        if (!(expression)) {                                              \
            (void)fprintf(                                                \
                stderr, "%s:%d: check failed: %s\n",                    \
                __FILE__, __LINE__, #expression                           \
            );                                                            \
            ++gdox_test_failures;                                         \
            goto cleanup;                                                 \
        }                                                                 \
    } while (false)

void gdox_test_xemu_capabilities(void)
{
    static const char extended_response[] =
        "{\"schema\":1,\"runtime\":\"xemu\",\"storage\":{"
        "\"full_hdd_ram_cow\":true,\"backing_writes\":false,"
        "\"persistent_save_export\":false,"
        "\"max_dirty_bytes\":4294967296,\"unexpected\":true}}";
    static const char incomplete_save_response[] =
        "{\"schema\":3,\"runtime\":\"xemu\",\"storage\":{"
        "\"full_hdd_ram_cow\":true,\"backing_writes\":false,"
        "\"persistent_save_export\":true,"
        "\"persistent_save_scope\":"
        "\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\","
        "\"persistent_save_format\":\"logical-files-v2\","
        "\"max_dirty_bytes\":4294967296}}";
    char root[256];
    char real_home[320];
    char escaped_write[384];
    char *saved_home = save_environment("HOME");
#if !defined(_WIN32)
    char *saved_ld_preload = save_environment("LD_PRELOAD");
#endif
#if defined(_WIN32)
    char legacy_parent[384];
    char legacy_root[448];
    char configuration[320];
    char *saved_appdata = save_environment("APPDATA");
    gdox_emulator_options options = {0};
    bool legacy_parent_created = false;
    bool legacy_root_created = false;
    bool configuration_created = false;
#endif
    gdox_xemu_storage_capabilities parsed;
    gdox_error error;
    bool save_export = true;
    bool root_created = false;
    bool home_created = false;

    (void)snprintf(
        root,
        sizeof(root),
        "./gdox-xemu-capabilities-%d-%lld",
        gdox_test_getpid(),
        (long long)time(NULL)
    );
    (void)snprintf(real_home, sizeof(real_home), "%s/home", root);
    (void)snprintf(
        escaped_write, sizeof(escaped_write), "%s/probe.bin", real_home
    );

    CAPABILITY_CHECK(gdox_xemu_capabilities_parse(
        GDOX_XEMU_CAPABILITIES_FALSE_RESPONSE,
        strlen(GDOX_XEMU_CAPABILITIES_FALSE_RESPONSE),
        &parsed,
        &error
    ));
    CAPABILITY_CHECK(!parsed.persistent_save_export);
    CAPABILITY_CHECK(gdox_xemu_capabilities_parse(
        GDOX_XEMU_CAPABILITIES_TRUE_RESPONSE,
        strlen(GDOX_XEMU_CAPABILITIES_TRUE_RESPONSE),
        &parsed,
        &error
    ));
    CAPABILITY_CHECK(parsed.persistent_save_export);
    CAPABILITY_CHECK(!gdox_xemu_capabilities_parse(
        incomplete_save_response,
        strlen(incomplete_save_response),
        &parsed,
        &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CAPABILITY_CHECK(!gdox_xemu_capabilities_parse(
        extended_response,
        strlen(extended_response),
        &parsed,
        &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);

#if !defined(_WIN32)
    CAPABILITY_CHECK(set_environment("LD_PRELOAD", ""));
#endif
    clear_environment("GDOX_TEST_XEMU_CAPABILITY_MODE");
    CAPABILITY_CHECK(gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(!save_export);
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export"
    ));
    CAPABILITY_CHECK(gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(save_export);
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "isolation-check"
    ));
    CAPABILITY_CHECK(gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(!save_export);

    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "malformed"
    ));
    CAPABILITY_CHECK(!gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "unknown-field"
    ));
    CAPABILITY_CHECK(!gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "stderr"
    ));
    CAPABILITY_CHECK(!gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "nonzero"
    ));
    CAPABILITY_CHECK(!gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "oversized"
    ));
    CAPABILITY_CHECK(!gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);

    CAPABILITY_CHECK(gdox_test_mkdir(root) == 0);
    root_created = true;
    CAPABILITY_CHECK(gdox_test_mkdir(real_home) == 0);
    home_created = true;
    CAPABILITY_CHECK(set_environment("HOME", real_home));
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "profile-write"
    ));
    CAPABILITY_CHECK(gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(!path_exists(escaped_write));

    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "hang"
    ));
    CAPABILITY_CHECK(!gdox_emulator_query_storage_capabilities(
        gdox_test_program_path, &save_export, &error
    ));
    CAPABILITY_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);

#if defined(_WIN32)
    (void)snprintf(
        legacy_parent, sizeof(legacy_parent), "%s/xemu", real_home
    );
    (void)snprintf(
        legacy_root, sizeof(legacy_root), "%s/xemu", legacy_parent
    );
    (void)snprintf(
        configuration, sizeof(configuration), "%s/xemu.toml", root
    );
    CAPABILITY_CHECK(gdox_test_mkdir(legacy_parent) == 0);
    legacy_parent_created = true;
    CAPABILITY_CHECK(write_text(legacy_root, "not-a-directory"));
    legacy_root_created = true;
    CAPABILITY_CHECK(write_text(configuration, "[general]\n"));
    configuration_created = true;
    CAPABILITY_CHECK(set_environment("APPDATA", real_home));
    CAPABILITY_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export"
    ));
    options.executable = gdox_test_program_path;
    options.configuration = configuration;
    options.internal_resolution_scale = 1U;
    options.aspect = GDOX_EMULATOR_ASPECT_AUTOMATIC;
    options.fit = GDOX_EMULATOR_FIT_SCALE;
    options.window_width = 1280U;
    options.window_height = 720U;
    CAPABILITY_CHECK(gdox_emulator_prepare(&options, &error));
    CAPABILITY_CHECK(path_exists(legacy_root));
#endif

cleanup:
    clear_environment("GDOX_TEST_XEMU_CAPABILITY_MODE");
    restore_environment("HOME", saved_home);
#if !defined(_WIN32)
    restore_environment("LD_PRELOAD", saved_ld_preload);
#endif
#if defined(_WIN32)
    restore_environment("APPDATA", saved_appdata);
    if (configuration_created) {
        (void)gdox_test_remove(configuration);
    }
    if (legacy_root_created) {
        (void)gdox_test_remove(legacy_root);
    }
    if (legacy_parent_created) {
        (void)gdox_test_rmdir(legacy_parent);
    }
#endif
    if (path_exists(escaped_write)) {
        (void)gdox_test_remove(escaped_write);
    }
    if (home_created) {
        (void)gdox_test_rmdir(real_home);
    }
    if (root_created) {
        (void)gdox_test_rmdir(root);
    }
}
