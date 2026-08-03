#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "gdox/emulator.h"
#include "core/emulator_configuration.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_file_configuration(void)
{
    static const char original[] =
        "[sys.files]\n"
        "bootrom_path = '/firmware/mcpx.bin'\n"
        "flashrom_path = \"C:\\\\Firmware\\\\bios.bin\"\n";
    char path[GDOX_EMULATOR_PATH_CAPACITY];
    char *updated = NULL;
    gdox_error error;

    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        original,
        "bootrom_path",
        path,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(path, "/firmware/mcpx.bin") == 0);
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        original,
        "flashrom_path",
        path,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(path, "C:\\Firmware\\bios.bin") == 0);
    GDOX_TEST_CHECK(gdox_emulator_configuration_set_file(
        original,
        "hdd_path",
        "C:\\Data\\A \"Player\"\\xbox.qcow2",
        &updated,
        &error
    ));
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        updated,
        "hdd_path",
        path,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(path, "C:\\Data\\A \"Player\"\\xbox.qcow2") == 0);
    free(updated);
    updated = NULL;
    GDOX_TEST_CHECK(gdox_emulator_configuration_set_file(
        original,
        "eeprom_path",
        "/managed/xemu/eeprom.bin",
        &updated,
        &error
    ));
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        updated,
        "eeprom_path",
        path,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(path, "/managed/xemu/eeprom.bin") == 0);
    free(updated);
}

#if !defined(_WIN32)

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static bool write_test_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    const size_t bytes = strlen(text);

    return file != NULL
        && fwrite(text, 1U, bytes, file) == bytes
        && fclose(file) == 0;
}

static char *save_environment(const char *name)
{
    const char *value = getenv(name);
    return value != NULL ? strdup(value) : NULL;
}

static void restore_environment(const char *name, char *value)
{
    if (value != NULL) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
    free(value);
}

static bool create_directory_tree(const char *path)
{
    char partial[GDOX_EMULATOR_PATH_CAPACITY];
    char *cursor;

    if (strlen(path) >= sizeof(partial)) {
        return false;
    }
    memcpy(partial, path, strlen(path) + 1U);
    cursor = partial[0] == '/' ? partial + 1U : partial;
    while ((cursor = strchr(cursor, '/')) != NULL) {
        *cursor = '\0';
        if (mkdir(partial, 0700) != 0 && errno != EEXIST) {
            return false;
        }
        *cursor++ = '/';
    }
    return mkdir(partial, 0700) == 0 || errno == EEXIST;
}

static bool remove_directory_branch(const char *leaf, const char *root)
{
    char current[GDOX_EMULATOR_PATH_CAPACITY];

    if (strlen(leaf) >= sizeof(current) || strlen(root) >= strlen(leaf)) {
        return false;
    }
    memcpy(current, leaf, strlen(leaf) + 1U);
    while (strcmp(current, root) != 0) {
        char *slash;

        if (rmdir(current) != 0) {
            return false;
        }
        slash = strrchr(current, '/');
        if (slash == NULL) {
            return false;
        }
        *slash = '\0';
    }
    return rmdir(root) == 0;
}

