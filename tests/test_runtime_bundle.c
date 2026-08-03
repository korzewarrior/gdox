#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "app/runtime_bundle.h"
#include "core/emulator_configuration.h"
#include "gdox/hash.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool gdox_runtime_bundle_prepare_fixture(
    const char *executable_override,
    const char *clean_hdd_fixture,
    gdox_runtime_bundle_status *status,
    gdox_error *error
);

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

static bool write_qcow_hdd_fixture(const char *path)
{
    static const unsigned char header[] = {0x51U, 0x46U, 0x49U, 0xfbU};
    static const long final_offset = 16L * 64L * 1024L - 1L;
    FILE *file = fopen(path, "wb");

    return file != NULL
        && fwrite(header, 1U, sizeof(header), file) == sizeof(header)
        && fseek(file, final_offset, SEEK_SET) == 0
        && fputc(0, file) == 0
        && fclose(file) == 0;
}

static bool install_test_executable(const char *path)
{
#if defined(_WIN32)
    return CopyFileA(gdox_test_program_path, path, FALSE) != 0;
#else
    return symlink(gdox_test_program_path, path) == 0;
#endif
}

static bool file_size(const char *path, size_t *bytes)
{
    FILE *file = fopen(path, "rb");
    long length;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    length = ftell(file);
    if (length < 0L || fclose(file) != 0) {
        return false;
    }
    *bytes = (size_t)length;
    return true;
}

static bool read_text_fixture(
    const char *path,
    char *text,
    size_t capacity
)
{
    FILE *file = fopen(path, "rb");
    size_t bytes;

    if (file == NULL || capacity == 0U) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    bytes = fread(text, 1U, capacity - 1U, file);
    if (ferror(file) || !feof(file) || fclose(file) != 0) {
        return false;
    }
    text[bytes] = '\0';
    return true;
}

static bool read_fixture(
    const char *path,
    void *data,
    size_t bytes
)
{
    FILE *file = fopen(path, "rb");
    bool success;

    if (file == NULL) {
        return false;
    }
    success = fread(data, 1U, bytes, file) == bytes
        && fgetc(file) == EOF
        && !ferror(file);
    return fclose(file) == 0 && success;
}

#if defined(_WIN32)
static void test_windows_configuration_candidate(
    const char *root,
    const char *executable
)
{
    static const char source[] = "[sys.files]\n";
    char appdata[320];
    char xemu_parent[384];
    char xemu_directory[448];
    char external[512];
    char managed[512];
    char discovered[GDOX_EMULATOR_PATH_CAPACITY];
    bool required = false;
    char saved_appdata[GDOX_EMULATOR_PATH_CAPACITY];
    size_t missing_size;
    const DWORD saved_bytes = GetEnvironmentVariableA(
        "APPDATA",
        saved_appdata,
        (DWORD)sizeof(saved_appdata)
    );
    const bool appdata_was_set =
        saved_bytes != 0U && saved_bytes < (DWORD)sizeof(saved_appdata);
    gdox_error error;

    (void)snprintf(appdata, sizeof(appdata), "%s/appdata", root);
    (void)snprintf(xemu_parent, sizeof(xemu_parent), "%s/xemu", appdata);
    (void)snprintf(
        xemu_directory,
        sizeof(xemu_directory),
        "%s/xemu",
        xemu_parent
    );
    (void)snprintf(external, sizeof(external), "%s/xemu.toml", xemu_directory);
    (void)snprintf(
        managed,
        sizeof(managed),
        "%s/gdox/gdox/data/xemu/xemu.toml",
        appdata
    );
    GDOX_TEST_CHECK(gdox_test_mkdir(appdata) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(xemu_parent) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(xemu_directory) == 0);
    GDOX_TEST_CHECK(write_fixture(
        external,
        source,
        sizeof(source) - 1U
    ));
    clear_environment("GDOX_XEMU_CONFIG");
    GDOX_TEST_CHECK(set_environment("APPDATA", appdata));
    GDOX_TEST_CHECK(gdox_emulator_discover_configuration(
        executable,
        discovered,
        &required,
        &error
    ));
    GDOX_TEST_CHECK(!required);
    GDOX_TEST_CHECK(strstr(discovered, "xemu\\xemu\\xemu.toml") != NULL);
    GDOX_TEST_CHECK(!file_size(managed, &missing_size));

    if (appdata_was_set) {
        GDOX_TEST_CHECK(set_environment("APPDATA", saved_appdata));
    } else {
        clear_environment("APPDATA");
    }
    GDOX_TEST_CHECK(gdox_test_remove(external) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(xemu_directory) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(xemu_parent) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(appdata) == 0);
}

