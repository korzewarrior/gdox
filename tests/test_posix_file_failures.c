#define _POSIX_C_SOURCE 200809L

#include "test.h"
#include "gdox/emulator.h"
#include "gdox/source.h"
#include "core/ports/preservation_io.h"
#include "platform/user_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int gdox_test_failures = 0;
static char target[4096];
static int failed_descriptor = -1;
static bool fail_sync = false;

int __real_fsync(int descriptor);
int __wrap_fsync(int descriptor);

int __wrap_fsync(int descriptor)
{
    char link[64];
    char path[4096];
    (void)snprintf(link, sizeof(link), "/proc/self/fd/%d", descriptor);
    const ssize_t length = readlink(link, path, sizeof(path) - 1U);
    if (length >= 0) {
        path[length] = '\0';
        if (fail_sync && strncmp(path, target, strlen(target)) == 0
            && path[strlen(target)] == '.') {
            failed_descriptor = descriptor;
            errno = EIO;
            return -1;
        }
    }
    return __real_fsync(descriptor);
}

static void test_failed_sync(const char *helper, const char *directory)
{
    const uint8_t original[] = "[general]\n";
    gdox_error error;
    const gdox_emulator_options options = {
        .executable = helper,
        .configuration = target,
        .save_vault = directory,
        .internal_resolution_scale = 1U,
        .window_width = 1280U,
        .window_height = 720U,
    };
    GDOX_TEST_CHECK(gdox_storage_write_private(
        target, original, sizeof(original) - 1U, false, &error
    ));
    GDOX_TEST_CHECK(setenv("GDOX_TEST_XEMU_CAPABILITY_MODE", "save-export", 1) == 0);
    for (unsigned int writer = 0U; writer < 2U; ++writer) {
        uint8_t *data = NULL;
        size_t bytes = 0U;
        bool found = false;
        failed_descriptor = -1;
        fail_sync = true;
        const bool saved = writer == 0U
            ? gdox_storage_write_private(target, (const uint8_t *)"new", 3U, true, &error)
            : gdox_emulator_prepare(&options, &error);
        fail_sync = false;
        GDOX_TEST_CHECK(!saved && error.code == GDOX_ERROR_IO);
        GDOX_TEST_CHECK(failed_descriptor >= 0);
        GDOX_TEST_CHECK(fcntl(failed_descriptor, F_GETFD) == -1 && errno == EBADF);
        GDOX_TEST_CHECK(gdox_storage_read(target, 1024U, &data, &bytes, &found, &error));
        const bool unchanged = found && bytes == sizeof(original) - 1U
            && memcmp(data, original, bytes) == 0;
        free(data);
        GDOX_TEST_CHECK(unchanged);
    }
    GDOX_TEST_CHECK(unlink(target) == 0);
}

static void test_fifo_rejection(void)
{
    gdox_error error;
    gdox_sector_source source = {0};
    gdox_preservation_file *file = NULL;
    uint64_t length = 0U;
    uint8_t *data = NULL;
    size_t bytes = 0U;
    bool found = false;
    GDOX_TEST_CHECK(mkfifo(target, 0600) == 0);
    (void)alarm(3U);
    GDOX_TEST_CHECK(!gdox_source_open_file(target, &source, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(!gdox_storage_read(target, 1024U, &data, &bytes, &found, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(!gdox_preservation_file_open_read(target, &file, &length, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    (void)alarm(0U);
    GDOX_TEST_CHECK(unlink(target) == 0);
}

int main(int argc, char **argv)
{
    char directory[] = "gdox-file-failures-XXXXXX";
    if (argc != 2 || mkdtemp(directory) == NULL) return 2;
    char absolute[4096];
    if (getcwd(absolute, sizeof(absolute)) == NULL) return 2;
    const int written = snprintf(target, sizeof(target), "%s/%s/value", absolute, directory);
    if (written < 0 || (size_t)written >= sizeof(target)) return 2;
    test_failed_sync(argv[1], absolute);
    if (gdox_test_failures == 0) test_fifo_rejection();
    (void)unlink(target);
    if (rmdir(directory) != 0) ++gdox_test_failures;
    return gdox_test_failures == 0 ? 0 : 1;
}
