#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "gdox/emulator.h"
#include "platform/emulator_configuration.h"

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
        "C:\\Users\\A \"Player\"\\xbox.qcow2",
        &updated,
        &error
    ));
    GDOX_TEST_CHECK(gdox_emulator_configuration_get_file(
        updated,
        "hdd_path",
        path,
        &error
    ));
    GDOX_TEST_CHECK(strcmp(path, "C:\\Users\\A \"Player\"\\xbox.qcow2") == 0);
    free(updated);
}

#if !defined(_WIN32)

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

static void test_targeted_configuration_and_process(void)
{
    static const char path[] = "gdox-xemu-config.tmp";
    static const char original[] =
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
    FILE *file = fopen(path, "wb");
    gdox_emulator_options options;
    gdox_emulator_process *process = NULL;
    gdox_error error;
    char *first;
    char *second;
    struct stat status;
    bool running = true;
    int exit_code = -1;
    unsigned int attempts;

    GDOX_TEST_CHECK(file != NULL);
    GDOX_TEST_CHECK(
        fwrite(original, 1U, sizeof(original) - 1U, file) == sizeof(original) - 1U
    );
    GDOX_TEST_CHECK(fclose(file) == 0);
#if defined(__APPLE__)
    options.executable = "/usr/bin/true";
#else
    options.executable = "/bin/true";
#endif
    options.configuration = path;
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

    GDOX_TEST_CHECK(
        gdox_emulator_launch(
            &options,
            "nbd://127.0.0.1:1/test",
            &process,
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
            gdox_emulator_poll(process, &running, &exit_code, &error)
        );
        if (running) {
            const struct timespec delay = {0, 1000000L};
            (void)nanosleep(&delay, NULL);
        }
    }
    GDOX_TEST_CHECK(!running);
    GDOX_TEST_CHECK(exit_code == 0);
    gdox_emulator_process_destroy(process);
    GDOX_TEST_CHECK(remove(path) == 0);
}

#endif

void gdox_test_emulator(void)
{
    test_file_configuration();
#if !defined(_WIN32)
    test_targeted_configuration_and_process();
#endif
}