static void test_windows_legacy_configuration_hdd_is_ignored(
    const char *root,
    const char *executable,
    const char *bundled_hdd,
    const char *external_hdd,
    const char *managed_configuration
)
{
    char local_appdata[320];
    char gdox_directory[384];
    char xemu_directory[448];
    char legacy_configuration[512];
    char configuration_text[768];
    char managed_text[1024];
    char saved_local_appdata[GDOX_EMULATOR_PATH_CAPACITY];
    const DWORD saved_bytes = GetEnvironmentVariableA(
        "LOCALAPPDATA",
        saved_local_appdata,
        (DWORD)sizeof(saved_local_appdata)
    );
    const bool local_appdata_was_set =
        saved_bytes != 0U
        && saved_bytes < (DWORD)sizeof(saved_local_appdata);
    gdox_runtime_bundle_status status;
    gdox_error error;

    (void)snprintf(
        local_appdata,
        sizeof(local_appdata),
        "%s/localappdata",
        root
    );
    (void)snprintf(
        gdox_directory,
        sizeof(gdox_directory),
        "%s/GDOX",
        local_appdata
    );
    (void)snprintf(
        xemu_directory,
        sizeof(xemu_directory),
        "%s/xemu",
        gdox_directory
    );
    (void)snprintf(
        legacy_configuration,
        sizeof(legacy_configuration),
        "%s/xemu.toml",
        xemu_directory
    );
    (void)snprintf(
        configuration_text,
        sizeof(configuration_text),
        "[general]\nsource_marker = 'legacy-windows'\n\n"
        "[sys.files]\nhdd_path = '%s'\n",
        external_hdd
    );

    GDOX_TEST_CHECK(gdox_test_remove(managed_configuration) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(local_appdata) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(gdox_directory) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(xemu_directory) == 0);
    GDOX_TEST_CHECK(write_fixture(
        legacy_configuration,
        configuration_text,
        strlen(configuration_text)
    ));
    clear_environment("GDOX_XEMU_CONFIG");
    GDOX_TEST_CHECK(set_environment("LOCALAPPDATA", local_appdata));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        executable,
        bundled_hdd,
        &status,
        &error
    ));
    GDOX_TEST_CHECK(status.configuration_ready);
    GDOX_TEST_CHECK(status.hdd_ready);
    GDOX_TEST_CHECK(strcmp(status.hdd, bundled_hdd) == 0);
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        managed_text,
        sizeof(managed_text)
    ));
    GDOX_TEST_CHECK(strstr(managed_text, "legacy-windows") == NULL);
    GDOX_TEST_CHECK(strstr(managed_text, external_hdd) == NULL);
    GDOX_TEST_CHECK(strstr(managed_text, "eeprom_path = ") != NULL);
    GDOX_TEST_CHECK(read_text_fixture(
        legacy_configuration,
        managed_text,
        sizeof(managed_text)
    ));
    GDOX_TEST_CHECK(strcmp(managed_text, configuration_text) == 0);

    if (local_appdata_was_set) {
        GDOX_TEST_CHECK(set_environment(
            "LOCALAPPDATA",
            saved_local_appdata
        ));
    } else {
        clear_environment("LOCALAPPDATA");
    }
    GDOX_TEST_CHECK(gdox_test_remove(managed_configuration) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(legacy_configuration) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(xemu_directory) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(gdox_directory) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(local_appdata) == 0);
}
#endif

