#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "platform/session_storage.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define gdox_test_remove _unlink
#define gdox_test_rmdir _rmdir
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define gdox_test_access access
#define gdox_test_getcwd getcwd
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_remove unlink
#define gdox_test_rmdir rmdir
#endif

static bool join_path(
    char output[GDOX_SESSION_PATH_CAPACITY],
    const char *root,
    const char *name
)
{
    const int written = snprintf(
        output, GDOX_SESSION_PATH_CAPACITY, "%s/%s", root, name
    );

    return written >= 0
        && (size_t)written < GDOX_SESSION_PATH_CAPACITY;
}

static bool parent_path(
    char output[GDOX_SESSION_PATH_CAPACITY],
    const char *path
)
{
    char *separator;

    if (snprintf(output, GDOX_SESSION_PATH_CAPACITY, "%s", path) < 0
        || strlen(path) >= GDOX_SESSION_PATH_CAPACITY) {
        return false;
    }
    separator = strrchr(output, '/');
#if defined(_WIN32)
    {
        char *backslash = strrchr(output, '\\');

        if (backslash != NULL
            && (separator == NULL || backslash > separator)) {
            separator = backslash;
        }
    }
#endif
    if (separator == NULL) {
        return false;
    }
    *separator = '\0';
    return true;
}

static void abandon_storage(gdox_session_storage *storage)
{
#if defined(_WIN32)
    (void)CloseHandle((HANDLE)storage->lock_handle);
#else
    (void)close((int)storage->lock_handle);
#endif
    storage->lock_handle = -1;
    storage->active = false;
}

static bool create_directory_link(const char *target, const char *link)
{
#if defined(_WIN32)
#if !defined(SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2U
#endif
    if (CreateSymbolicLinkA(
            link,
            target,
            SYMBOLIC_LINK_FLAG_DIRECTORY
                | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
        )) {
        return true;
    }
    return GetLastError() == ERROR_INVALID_PARAMETER
        && CreateSymbolicLinkA(link, target, SYMBOLIC_LINK_FLAG_DIRECTORY);
#else
    return symlink(target, link) == 0;
#endif
}

static bool set_session_home(const char *value)
{
#if defined(_WIN32)
    return _putenv_s("GDOX_SESSION_HOME", value != NULL ? value : "") == 0;
#else
    return value != NULL
        ? setenv("GDOX_SESSION_HOME", value, 1) == 0
        : unsetenv("GDOX_SESSION_HOME") == 0;
#endif
}

#if defined(_WIN32)
static bool set_data_home(const char *value)
{
    return _putenv_s("GDOX_DATA_HOME", value != NULL ? value : "") == 0;
}

