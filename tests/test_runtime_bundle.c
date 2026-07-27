#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "app/runtime_bundle.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_chmod_executable(path) ((void)(path), 0)
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_remove _unlink
#define gdox_test_rmdir _rmdir
static bool set_environment(const char *name, const char *value)
{
    return SetEnvironmentVariableA(name, value) != 0;
}
static void clear_environment(const char *name)
{
    (void)SetEnvironmentVariableA(name, NULL);
}
#else
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#define gdox_test_chmod_executable(path) chmod(path, 0700)
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

static bool write_fixture(
    const char *path,
    const void *data,
    size_t bytes
)
{
    FILE *file = fopen(path, "wb");
    return file != NULL
        && fwrite(data, 1U, bytes, file) == bytes
        && fclose(file) == 0;
}

void gdox_test_runtime_bundle(void)
{
    static const char configuration_text[] = "[sys.files]\n";
    static const char hdd_text[] = "bounded qcow fixture";
    char root[256];
    char runtime[320];
    char xemu[384];
    char app_dir[448];
    char executable[512];
    char hdd_directory[384];
    char hdd[448];
    char configuration[320];
    char data[320];
    char managed_directory[384];
    char managed_hdd[448];
    char managed_configuration[448];
    gdox_runtime_bundle_status status;
    gdox_error error;
    FILE *managed;
    char text[1024];
    size_t bytes;

    (void)snprintf(
        root,
        sizeof(root),
        "./gdox-bundle-%d-%lld",
        gdox_test_getpid(),
        (long long)time(NULL)
    );
    (void)snprintf(runtime, sizeof(runtime), "%s/runtime", root);
    (void)snprintf(xemu, sizeof(xemu), "%s/xemu", runtime);
    (void)snprintf(app_dir, sizeof(app_dir), "%s/AppDir", xemu);
    (void)snprintf(executable, sizeof(executable), "%s/AppRun", app_dir);
    (void)snprintf(hdd_directory, sizeof(hdd_directory), "%s/hdd", runtime);
    (void)snprintf(hdd, sizeof(hdd), "%s/xbox_hdd.qcow2", hdd_directory);
    (void)snprintf(configuration, sizeof(configuration), "%s/source.toml", root);
    (void)snprintf(data, sizeof(data), "%s/data", root);
    (void)snprintf(managed_directory, sizeof(managed_directory), "%s/xemu", data);
    (void)snprintf(managed_hdd, sizeof(managed_hdd), "%s/xbox_hdd.qcow2", managed_directory);
    (void)snprintf(managed_configuration, sizeof(managed_configuration), "%s/xemu.toml", managed_directory);

    GDOX_TEST_CHECK(gdox_test_mkdir(root) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(runtime) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(xemu) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(app_dir) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(hdd_directory) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(data) == 0);
    GDOX_TEST_CHECK(write_fixture(executable, "", 0U));
    GDOX_TEST_CHECK(gdox_test_chmod_executable(executable) == 0);
    GDOX_TEST_CHECK(write_fixture(hdd, hdd_text, sizeof(hdd_text) - 1U));
    GDOX_TEST_CHECK(write_fixture(
        configuration,
        configuration_text,
        sizeof(configuration_text) - 1U
    ));
    GDOX_TEST_CHECK(set_environment("GDOX_XEMU", executable));
    GDOX_TEST_CHECK(set_environment("GDOX_XEMU_CONFIG", configuration));
    GDOX_TEST_CHECK(set_environment("GDOX_DATA_HOME", data));

    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare(NULL, NULL, &status, &error));
    GDOX_TEST_CHECK(status.xemu_available);
    GDOX_TEST_CHECK(status.bundled);
    GDOX_TEST_CHECK(status.configuration_ready);
    GDOX_TEST_CHECK(!status.mcpx_ready);
    GDOX_TEST_CHECK(!status.flash_ready);
    GDOX_TEST_CHECK(status.hdd_ready);
    GDOX_TEST_CHECK(!status.custom_hdd);
    GDOX_TEST_CHECK(strcmp(status.executable, executable) == 0);
#if defined(_WIN32)
    GDOX_TEST_CHECK(strstr(status.configuration, "xemu.toml") != NULL);
#else
    GDOX_TEST_CHECK(strcmp(status.configuration, managed_configuration) == 0);
#endif

    managed = fopen(managed_hdd, "rb");
    GDOX_TEST_CHECK(managed != NULL);
    bytes = fread(text, 1U, sizeof(text), managed);
    GDOX_TEST_CHECK(fclose(managed) == 0);
    GDOX_TEST_CHECK(bytes == sizeof(hdd_text) - 1U);
    GDOX_TEST_CHECK(memcmp(text, hdd_text, bytes) == 0);
    managed = fopen(managed_configuration, "rb");
    GDOX_TEST_CHECK(managed != NULL);
    bytes = fread(text, 1U, sizeof(text) - 1U, managed);
    GDOX_TEST_CHECK(fclose(managed) == 0);
    text[bytes] = '\0';
    GDOX_TEST_CHECK(strstr(text, "hdd_path = ") != NULL);
#if defined(_WIN32)
    GDOX_TEST_CHECK(strstr(text, "xbox_hdd.qcow2") != NULL);
#else
    GDOX_TEST_CHECK(strstr(text, managed_hdd) != NULL);
#endif

    GDOX_TEST_CHECK(
        gdox_runtime_bundle_prepare(executable, NULL, &status, &error)
    );
    GDOX_TEST_CHECK(status.xemu_available);
    GDOX_TEST_CHECK(status.custom_executable);
    GDOX_TEST_CHECK(!status.bundled);
    GDOX_TEST_CHECK(strcmp(status.executable, executable) == 0);
    GDOX_TEST_CHECK(status.configuration_ready);

    GDOX_TEST_CHECK(
        gdox_runtime_bundle_prepare(executable, hdd, &status, &error)
    );
    GDOX_TEST_CHECK(status.hdd_ready);
    GDOX_TEST_CHECK(status.custom_hdd);
    GDOX_TEST_CHECK(strcmp(status.hdd, hdd) == 0);
    managed = fopen(managed_configuration, "rb");
    GDOX_TEST_CHECK(managed != NULL);
    bytes = fread(text, 1U, sizeof(text) - 1U, managed);
    GDOX_TEST_CHECK(fclose(managed) == 0);
    text[bytes] = '\0';
    GDOX_TEST_CHECK(strstr(text, hdd) != NULL);

    clear_environment("GDOX_DATA_HOME");
    clear_environment("GDOX_XEMU_CONFIG");
    clear_environment("GDOX_XEMU");
    (void)gdox_test_remove(managed_configuration);
    (void)gdox_test_remove(managed_hdd);
    (void)gdox_test_remove(configuration);
    (void)gdox_test_remove(hdd);
    (void)gdox_test_remove(executable);
    (void)gdox_test_rmdir(managed_directory);
    (void)gdox_test_rmdir(data);
    (void)gdox_test_rmdir(hdd_directory);
    (void)gdox_test_rmdir(app_dir);
    (void)gdox_test_rmdir(xemu);
    (void)gdox_test_rmdir(runtime);
    (void)gdox_test_rmdir(root);
}