static void test_exact_file_removal(const char *root)
{
    static const uint8_t clean[] = "known clean private file";
    static const uint8_t dirty[] = "unknown private file";
    char path[320];
    gdox_storage_remove_result result;
    gdox_hashes hashes;
    gdox_error error;
    size_t bytes;

    (void)snprintf(path, sizeof(path), "%s/exact-private-file", root);
    GDOX_TEST_CHECK(gdox_hash_buffer(
        clean,
        sizeof(clean) - 1U,
        &hashes,
        &error
    ));
    GDOX_TEST_CHECK(write_fixture(path, dirty, sizeof(dirty) - 1U));
    GDOX_TEST_CHECK(gdox_storage_remove_exact_file(
        path,
        sizeof(clean) - 1U,
        hashes.sha256,
        &result,
        &error
    ));
    GDOX_TEST_CHECK(result == GDOX_STORAGE_REMOVE_MISMATCH);
    GDOX_TEST_CHECK(file_size(path, &bytes));
    GDOX_TEST_CHECK(bytes == sizeof(dirty) - 1U);
    GDOX_TEST_CHECK(gdox_test_remove(path) == 0);

    GDOX_TEST_CHECK(write_fixture(path, clean, sizeof(clean) - 1U));
    GDOX_TEST_CHECK(gdox_storage_remove_exact_file(
        path,
        sizeof(clean) - 1U,
        hashes.sha256,
        &result,
        &error
    ));
    GDOX_TEST_CHECK(result == GDOX_STORAGE_REMOVE_REMOVED);
    GDOX_TEST_CHECK(!file_size(path, &bytes));
    GDOX_TEST_CHECK(gdox_storage_remove_exact_file(
        path,
        sizeof(clean) - 1U,
        hashes.sha256,
        &result,
        &error
    ));
    GDOX_TEST_CHECK(result == GDOX_STORAGE_REMOVE_NOT_FOUND);
}