static void test_configuration_discovery_is_read_only(void)
{
    static const char external_text[] = "[sys.files]\n";
    char root[256];
    char home[320];
    char external_directory[512];
    char external_configuration[576];
    char managed_configuration[576];
    char discovered[GDOX_EMULATOR_PATH_CAPACITY];
    bool required = false;
    char *saved_home = save_environment("HOME");
    char *saved_xdg_data = save_environment("XDG_DATA_HOME");
    char *saved_xemu_configuration =
        save_environment("GDOX_XEMU_CONFIG");
    gdox_error error;

    (void)snprintf(
        root,
        sizeof(root),
        "./gdox-emulator-discovery-%ld-%lld",
        (long)getpid(),
        (long long)time(NULL)
    );
    (void)snprintf(home, sizeof(home), "%s/home", root);
#if defined(__APPLE__)
    (void)snprintf(
        external_directory,
        sizeof(external_directory),
        "%s/Library/Application Support/xemu/xemu",
        home
    );
    (void)snprintf(
        managed_configuration,
        sizeof(managed_configuration),
        "%s/Library/Application Support/org.gdox.gdox/xemu/xemu.toml",
        home
    );
#else
    (void)snprintf(
        external_directory,
        sizeof(external_directory),
        "%s/.local/share/xemu/xemu",
        home
    );
    (void)snprintf(
        managed_configuration,
        sizeof(managed_configuration),
        "%s/.local/share/gdox/xemu/xemu.toml",
        home
    );
#endif
    (void)snprintf(
        external_configuration,
        sizeof(external_configuration),
        "%s/xemu.toml",
        external_directory
    );
    GDOX_TEST_CHECK(create_directory_tree(external_directory));
    GDOX_TEST_CHECK(write_test_text(
        external_configuration,
        external_text
    ));
    GDOX_TEST_CHECK(setenv("HOME", home, 1) == 0);
    GDOX_TEST_CHECK(unsetenv("GDOX_XEMU_CONFIG") == 0);
    GDOX_TEST_CHECK(unsetenv("XDG_DATA_HOME") == 0);
    GDOX_TEST_CHECK(gdox_emulator_discover_configuration(
        "/bin/true",
        discovered,
        &required,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(discovered, external_configuration) == 0);
    GDOX_TEST_CHECK(!required);
    GDOX_TEST_CHECK(access(managed_configuration, F_OK) != 0);

    restore_environment(
        "GDOX_XEMU_CONFIG",
        saved_xemu_configuration
    );
    restore_environment("XDG_DATA_HOME", saved_xdg_data);
    restore_environment("HOME", saved_home);
    GDOX_TEST_CHECK(remove(external_configuration) == 0);
    GDOX_TEST_CHECK(remove_directory_branch(external_directory, root));
}

static char *read_test_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    size_t bytes;
    char *text;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    length = ftell(file);
    if (length < 0L
        || length > 1024L * 1024L
        || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    bytes = (size_t)length;
    text = calloc(bytes + 1U, 1U);
    if (text == NULL || fread(text, 1U, bytes, file) != bytes) {
        free(text);
        (void)fclose(file);
        return NULL;
    }
    (void)fclose(file);
    return text;
}

static void test_configuration_source_validation(void)
{
    static const char path[] = "gdox-xemu-config-fifo.tmp";
    char home[256];
    char *saved_home = save_environment("HOME");
    char *saved_xdg_data = save_environment("XDG_DATA_HOME");
    gdox_emulator_options options = {0};
    gdox_error error;
    size_t home_bytes;
    int written;

    GDOX_TEST_CHECK(getcwd(home, sizeof(home)) != NULL);
    home_bytes = strlen(home);
    written = snprintf(
        home + home_bytes,
        sizeof(home) - home_bytes,
        "/gdox-emulator-fifo-home-%ld",
        (long)getpid()
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(home) - home_bytes
    );
    (void)rmdir(home);
    GDOX_TEST_CHECK(mkdir(home, 0700) == 0);
    GDOX_TEST_CHECK(setenv("HOME", home, 1) == 0);
    GDOX_TEST_CHECK(unsetenv("XDG_DATA_HOME") == 0);
    (void)remove(path);
    GDOX_TEST_CHECK(mkfifo(path, 0600) == 0);
    options.executable = gdox_test_program_path;
    options.configuration = path;
    options.save_vault = home;
    GDOX_TEST_CHECK(setenv(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export", 1
    ) == 0);
    GDOX_TEST_CHECK(!gdox_emulator_prepare(&options, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(unsetenv("GDOX_TEST_XEMU_CAPABILITY_MODE") == 0);
    GDOX_TEST_CHECK(remove(path) == 0);
    restore_environment("XDG_DATA_HOME", saved_xdg_data);
    restore_environment("HOME", saved_home);
    GDOX_TEST_CHECK(rmdir(home) == 0);
}

static void exercise_targeted_configuration_and_process(
    const char *path,
    const char *home,
    gdox_emulator_process **process_output
)
{
    static const char original[] =
        "[general]\n"
        "show_welcome = true\n"
        "\n"
        "[display.quality]\n"
        "surface_scale = 7\n"
        "\n"
        "[display.window]\n"
        "fullscreen_on_startup = false\n"
        "\n"
        "[display.ui]\n"
        "aspect_ratio = '4x3'\n"
        "\n"
        "[sys.files]\n"
        "bootrom_path = '/preserved/mcpx.bin'\n";
    FILE *file;
    gdox_emulator_options options;
    gdox_error error;
    char *first;
    char *second;
    struct stat status;
    bool running = true;
    int exit_code = -1;
    unsigned int attempts;
    bool write_succeeded;
    int close_result;

    GDOX_TEST_CHECK(mkdir(home, 0700) == 0);
    GDOX_TEST_CHECK(setenv("HOME", home, 1) == 0);
    GDOX_TEST_CHECK(unsetenv("XDG_DATA_HOME") == 0);
    file = fopen(path, "wb");
    GDOX_TEST_CHECK(file != NULL);
    write_succeeded = fwrite(
        original, 1U, sizeof(original) - 1U, file
    ) == sizeof(original) - 1U;
    close_result = fclose(file);
    GDOX_TEST_CHECK(write_succeeded);
    GDOX_TEST_CHECK(close_result == 0);
    options.executable = gdox_test_program_path;
    options.configuration = path;
    options.save_vault = home;
    options.internal_resolution_scale = 2U;
    options.aspect = GDOX_EMULATOR_ASPECT_WIDESCREEN;
    options.fit = GDOX_EMULATOR_FIT_STRETCH;
    options.fullscreen = true;
    options.console_output = false;
    options.window_width = 1280U;
    options.window_height = 720U;
    GDOX_TEST_CHECK(gdox_emulator_prepare(&options, &error));
    first = read_test_text(path);
    GDOX_TEST_CHECK(first != NULL);
    GDOX_TEST_CHECK(strstr(first, "show_welcome = false\n") != NULL);
    GDOX_TEST_CHECK(
        strstr(first, "volatile_hard_disk = true\n") != NULL
    );
    GDOX_TEST_CHECK(strstr(first, "cache_shaders = false\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "surface_scale = 2\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "aspect_ratio = \"16x9\"\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "fit = \"stretch\"\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "fullscreen_on_startup = true\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "startup_size = \"last_used\"\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "last_width = 1280\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "last_height = 720\n") != NULL);
    GDOX_TEST_CHECK(strstr(first, "bootrom_path = '/preserved/mcpx.bin'\n") != NULL);
    GDOX_TEST_CHECK(stat(path, &status) == 0);
    GDOX_TEST_CHECK((status.st_mode & 0777) == 0600);
    GDOX_TEST_CHECK(gdox_emulator_prepare(&options, &error));
    second = read_test_text(path);
    GDOX_TEST_CHECK(second != NULL);
    GDOX_TEST_CHECK(strcmp(first, second) == 0);
    free(first);
    free(second);

    options.save_vault = NULL;
    GDOX_TEST_CHECK(!gdox_emulator_launch(
        &options,
        "nbd://127.0.0.1:1/test",
        process_output,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(*process_output == NULL);
    options.save_vault = home;
    GDOX_TEST_CHECK(
        gdox_emulator_launch(
            &options,
            "nbd://127.0.0.1:1/test",
            process_output,
            &error
        )
    );
    /*
     * The first translated process launch on Apple Silicon can spend more
     * than 100 ms warming Rosetta. Keep the poll non-blocking while allowing
     * a bounded five-second host scheduling window.
     */
    for (attempts = 0U; attempts < 5000U && running; ++attempts) {
        GDOX_TEST_CHECK(
            gdox_emulator_poll(
                *process_output, &running, &exit_code, &error
            )
        );
        if (running) {
            const struct timespec delay = {0, 1000000L};
            (void)nanosleep(&delay, NULL);
        }
    }
    GDOX_TEST_CHECK(!running);
    GDOX_TEST_CHECK(exit_code == 0);
}

static void test_targeted_configuration_and_process(void)
{
    static const char path[] = "gdox-xemu-config.tmp";
    char home[256];
    char *saved_home;
    char *saved_xdg_data;
    gdox_emulator_process *process = NULL;
    size_t home_bytes;
    int written;
    bool configuration_removed;
    bool home_removed;

    GDOX_TEST_CHECK(getcwd(home, sizeof(home)) != NULL);
    home_bytes = strlen(home);
    written = snprintf(
        home + home_bytes,
        sizeof(home) - home_bytes,
        "/gdox-emulator-target-home-%ld",
        (long)getpid()
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(home) - home_bytes
    );
    saved_home = save_environment("HOME");
    saved_xdg_data = save_environment("XDG_DATA_HOME");
    (void)remove(path);
    (void)rmdir(home);

    GDOX_TEST_CHECK(setenv(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export", 1
    ) == 0);
    exercise_targeted_configuration_and_process(path, home, &process);
    GDOX_TEST_CHECK(unsetenv("GDOX_TEST_XEMU_CAPABILITY_MODE") == 0);

    if (process != NULL) {
        gdox_emulator_process_destroy(process);
    }
    configuration_removed = remove(path) == 0 || errno == ENOENT;
    restore_environment("XDG_DATA_HOME", saved_xdg_data);
    restore_environment("HOME", saved_home);
    home_removed = rmdir(home) == 0 || errno == ENOENT;
    GDOX_TEST_CHECK(configuration_removed);
    GDOX_TEST_CHECK(home_removed);
    GDOX_TEST_CHECK(access(path, F_OK) != 0);
}

static void test_standalone_xemu_storage_is_preserved(void)
{
    char root[256];
    char home[320];
    char xemu_root[576];
    char shaders[640];
    char nested[704];
    char cache_file[768];
    char cache_list[640];
    char xemu_config[640];
    char eeprom[640];
    char managed_config[384];
    char *preflight_text;
    char *saved_home = save_environment("HOME");
    char *saved_xdg_data = save_environment("XDG_DATA_HOME");
    gdox_emulator_options options = {0};
    gdox_error error;
    size_t root_bytes;
    int written;

    GDOX_TEST_CHECK(getcwd(root, sizeof(root)) != NULL);
    root_bytes = strlen(root);
    written = snprintf(
        root + root_bytes,
        sizeof(root) - root_bytes,
        "/gdox-xemu-cache-cleanup-%ld-%lld",
        (long)getpid(),
        (long long)time(NULL)
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(root) - root_bytes
    );
    (void)snprintf(home, sizeof(home), "%s/home", root);
#if defined(__APPLE__)
    (void)snprintf(
        xemu_root,
        sizeof(xemu_root),
        "%s/Library/Application Support/xemu/xemu",
        home
    );
#else
    (void)snprintf(
        xemu_root,
        sizeof(xemu_root),
        "%s/.local/share/xemu/xemu",
        home
    );
#endif
    (void)snprintf(shaders, sizeof(shaders), "%s/shaders", xemu_root);
    (void)snprintf(nested, sizeof(nested), "%s/title", shaders);
    (void)snprintf(cache_file, sizeof(cache_file), "%s/shader.bin", nested);
    (void)snprintf(
        cache_list, sizeof(cache_list), "%s/shader_cache_list", xemu_root
    );
    (void)snprintf(xemu_config, sizeof(xemu_config), "%s/xemu.toml", xemu_root);
    (void)snprintf(eeprom, sizeof(eeprom), "%s/eeprom.bin", xemu_root);
    (void)snprintf(managed_config, sizeof(managed_config), "%s/managed.toml", root);
    GDOX_TEST_CHECK(create_directory_tree(nested));
    GDOX_TEST_CHECK(write_test_text(cache_file, "derived"));
    GDOX_TEST_CHECK(write_test_text(cache_list, "derived"));
    GDOX_TEST_CHECK(write_test_text(xemu_config, "preserved"));
    GDOX_TEST_CHECK(write_test_text(eeprom, "preserved"));
    GDOX_TEST_CHECK(write_test_text(managed_config, "[general]\n"));
    GDOX_TEST_CHECK(setenv("HOME", home, 1) == 0);
    GDOX_TEST_CHECK(unsetenv("XDG_DATA_HOME") == 0);
    options.executable = gdox_test_program_path;
    options.configuration = managed_config;
    options.internal_resolution_scale = 1U;
    options.aspect = GDOX_EMULATOR_ASPECT_AUTOMATIC;
    options.fit = GDOX_EMULATOR_FIT_SCALE;
    options.window_width = 1280U;
    options.window_height = 720U;
    GDOX_TEST_CHECK(setenv(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "malformed", 1
    ) == 0);
    GDOX_TEST_CHECK(!gdox_emulator_prepare(&options, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(access(shaders, F_OK) == 0);
    preflight_text = read_test_text(managed_config);
    GDOX_TEST_CHECK(preflight_text != NULL);
    if (preflight_text != NULL) {
        GDOX_TEST_CHECK(strcmp(preflight_text, "[general]\n") == 0);
    }
    free(preflight_text);
    GDOX_TEST_CHECK(unsetenv("GDOX_TEST_XEMU_CAPABILITY_MODE") == 0);
    GDOX_TEST_CHECK(!gdox_emulator_prepare(&options, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(access(shaders, F_OK) == 0);
    GDOX_TEST_CHECK(setenv(
        "GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export", 1
    ) == 0);
    GDOX_TEST_CHECK(gdox_emulator_prepare(&options, &error));
    GDOX_TEST_CHECK(access(shaders, F_OK) == 0);

    GDOX_TEST_CHECK(remove(cache_file) == 0);
    GDOX_TEST_CHECK(remove(cache_list) == 0);
    GDOX_TEST_CHECK(remove(eeprom) == 0);
    GDOX_TEST_CHECK(remove(xemu_config) == 0);
    GDOX_TEST_CHECK(unsetenv("GDOX_TEST_XEMU_CAPABILITY_MODE") == 0);

    restore_environment("XDG_DATA_HOME", saved_xdg_data);
    restore_environment("HOME", saved_home);
    GDOX_TEST_CHECK(remove(managed_config) == 0);
    {
        char *slash = strrchr(xemu_root, '/');

        GDOX_TEST_CHECK(slash != NULL);
        if (slash != NULL) {
            *slash = '\0';
            GDOX_TEST_CHECK(remove_directory_branch(nested, root));
        }
    }
}

#endif

void gdox_test_emulator(void)
{
    test_file_configuration();
#if !defined(_WIN32)
    test_configuration_discovery_is_read_only();
    test_configuration_source_validation();
    test_targeted_configuration_and_process();
    test_standalone_xemu_storage_is_preserved();
#endif
}