static char *saved_data_home(void)
{
    const char *value = getenv("GDOX_DATA_HOME");
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
#endif

static char *saved_session_home(void)
{
    const char *value = getenv("GDOX_SESSION_HOME");
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

static bool write_text(const char *path, const char *text)
{
#if defined(_WIN32)
    const size_t bytes = strlen(text);
    DWORD written = 0U;
    HANDLE file = CreateFileA(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    bool success;

    if (file == INVALID_HANDLE_VALUE
        || SetFilePointer(file, 0L, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER
        || !SetEndOfFile(file)) {
        if (file != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(file);
        }
        return false;
    }
    success = bytes <= MAXDWORD
        && WriteFile(file, text, (DWORD)bytes, &written, NULL)
        && written == (DWORD)bytes
        && FlushFileBuffers(file);
    return CloseHandle(file) != 0 && success;
#else
    FILE *file = fopen(path, "wb");
    const size_t bytes = strlen(text);
    bool success;

    if (file == NULL) {
        return false;
    }
    success = fwrite(text, 1U, bytes, file) == bytes;
    return fclose(file) == 0 && success;
#endif
}

static bool read_text(
    const char *path,
    char *output,
    size_t capacity
)
{
    FILE *file = fopen(path, "rb");
    size_t bytes;

    if (file == NULL || capacity == 0U) {
        return false;
    }
    bytes = fread(output, 1U, capacity - 1U, file);
    output[bytes] = '\0';
    return !ferror(file) && fclose(file) == 0;
}

#if !defined(_WIN32)
static bool inspect_private_directory(
    const char *path,
    struct stat *status
)
{
    const int directory = open(
        path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    const bool success = directory >= 0
        && fstat(directory, status) == 0;

    return directory >= 0 && close(directory) == 0 && success;
}
#endif

static void test_owned_session_lifecycle(void)
{
    char working[GDOX_SESSION_PATH_CAPACITY];
    char base[GDOX_SESSION_PATH_CAPACITY];
    char parent[GDOX_SESSION_PATH_CAPACITY];
    char nested[GDOX_SESSION_PATH_CAPACITY];
    char target[GDOX_SESSION_PATH_CAPACITY];
    char sibling[GDOX_SESSION_PATH_CAPACITY];
    char target_file[GDOX_SESSION_PATH_CAPACITY];
    char sibling_file[GDOX_SESSION_PATH_CAPACITY];
    char active_root[GDOX_SESSION_PATH_CAPACITY];
    char stale_root[GDOX_SESSION_PATH_CAPACITY];
    char malformed[GDOX_SESSION_PATH_CAPACITY];
    char missing_marker[GDOX_SESSION_PATH_CAPACITY];
    char unrelated[GDOX_SESSION_PATH_CAPACITY];
    char marker_path[GDOX_SESSION_PATH_CAPACITY];
    char lock_path[GDOX_SESSION_PATH_CAPACITY];
    char outside[GDOX_SESSION_PATH_CAPACITY];
    char outside_file[GDOX_SESSION_PATH_CAPACITY];
    char linked[GDOX_SESSION_PATH_CAPACITY];
    char marker[512];
    char *saved = saved_session_home();
    gdox_session_storage storage = {0};
    gdox_session_storage other = {0};
    gdox_error error;
    char owner_prefix[64];
    intptr_t first_lock;
    intptr_t second_lock;
    int written;
#if !defined(_WIN32)
    struct stat parent_status;
#endif

    GDOX_TEST_CHECK(gdox_test_getcwd(working, sizeof(working)) != NULL);
    written = snprintf(
        base,
        sizeof(base),
        "%s/gdox-session-storage-test-%d",
        working,
        gdox_test_getpid()
    );
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(base));
    (void)gdox_test_rmdir(base);
    GDOX_TEST_CHECK(gdox_test_mkdir(base) == 0);
    GDOX_TEST_CHECK(set_session_home(base));
    GDOX_TEST_CHECK(gdox_session_storage_recover(&error));
    GDOX_TEST_CHECK(gdox_session_storage_create(&storage, &error));
    GDOX_TEST_CHECK(storage.active);
    GDOX_TEST_CHECK(storage.lock_handle >= 0);
    GDOX_TEST_CHECK(strstr(storage.root, "gdox-session") != NULL);
    GDOX_TEST_CHECK(parent_path(parent, storage.root));
    GDOX_TEST_CHECK(join_path(
        marker_path, storage.root, GDOX_SESSION_OWNER_MARKER
    ));
    GDOX_TEST_CHECK(join_path(
        lock_path, storage.root, GDOX_SESSION_LOCK_FILE
    ));
    GDOX_TEST_CHECK(gdox_test_access(marker_path, 0) == 0);
    GDOX_TEST_CHECK(gdox_test_access(lock_path, 0) == 0);
    written = snprintf(
        owner_prefix,
        sizeof(owner_prefix),
        "session-%d-",
        gdox_test_getpid()
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(owner_prefix)
    );
    GDOX_TEST_CHECK(strstr(storage.root, owner_prefix) != NULL);
    GDOX_TEST_CHECK(snprintf(
        active_root, sizeof(active_root), "%s", storage.root
    ) >= 0);
    GDOX_TEST_CHECK(gdox_session_storage_recover(&error));
    GDOX_TEST_CHECK(gdox_test_access(active_root, 0) == 0);

    GDOX_TEST_CHECK(join_path(
        malformed, parent, "session-malformed-marker"
    ));
    GDOX_TEST_CHECK(join_path(
        missing_marker, parent, "session-missing-marker"
    ));
    GDOX_TEST_CHECK(join_path(
        unrelated, parent, "session-unrelated-file"
    ));
    GDOX_TEST_CHECK(gdox_test_mkdir(malformed) == 0);
    GDOX_TEST_CHECK(join_path(
        marker_path, malformed, GDOX_SESSION_OWNER_MARKER
    ));
    GDOX_TEST_CHECK(write_text(marker_path, "not a GDOX owner marker"));
    GDOX_TEST_CHECK(join_path(
        lock_path, malformed, GDOX_SESSION_LOCK_FILE
    ));
    GDOX_TEST_CHECK(write_text(lock_path, ""));
    GDOX_TEST_CHECK(gdox_test_mkdir(missing_marker) == 0);
    GDOX_TEST_CHECK(write_text(unrelated, "preserved"));
    GDOX_TEST_CHECK(join_path(outside, base, "outside"));
    GDOX_TEST_CHECK(join_path(outside_file, outside, "save.bin"));
    GDOX_TEST_CHECK(join_path(linked, parent, "session-reparse-entry"));
    GDOX_TEST_CHECK(gdox_test_mkdir(outside) == 0);
    GDOX_TEST_CHECK(write_text(outside_file, "preserved"));
    GDOX_TEST_CHECK(create_directory_link(outside, linked));
    GDOX_TEST_CHECK(gdox_session_storage_recover(&error));

    GDOX_TEST_CHECK(gdox_test_remove(marker_path) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(lock_path) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(malformed) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(missing_marker) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(unrelated) == 0);
#if defined(_WIN32)
    GDOX_TEST_CHECK(gdox_test_rmdir(linked) == 0);
#else
    GDOX_TEST_CHECK(gdox_test_remove(linked) == 0);
#endif

    GDOX_TEST_CHECK(gdox_session_storage_path(
        &storage, "nested", nested, &error
    ));
    GDOX_TEST_CHECK(gdox_test_mkdir(nested) == 0);
    GDOX_TEST_CHECK(gdox_session_storage_path(
        &storage, "nested/target", target, &error
    ));
    GDOX_TEST_CHECK(gdox_session_storage_path(
        &storage, "nested/sibling", sibling, &error
    ));
    GDOX_TEST_CHECK(gdox_test_mkdir(target) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(sibling) == 0);
    written = snprintf(
        target_file, sizeof(target_file), "%s/cache.bin", target
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(target_file)
    );
    written = snprintf(
        sibling_file, sizeof(sibling_file), "%s/save.bin", sibling
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(sibling_file)
    );
    GDOX_TEST_CHECK(write_text(target_file, "derived"));
    GDOX_TEST_CHECK(write_text(sibling_file, "preserved"));
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        storage.root, "nested/target", &error
    ));
    GDOX_TEST_CHECK(gdox_test_access(target, 0) != 0);
    GDOX_TEST_CHECK(gdox_test_access(sibling_file, 0) == 0);
    GDOX_TEST_CHECK(!gdox_session_storage_remove_relative(
        storage.root, "nested/../sibling", &error
    ));
    GDOX_TEST_CHECK(gdox_test_access(sibling_file, 0) == 0);
    GDOX_TEST_CHECK(join_path(linked, nested, "outside-link"));
    GDOX_TEST_CHECK(create_directory_link(outside, linked));

    GDOX_TEST_CHECK(join_path(
        marker_path, storage.root, GDOX_SESSION_OWNER_MARKER
    ));
    GDOX_TEST_CHECK(read_text(marker_path, marker, sizeof(marker)));
    GDOX_TEST_CHECK(write_text(marker_path, "corrupt marker"));
    GDOX_TEST_CHECK(!gdox_session_storage_cleanup(&storage, &error));
    GDOX_TEST_CHECK(storage.active);
    GDOX_TEST_CHECK(gdox_test_access(active_root, 0) == 0);
    GDOX_TEST_CHECK(write_text(marker_path, marker));

    GDOX_TEST_CHECK(gdox_session_storage_create(&other, &error));
    first_lock = storage.lock_handle;
    second_lock = other.lock_handle;
    storage.lock_handle = second_lock;
    other.lock_handle = first_lock;
    GDOX_TEST_CHECK(!gdox_session_storage_cleanup(&storage, &error));
    GDOX_TEST_CHECK(!gdox_session_storage_cleanup(&other, &error));
    storage.lock_handle = first_lock;
    other.lock_handle = second_lock;
    GDOX_TEST_CHECK(gdox_session_storage_cleanup(&other, &error));
    GDOX_TEST_CHECK(gdox_session_storage_cleanup(&storage, &error));
    GDOX_TEST_CHECK(!storage.active);
    GDOX_TEST_CHECK(gdox_test_access(active_root, 0) != 0);

    GDOX_TEST_CHECK(gdox_session_storage_create(&storage, &error));
    GDOX_TEST_CHECK(snprintf(
        stale_root, sizeof(stale_root), "%s", storage.root
    ) >= 0);
    GDOX_TEST_CHECK(strstr(stale_root, owner_prefix) != NULL);
    GDOX_TEST_CHECK(gdox_session_storage_path(
        &storage, "abandoned.bin", target_file, &error
    ));
    GDOX_TEST_CHECK(write_text(target_file, "derived"));
    abandon_storage(&storage);
    GDOX_TEST_CHECK(gdox_session_storage_recover(&error));
    GDOX_TEST_CHECK(gdox_test_access(stale_root, 0) != 0);
#if defined(_WIN32)
    GDOX_TEST_CHECK(gdox_test_access(parent, 0) != 0);
#else
    GDOX_TEST_CHECK(inspect_private_directory(parent, &parent_status));
    GDOX_TEST_CHECK(S_ISDIR(parent_status.st_mode));
    GDOX_TEST_CHECK(parent_status.st_uid == geteuid());
    GDOX_TEST_CHECK((parent_status.st_mode & 077U) == 0U);
    GDOX_TEST_CHECK(gdox_test_rmdir(parent) == 0);
#endif
    GDOX_TEST_CHECK(gdox_test_remove(outside_file) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(outside) == 0);
    GDOX_TEST_CHECK(set_session_home(saved));
    free(saved);
    GDOX_TEST_CHECK(gdox_test_rmdir(base) == 0);
}

#if !defined(_WIN32)
static int concurrent_session_worker(void)
{
    enum { iterations = 16U };
    unsigned int iteration;

    for (iteration = 0U; iteration < iterations; ++iteration) {
        gdox_session_storage storage = {0};
        gdox_error error;

        if (!gdox_session_storage_recover(&error)
            || !gdox_session_storage_create(&storage, &error)
            || !gdox_session_storage_cleanup(&storage, &error)) {
            return 1;
        }
    }
    return 0;
}

static void test_concurrent_session_creation(void)
{
    enum { worker_count = 4U };
    char working[GDOX_SESSION_PATH_CAPACITY];
    char base[GDOX_SESSION_PATH_CAPACITY];
    char parent[GDOX_SESSION_PATH_CAPACITY];
    char parent_name[64];
    char *saved = saved_session_home();
    pid_t workers[worker_count];
    size_t started = 0U;
    size_t index;
    bool success = true;
    gdox_error error;
    struct stat parent_status;
    int written;

    GDOX_TEST_CHECK(gdox_test_getcwd(working, sizeof(working)) != NULL);
    written = snprintf(
        base,
        sizeof(base),
        "%s/gdox-session-concurrency-%ld",
        working,
        (long)gdox_test_getpid()
    );
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(base));
    GDOX_TEST_CHECK(gdox_test_mkdir(base) == 0);
    GDOX_TEST_CHECK(set_session_home(base));
    for (index = 0U; index < worker_count; ++index) {
        const pid_t child = fork();

        if (child == 0) {
            _exit(concurrent_session_worker());
        }
        if (child < 0) {
            success = false;
            break;
        }
        workers[started++] = child;
    }
    for (index = 0U; index < started; ++index) {
        int status = 0;

        if (waitpid(workers[index], &status, 0) != workers[index]
            || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            success = false;
        }
    }
    success = started == worker_count && success;
    success = gdox_session_storage_recover(&error) && success;
    written = snprintf(
        parent_name,
        sizeof(parent_name),
        "gdox-session-%lu",
        (unsigned long)geteuid()
    );
    success = written >= 0
        && (size_t)written < sizeof(parent_name)
        && join_path(parent, base, parent_name)
        && success;
    success = inspect_private_directory(parent, &parent_status) && success;
    success = S_ISDIR(parent_status.st_mode) && success;
    success = parent_status.st_uid == geteuid() && success;
    success = (parent_status.st_mode & 077U) == 0U && success;
    success = gdox_test_rmdir(parent) == 0 && success;
    success = set_session_home(saved) && success;
    free(saved);
    success = gdox_test_rmdir(base) == 0 && success;
    GDOX_TEST_CHECK(success);
}
#endif

static void test_memory_session_policy(void)
{
    gdox_session_storage storage = {0};
    gdox_error error;

#if defined(__linux__)
    GDOX_TEST_CHECK(gdox_session_storage_recover_memory(&error));
    GDOX_TEST_CHECK(gdox_session_storage_create_memory(&storage, &error));
    GDOX_TEST_CHECK(storage.active);
    GDOX_TEST_CHECK(storage.memory_backed);
    GDOX_TEST_CHECK(strncmp(storage.root, "/dev/shm/", 9U) == 0);
    GDOX_TEST_CHECK(strstr(storage.root, "gdox-session") != NULL);
    GDOX_TEST_CHECK(gdox_session_storage_cleanup(&storage, &error));
#else
    GDOX_TEST_CHECK(!gdox_session_storage_create_memory(&storage, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
#endif
}

#if defined(_WIN32)
static void test_private_user_storage_policy(void)
{
    char working[GDOX_SESSION_PATH_CAPACITY];
    char base[GDOX_SESSION_PATH_CAPACITY];
    char private_path[GDOX_SESSION_PATH_CAPACITY];
    char broad_path[GDOX_SESSION_PATH_CAPACITY];
    char outside[GDOX_SESSION_PATH_CAPACITY];
    char linked[GDOX_SESSION_PATH_CAPACITY];
    char data_root[GDOX_SESSION_PATH_CAPACITY];
    char data_xemu[GDOX_SESSION_PATH_CAPACITY];
    char data_saves[GDOX_SESSION_PATH_CAPACITY];
    char vault[GDOX_STORAGE_PATH_CAPACITY];
    char *saved = saved_data_home();
    gdox_error error;

    GDOX_TEST_CHECK(gdox_test_getcwd(working, sizeof(working)) != NULL);
    GDOX_TEST_CHECK(snprintf(
        base,
        sizeof(base),
        "%s/gdox-private-storage-%ld",
        working,
        (long)gdox_test_getpid()
    ) > 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(base) == 0);
    GDOX_TEST_CHECK(join_path(private_path, base, "private"));
    GDOX_TEST_CHECK(gdox_storage_ensure_private_directory(
        private_path, &error
    ));
    GDOX_TEST_CHECK(gdox_storage_ensure_private_directory(
        private_path, &error
    ));

    GDOX_TEST_CHECK(join_path(broad_path, base, "broad"));
    GDOX_TEST_CHECK(gdox_storage_ensure_directory(broad_path, &error));
    GDOX_TEST_CHECK(!gdox_storage_ensure_private_directory(
        broad_path, &error
    ));
    GDOX_TEST_CHECK(gdox_test_access(broad_path, 0) == 0);

    GDOX_TEST_CHECK(join_path(outside, base, "outside"));
    GDOX_TEST_CHECK(join_path(linked, base, "linked"));
    GDOX_TEST_CHECK(gdox_test_mkdir(outside) == 0);
    GDOX_TEST_CHECK(create_directory_link(outside, linked));
    GDOX_TEST_CHECK(!gdox_storage_ensure_private_directory(linked, &error));
    GDOX_TEST_CHECK(gdox_test_access(outside, 0) == 0);

    GDOX_TEST_CHECK(join_path(data_root, base, "custom-data"));
    GDOX_TEST_CHECK(set_data_home(data_root));
    GDOX_TEST_CHECK(gdox_user_data_path("xemu/saves/v1", vault, &error));
    GDOX_TEST_CHECK(gdox_storage_ensure_directory(vault, &error));
    GDOX_TEST_CHECK(!gdox_storage_ensure_private_directory(vault, &error));
    GDOX_TEST_CHECK(gdox_test_access(vault, 0) == 0);
    GDOX_TEST_CHECK(join_path(data_xemu, data_root, "xemu"));
    GDOX_TEST_CHECK(join_path(data_saves, data_xemu, "saves"));

    GDOX_TEST_CHECK(set_data_home(saved));
    free(saved);
    GDOX_TEST_CHECK(gdox_test_rmdir(vault) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(data_saves) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(data_xemu) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(data_root) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(linked) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(outside) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(broad_path) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(private_path) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(base) == 0);
}
#endif

void gdox_test_session_storage(void)
{
    test_owned_session_lifecycle();
#if !defined(_WIN32)
    test_concurrent_session_creation();
#endif
    test_memory_session_policy();
#if defined(_WIN32)
    test_private_user_storage_policy();
#endif
}