void gdox_test_runtime_bundle(void)
{
    static const char hdd_text[] = "bounded qcow fixture";
    static const unsigned char qcow_magic[] = {0x51U, 0x46U, 0x49U, 0xfbU};
    static const char invalid_mcpx[512] = {0};
    static const unsigned char bios_fill = 0xa5U;
    char root[256];
    char runtime[320];
    char xemu[384];
    char app_dir[512];
    char executable[640];
    char bundled_executable[640];
#if defined(__APPLE__)
    char contents[576];
    char macos[608];
#endif
    char hdd_directory[384];
    char hdd[448];
    char configuration[320];
    char data[320];
    char managed_directory[384];
    char managed_firmware[448];
    char managed_hdd[448];
    char managed_configuration[448];
    char managed_eeprom[448];
    char external_bios[320];
    char external_hdd[320];
    char external_mcpx[320];
    char external_eeprom[320];
    char configuration_text[1536];
    char external_original[1536];
    char managed_original[1536];
    char configured_eeprom[GDOX_EMULATOR_PATH_CAPACITY];
    unsigned char bios[256U * 1024U];
    unsigned char eeprom[256U];
    unsigned char eeprom_changed[256U];
    unsigned char eeprom_read[256U];
    gdox_runtime_bundle_status status;
    gdox_error error;
    FILE *managed;
    char text[1536];
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
#if defined(__APPLE__)
    (void)snprintf(app_dir, sizeof(app_dir), "%s/xemu.app", xemu);
    (void)snprintf(contents, sizeof(contents), "%s/Contents", app_dir);
    (void)snprintf(macos, sizeof(macos), "%s/MacOS", contents);
    (void)snprintf(
        executable, sizeof(executable), "%s/external-xemu", macos
    );
    (void)snprintf(
        bundled_executable,
        sizeof(bundled_executable),
        "%s/xemu",
        macos
    );
#else
    (void)snprintf(app_dir, sizeof(app_dir), "%s/AppDir", xemu);
    (void)snprintf(executable, sizeof(executable), "%s/AppRun", app_dir);
#if defined(_WIN32)
    (void)snprintf(
        bundled_executable,
        sizeof(bundled_executable),
        "%s/xemu.exe",
        xemu
    );
#else
    (void)snprintf(
        bundled_executable,
        sizeof(bundled_executable),
        "%s/xemu",
        xemu
    );
#endif
#endif
    (void)snprintf(hdd_directory, sizeof(hdd_directory), "%s/hdd", runtime);
    (void)snprintf(hdd, sizeof(hdd), "%s/xbox_hdd.qcow2", hdd_directory);
    (void)snprintf(configuration, sizeof(configuration), "%s/source.toml", root);
    (void)snprintf(data, sizeof(data), "%s/data", root);
    (void)snprintf(managed_directory, sizeof(managed_directory), "%s/xemu", data);
    (void)snprintf(managed_firmware, sizeof(managed_firmware), "%s/firmware", managed_directory);
    (void)snprintf(managed_hdd, sizeof(managed_hdd), "%s/xbox_hdd.qcow2", managed_directory);
    (void)snprintf(managed_configuration, sizeof(managed_configuration), "%s/xemu.toml", managed_directory);
    (void)snprintf(managed_eeprom, sizeof(managed_eeprom), "%s/eeprom.bin", managed_directory);
    (void)snprintf(external_bios, sizeof(external_bios), "%s/external-bios.bin", root);
    (void)snprintf(external_hdd, sizeof(external_hdd), "%s/external-hdd.qcow2", root);
    (void)snprintf(external_mcpx, sizeof(external_mcpx), "%s/external-mcpx.bin", root);
    (void)snprintf(external_eeprom, sizeof(external_eeprom), "%s/external-eeprom.bin", root);
    (void)snprintf(
        configuration_text,
        sizeof(configuration_text),
        "[general]\nsource_marker = 'external-unchanged'\n\n"
        "[sys.files]\nbootrom_path = '%s'\nflashrom_path = '%s'\n"
        "eeprom_path = '%s'\n",
        external_mcpx,
        external_bios,
        external_eeprom
    );
    memset(bios, bios_fill, sizeof(bios));
    memset(eeprom, 0x3c, sizeof(eeprom));
    memset(eeprom_changed, 0xc3, sizeof(eeprom_changed));

    GDOX_TEST_CHECK(gdox_test_mkdir(root) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(runtime) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(xemu) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(app_dir) == 0);
#if defined(__APPLE__)
    GDOX_TEST_CHECK(gdox_test_mkdir(contents) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(macos) == 0);
#endif
    GDOX_TEST_CHECK(gdox_test_mkdir(hdd_directory) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(data) == 0);
    test_exact_file_removal(root);
    GDOX_TEST_CHECK(install_test_executable(executable));
    GDOX_TEST_CHECK(gdox_test_chmod_executable(executable) == 0);
    GDOX_TEST_CHECK(install_test_executable(bundled_executable));
    GDOX_TEST_CHECK(gdox_test_chmod_executable(bundled_executable) == 0);
    GDOX_TEST_CHECK(write_qcow_hdd_fixture(hdd));
    GDOX_TEST_CHECK(write_fixture(external_bios, bios, sizeof(bios)));
    GDOX_TEST_CHECK(write_fixture(
        external_mcpx,
        invalid_mcpx,
        sizeof(invalid_mcpx)
    ));
    GDOX_TEST_CHECK(write_fixture(
        external_eeprom,
        eeprom,
        sizeof(eeprom)
    ));
    GDOX_TEST_CHECK(write_fixture(
        configuration,
        configuration_text,
        strlen(configuration_text)
    ));
    GDOX_TEST_CHECK(read_text_fixture(
        configuration,
        external_original,
        sizeof(external_original)
    ));

    GDOX_TEST_CHECK(set_environment("GDOX_RUNTIME_DIR", runtime));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export"
    ));
    GDOX_TEST_CHECK(!gdox_runtime_bundle_prepare(
        NULL, &status, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(strstr(error.message, "verified clean") != NULL);
    GDOX_TEST_CHECK(!file_size(managed_configuration, &bytes));
    GDOX_TEST_CHECK(!file_size(managed_hdd, &bytes));
    clear_environment("GDOX_TEST_XEMU_CAPABILITY_MODE");
    clear_environment("GDOX_RUNTIME_DIR");

    GDOX_TEST_CHECK(set_environment("GDOX_XEMU", executable));
    GDOX_TEST_CHECK(set_environment("GDOX_XEMU_CONFIG", configuration));
    GDOX_TEST_CHECK(set_environment("GDOX_DATA_HOME", data));

    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "malformed"
    ));
    GDOX_TEST_CHECK(!gdox_runtime_bundle_prepare(
        NULL, &status, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(!file_size(managed_configuration, &bytes));
    GDOX_TEST_CHECK(!file_size(managed_hdd, &bytes));
    clear_environment("GDOX_TEST_XEMU_CAPABILITY_MODE");

    GDOX_TEST_CHECK(!gdox_runtime_bundle_prepare(
        NULL, &status, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(strstr(error.message, "persistent logical save") != NULL);
    GDOX_TEST_CHECK(!file_size(managed_configuration, &bytes));
    GDOX_TEST_CHECK(!file_size(managed_hdd, &bytes));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export"
    ));

    GDOX_TEST_CHECK(!gdox_runtime_bundle_prepare(
        NULL, &status, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(strstr(error.message, "verified clean") != NULL);
    GDOX_TEST_CHECK(!file_size(managed_configuration, &bytes));
    GDOX_TEST_CHECK(!file_size(managed_hdd, &bytes));

    {
        const bool prepared = gdox_runtime_bundle_prepare_fixture(
            NULL, hdd, &status, &error
        );
        if (!prepared) {
            (void)fprintf(stderr, "runtime bundle fixture: %s\n", error.message);
        }
        GDOX_TEST_CHECK(prepared);
    }
    GDOX_TEST_CHECK(status.xemu_available);
    GDOX_TEST_CHECK(!status.bundled);
    GDOX_TEST_CHECK(status.custom_executable);
    GDOX_TEST_CHECK(status.full_hdd_isolation);
    GDOX_TEST_CHECK(status.persistent_save_export);
    GDOX_TEST_CHECK(status.configuration_ready);
    GDOX_TEST_CHECK(!status.mcpx_ready);
    GDOX_TEST_CHECK(status.flash_ready);
    GDOX_TEST_CHECK(status.hdd_ready);
    GDOX_TEST_CHECK(strcmp(status.executable, executable) == 0);
#if defined(_WIN32)
    GDOX_TEST_CHECK(strstr(status.configuration, "xemu.toml") != NULL);
#else
    GDOX_TEST_CHECK(strcmp(status.configuration, managed_configuration) == 0);
#endif

    GDOX_TEST_CHECK(!file_size(managed_hdd, &bytes));
    GDOX_TEST_CHECK(strcmp(status.hdd, hdd) == 0);
    GDOX_TEST_CHECK(file_size(hdd, &bytes));
    GDOX_TEST_CHECK(bytes == (size_t)16U * 64U * 1024U);
    managed = fopen(hdd, "rb");
    GDOX_TEST_CHECK(managed != NULL);
    bytes = fread(text, 1U, sizeof(qcow_magic), managed);
    GDOX_TEST_CHECK(fclose(managed) == 0);
    GDOX_TEST_CHECK(bytes == sizeof(qcow_magic));
    GDOX_TEST_CHECK(memcmp(text, qcow_magic, sizeof(qcow_magic)) == 0);
    managed = fopen(managed_configuration, "rb");
    GDOX_TEST_CHECK(managed != NULL);
    bytes = fread(text, 1U, sizeof(text) - 1U, managed);
    GDOX_TEST_CHECK(fclose(managed) == 0);
    text[bytes] = '\0';
    GDOX_TEST_CHECK(strstr(text, "hdd_path = ") != NULL);
    GDOX_TEST_CHECK(strstr(text, "flashrom_path = ") != NULL);
    GDOX_TEST_CHECK(strstr(text, "eeprom_path = ") != NULL);
    GDOX_TEST_CHECK(strstr(text, "source_marker") == NULL);
    GDOX_TEST_CHECK(strstr(text, external_eeprom) == NULL);
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        text,
        "eeprom_path",
        configured_eeprom,
        &error
    ));
#if defined(_WIN32)
    GDOX_TEST_CHECK(strstr(text, "xbox_hdd.qcow2") != NULL);
#else
    GDOX_TEST_CHECK(strstr(text, hdd) != NULL);
#endif
    GDOX_TEST_CHECK(file_size(status.flash, &bytes));
    GDOX_TEST_CHECK(bytes == sizeof(bios));
    GDOX_TEST_CHECK(!file_size(status.mcpx, &bytes));
    GDOX_TEST_CHECK(strcmp(status.eeprom, configured_eeprom) == 0);
#if defined(_WIN32)
    GDOX_TEST_CHECK(strstr(status.eeprom, "xemu\\eeprom.bin") != NULL);
#else
    GDOX_TEST_CHECK(strcmp(status.eeprom, managed_eeprom) == 0);
#endif
    GDOX_TEST_CHECK(file_size(status.eeprom, &bytes));
    GDOX_TEST_CHECK(bytes == sizeof(eeprom));
    GDOX_TEST_CHECK(read_fixture(
        status.eeprom,
        eeprom_read,
        sizeof(eeprom_read)
    ));
    GDOX_TEST_CHECK(memcmp(eeprom_read, eeprom, sizeof(eeprom)) == 0);
    GDOX_TEST_CHECK(read_text_fixture(
        configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, external_original) == 0);

    GDOX_TEST_CHECK(write_fixture(
        managed_hdd,
        hdd_text,
        sizeof(hdd_text) - 1U
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL,
        hdd,
        &status,
        &error
    ));
    GDOX_TEST_CHECK(file_size(managed_hdd, &bytes));
    GDOX_TEST_CHECK(bytes == sizeof(hdd_text) - 1U);
    GDOX_TEST_CHECK(read_text_fixture(
        managed_hdd, text, sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, hdd_text) == 0);

    GDOX_TEST_CHECK(write_fixture(
        external_eeprom,
        eeprom_changed,
        sizeof(eeprom_changed)
    ));
    (void)snprintf(
        text,
        sizeof(text),
        "[general]\nsource_marker = 'poisoned-managed'\n\n"
        "[sys.files]\nhdd_path = '%s'\neeprom_path = '%s'\n",
        managed_hdd,
        external_eeprom
    );
    GDOX_TEST_CHECK(write_fixture(
        managed_configuration,
        text,
        strlen(text)
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(status.flash_ready);
    GDOX_TEST_CHECK(read_fixture(
        status.eeprom,
        eeprom_read,
        sizeof(eeprom_read)
    ));
    GDOX_TEST_CHECK(memcmp(eeprom_read, eeprom, sizeof(eeprom)) == 0);
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strstr(text, "poisoned-managed") == NULL);
    GDOX_TEST_CHECK(strstr(text, external_eeprom) == NULL);
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        text,
        "eeprom_path",
        configured_eeprom,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(configured_eeprom, status.eeprom) == 0);
    GDOX_TEST_CHECK(write_fixture(
        managed_eeprom,
        "bad",
        sizeof("bad") - 1U
    ));
    GDOX_TEST_CHECK(!gdox_runtime_bundle_prepare_fixture(
        NULL,
        hdd,
        &status,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(gdox_test_remove(managed_eeprom) == 0);
    GDOX_TEST_CHECK(write_fixture(
        external_eeprom,
        "bad",
        sizeof("bad") - 1U
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(!file_size(status.eeprom, &bytes));
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        text,
        "eeprom_path",
        configured_eeprom,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(configured_eeprom, status.eeprom) == 0);
    GDOX_TEST_CHECK(write_fixture(
        external_eeprom,
        eeprom,
        sizeof(eeprom)
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(read_fixture(
        status.eeprom,
        eeprom_read,
        sizeof(eeprom_read)
    ));
    GDOX_TEST_CHECK(memcmp(eeprom_read, eeprom, sizeof(eeprom)) == 0);
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        managed_original,
        sizeof(managed_original)
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, managed_original) == 0);
    GDOX_TEST_CHECK(read_text_fixture(
        configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, external_original) == 0);

    (void)snprintf(
        text,
        sizeof(text),
        "[sys.files]\nhdd_path = '%s'\n",
        managed_hdd
    );
    GDOX_TEST_CHECK(gdox_test_remove(status.flash) == 0);
    GDOX_TEST_CHECK(write_fixture(
        managed_configuration,
        text,
        strlen(text)
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(status.flash_ready);
    GDOX_TEST_CHECK(file_size(status.flash, &bytes));
    GDOX_TEST_CHECK(bytes == sizeof(bios));
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strstr(text, "flashrom_path = ") != NULL);
    GDOX_TEST_CHECK(read_text_fixture(
        configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, external_original) == 0);

    GDOX_TEST_CHECK(write_fixture(external_bios, "bad", 3U));
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        managed_original,
        sizeof(managed_original)
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(status.flash_ready);
    GDOX_TEST_CHECK(file_size(status.flash, &bytes));
    GDOX_TEST_CHECK(bytes == sizeof(bios));
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, managed_original) == 0);

    GDOX_TEST_CHECK(gdox_test_remove(status.flash) == 0);
    GDOX_TEST_CHECK(write_fixture(
        managed_configuration,
        "[sys.files]\n",
        sizeof("[sys.files]\n") - 1U
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(!status.flash_ready);
    GDOX_TEST_CHECK(!file_size(status.flash, &bytes));

    GDOX_TEST_CHECK(
        gdox_runtime_bundle_prepare_fixture(
            executable, hdd, &status, &error
        )
    );
    GDOX_TEST_CHECK(status.xemu_available);
    GDOX_TEST_CHECK(status.custom_executable);
    GDOX_TEST_CHECK(!status.bundled);
    GDOX_TEST_CHECK(strcmp(status.executable, executable) == 0);
    GDOX_TEST_CHECK(status.configuration_ready);

    GDOX_TEST_CHECK(
        gdox_runtime_bundle_prepare_fixture(
            executable, hdd, &status, &error
        )
    );
    GDOX_TEST_CHECK(status.hdd_ready);
    GDOX_TEST_CHECK(strcmp(status.hdd, hdd) == 0);
    managed = fopen(managed_configuration, "rb");
    GDOX_TEST_CHECK(managed != NULL);
    bytes = fread(text, 1U, sizeof(text) - 1U, managed);
    GDOX_TEST_CHECK(fclose(managed) == 0);
    text[bytes] = '\0';
    GDOX_TEST_CHECK(strstr(text, hdd) != NULL);

    GDOX_TEST_CHECK(gdox_test_remove(managed_configuration) == 0);
    GDOX_TEST_CHECK(write_fixture(
        external_hdd,
        hdd_text,
        sizeof(hdd_text) - 1U
    ));
    (void)snprintf(
        configuration_text,
        sizeof(configuration_text),
        "[general]\nsource_marker = 'external-hdd'\n\n"
        "[sys.files]\nhdd_path = '%s'\n",
        external_hdd
    );
    GDOX_TEST_CHECK(write_fixture(
        configuration,
        configuration_text,
        strlen(configuration_text)
    ));
    GDOX_TEST_CHECK(gdox_runtime_bundle_prepare_fixture(
        NULL, hdd, &status, &error
    ));
    GDOX_TEST_CHECK(status.hdd_ready);
    GDOX_TEST_CHECK(status.configuration_ready);
    GDOX_TEST_CHECK(strcmp(status.hdd, hdd) == 0);
#if defined(_WIN32)
    GDOX_TEST_CHECK(strstr(status.configuration, "xemu.toml") != NULL);
#else
    GDOX_TEST_CHECK(strcmp(status.configuration, managed_configuration) == 0);
#endif
    GDOX_TEST_CHECK(read_text_fixture(
        configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strcmp(text, configuration_text) == 0);
    GDOX_TEST_CHECK(read_text_fixture(
        managed_configuration,
        text,
        sizeof(text)
    ));
    GDOX_TEST_CHECK(strstr(text, "source_marker") == NULL);
    GDOX_TEST_CHECK(strstr(text, external_hdd) == NULL);
    GDOX_TEST_CHECK(strstr(text, "xbox_hdd.qcow2") != NULL);
    GDOX_TEST_CHECK(strstr(text, "eeprom_path = ") != NULL);

#if defined(_WIN32)
    test_windows_configuration_candidate(root, executable);
    test_windows_legacy_configuration_hdd_is_ignored(
        root,
        executable,
        hdd,
        external_hdd,
        managed_configuration
    );
#endif
    if (file_size(managed_configuration, &bytes)) {
        GDOX_TEST_CHECK(gdox_test_remove(managed_configuration) == 0);
    }
    (void)snprintf(
        text,
        sizeof(text),
        "%s/missing-explicit.toml",
        root
    );
    GDOX_TEST_CHECK(set_environment("GDOX_XEMU_CONFIG", text));
    GDOX_TEST_CHECK(!gdox_runtime_bundle_prepare_fixture(
        executable,
        hdd,
        &status,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_NOT_FOUND);

    clear_environment("GDOX_DATA_HOME");
    clear_environment("GDOX_XEMU_CONFIG");
    clear_environment("GDOX_XEMU");
    (void)gdox_test_remove(status.flash);
    (void)gdox_test_remove(status.mcpx);
    (void)gdox_test_remove(managed_configuration);
    (void)gdox_test_remove(managed_hdd);
    (void)gdox_test_remove(managed_eeprom);
    (void)gdox_test_remove(configuration);
    (void)gdox_test_remove(external_mcpx);
    (void)gdox_test_remove(external_bios);
    (void)gdox_test_remove(external_hdd);
    (void)gdox_test_remove(external_eeprom);
    (void)gdox_test_remove(hdd);
    (void)gdox_test_remove(bundled_executable);
    (void)gdox_test_remove(executable);
    (void)gdox_test_rmdir(managed_firmware);
    (void)gdox_test_rmdir(managed_directory);
    (void)gdox_test_rmdir(data);
    (void)gdox_test_rmdir(hdd_directory);
#if defined(__APPLE__)
    (void)gdox_test_rmdir(macos);
    (void)gdox_test_rmdir(contents);
#endif
    (void)gdox_test_rmdir(app_dir);
    (void)gdox_test_rmdir(xemu);
    (void)gdox_test_rmdir(runtime);
    (void)gdox_test_rmdir(root);
}
