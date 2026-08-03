#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "app/xenia_content_migration.h"
#include "app/xenia_storage.h"
#include "platform/session_storage.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define gdox_test_access access
#define gdox_test_getcwd getcwd
#define gdox_test_getpid getpid
#define gdox_test_mkdir(path) mkdir(path, 0700)
#define gdox_test_remove unlink
#define gdox_test_rmdir rmdir
#endif

static bool set_environment(const char *name, const char *value)
{
#if defined(_WIN32)
    return _putenv_s(name, value != NULL ? value : "") == 0;
#else
    return value != NULL
        ? setenv(name, value, 1) == 0
        : unsetenv(name) == 0;
#endif
}

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

static bool join_path(
    char *output,
    size_t capacity,
    const char *parent,
    const char *relative
)
{
    const size_t parent_bytes = strlen(parent);
    const size_t relative_bytes = strlen(relative);
    const bool needs_separator = parent_bytes != 0U
        && parent[parent_bytes - 1U] != '/'
        && parent[parent_bytes - 1U] != '\\';
    size_t cursor = parent_bytes;

    if (parent_bytes + (needs_separator ? 1U : 0U)
        + relative_bytes + 1U > capacity) {
        return false;
    }
    memmove(output, parent, parent_bytes);
    if (needs_separator) {
        output[cursor++] = '/';
    }
    memcpy(output + cursor, relative, relative_bytes + 1U);
    return true;
}

static bool ensure_directory(const char *path)
{
    gdox_error error;

    return gdox_storage_ensure_directory(path, &error);
}

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

static bool make_file(
    const char *root,
    const char *relative,
    const char *text,
    char output[GDOX_SESSION_PATH_CAPACITY]
)
{
    char directory[GDOX_SESSION_PATH_CAPACITY];
    char *separator;
    gdox_error error;

    if (!join_path(
            output,
            GDOX_SESSION_PATH_CAPACITY,
            root,
            relative
        )) {
        return false;
    }
    memcpy(directory, output, strlen(output) + 1U);
    separator = strrchr(directory, '/');
#if defined(_WIN32)
    {
        char *backslash = strrchr(directory, '\\');

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
    if (!gdox_storage_ensure_directory(directory, &error)) {
        (void)fprintf(
            stderr,
            "could not create fixture directory %s: %s\n",
            directory,
            error.message
        );
        return false;
    }
    return write_text(output, text);
}

static bool rejected_and_preserved(
    const char *content_root,
    const char *rejected_path,
    const char *evidence_path
)
{
    gdox_error error;

    return !gdox_xenia_content_migrate(content_root, &error)
        && error.code == GDOX_ERROR_INVALID_SOURCE
        && strstr(error.message, rejected_path) != NULL
        && gdox_test_access(evidence_path, 0) == 0;
}

static void test_xenia_storage_lifecycle(void)
{
    char root[GDOX_SESSION_PATH_CAPACITY];
    char data[GDOX_SESSION_PATH_CAPACITY];
    char sessions[GDOX_SESSION_PATH_CAPACITY];
    char revision_root[GDOX_SESSION_PATH_CAPACITY];
    char content_root[GDOX_SESSION_PATH_CAPACITY];
    char path[GDOX_SESSION_PATH_CAPACITY];
    char save_path[GDOX_SESSION_PATH_CAPACITY];
    char profile_path[GDOX_SESSION_PATH_CAPACITY];
    char xbox_save_path[GDOX_SESSION_PATH_CAPACITY];
    char header_save_path[GDOX_SESSION_PATH_CAPACITY];
    char unknown_path[GDOX_SESSION_PATH_CAPACITY];
    char rejected_path[GDOX_SESSION_PATH_CAPACITY];
    char legacy_proton[GDOX_SESSION_PATH_CAPACITY];
#if defined(__linux__) || defined(_WIN32)
    char session_root[GDOX_SESSION_PATH_CAPACITY];
#endif
#if !defined(_WIN32)
    char external_proton[GDOX_SESSION_PATH_CAPACITY];
    char external_evidence[GDOX_SESSION_PATH_CAPACITY];
#endif
    char *saved_data = save_environment("GDOX_DATA_HOME");
    char *saved_sessions = save_environment("GDOX_SESSION_HOME");
    const gdox_xenia_launch_policy *catalog_policy =
        gdox_xenia_default_policy();
    const gdox_xenia_runtime *runtime = gdox_xenia_runtime_at(0U);
    gdox_xenia_runtime isolated_runtime;
    gdox_xenia_launch_policy isolated_policy;
    gdox_xenia_storage storage = {0};
    gdox_error error;
    size_t root_bytes;
    int written;

    GDOX_TEST_CHECK(catalog_policy != NULL && runtime != NULL);
    isolated_runtime = *runtime;
    isolated_runtime.supports_storage_isolation = true;
    isolated_policy = *catalog_policy;
    isolated_policy.runtime = &isolated_runtime;
    GDOX_TEST_CHECK(gdox_test_getcwd(root, sizeof(root)) != NULL);
    root_bytes = strlen(root);
    written = snprintf(
        root + root_bytes,
        sizeof(root) - root_bytes,
        "/gdox-xenia-storage-test-%d",
        gdox_test_getpid()
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(root) - root_bytes
    );
    (void)gdox_test_rmdir(root);
    GDOX_TEST_CHECK(gdox_test_mkdir(root) == 0);
    GDOX_TEST_CHECK(join_path(data, sizeof(data), root, "data"));
    GDOX_TEST_CHECK(join_path(sessions, sizeof(sessions), root, "sessions"));
    GDOX_TEST_CHECK(ensure_directory(data));
    GDOX_TEST_CHECK(ensure_directory(sessions));
    GDOX_TEST_CHECK(set_environment("GDOX_DATA_HOME", data));
    GDOX_TEST_CHECK(set_environment("GDOX_SESSION_HOME", sessions));

    GDOX_TEST_CHECK(join_path(
        revision_root, sizeof(revision_root), data, "xenia/storage"
    ));
    GDOX_TEST_CHECK(join_path(
        revision_root, sizeof(revision_root), revision_root,
        runtime->revision
    ));
    GDOX_TEST_CHECK(make_file(
        revision_root, "cache_host/shader.bin", "derived", path
    ));
    GDOX_TEST_CHECK(make_file(
        revision_root, "modules/module/flags.bin", "derived", path
    ));
    GDOX_TEST_CHECK(make_file(
        revision_root, "scratch/title.bin", "derived", path
    ));
    GDOX_TEST_CHECK(make_file(
        revision_root, "preserved/settings.bin", "preserved", path
    ));
    GDOX_TEST_CHECK(make_file(
        data, "xenia/proton/pfx/drive_c/derived.bin", "derived",
        legacy_proton
    ));
    GDOX_TEST_CHECK(make_file(
        data, "xenia/logs/xenia.log", "derived", path
    ));

    GDOX_TEST_CHECK(join_path(
        content_root, sizeof(content_root), data, "xenia/content"
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E8/00000001/save.bin",
        "save",
        save_path
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/FFFE07D1/00010000/profile.bin",
        "profile",
        profile_path
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E8/00060000/xbox-save.bin",
        "save",
        xbox_save_path
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E8/00000002/installed.bin",
        "derived",
        path
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E8/Headers/00000001/save-header.bin",
        "save",
        header_save_path
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E8/Headers/00000002/content-header.bin",
        "derived",
        path
    ));
    GDOX_TEST_CHECK(make_file(
        content_root,
        "4D5307E8/00000003/legacy-derived.bin",
        "derived",
        path
    ));
    GDOX_TEST_CHECK(gdox_xenia_storage_recover(&error));
    GDOX_TEST_CHECK(join_path(
        path, sizeof(path), data, "xenia/storage"
    ));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);
    GDOX_TEST_CHECK(join_path(
        path, sizeof(path), data, "xenia/proton"
    ));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);
    GDOX_TEST_CHECK(gdox_test_access(legacy_proton, 0) != 0);
    GDOX_TEST_CHECK(join_path(
        path, sizeof(path), data, "xenia/logs"
    ));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);
#if !defined(_WIN32)
    GDOX_TEST_CHECK(join_path(
        external_proton, sizeof(external_proton), root, "external-proton"
    ));
    GDOX_TEST_CHECK(make_file(
        external_proton, "evidence.bin", "preserved", external_evidence
    ));
    GDOX_TEST_CHECK(join_path(
        path, sizeof(path), data, "xenia/proton"
    ));
    GDOX_TEST_CHECK(symlink(external_proton, path) == 0);
    GDOX_TEST_CHECK(gdox_xenia_storage_recover(&error));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);
    GDOX_TEST_CHECK(gdox_test_remove(external_evidence) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(external_proton) == 0);
#endif
    GDOX_TEST_CHECK(gdox_test_access(save_path, 0) == 0);
    GDOX_TEST_CHECK(gdox_test_access(profile_path, 0) == 0);
    GDOX_TEST_CHECK(gdox_test_access(xbox_save_path, 0) == 0);
    GDOX_TEST_CHECK(gdox_test_access(header_save_path, 0) == 0);
    GDOX_TEST_CHECK(join_path(
        path,
        sizeof(path),
        content_root,
        "0123456789ABCDEF/4D5307E8/00000002"
    ));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);
    GDOX_TEST_CHECK(join_path(
        path,
        sizeof(path),
        content_root,
        "0123456789ABCDEF/4D5307E8/Headers/00000002"
    ));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);
    GDOX_TEST_CHECK(join_path(
        path, sizeof(path), content_root, "4D5307E8/00000003"
    ));
    GDOX_TEST_CHECK(gdox_test_access(path, 0) != 0);

    GDOX_TEST_CHECK(make_file(
        content_root,
        "unrecognized/00000002/unknown.bin",
        "preserved",
        unknown_path
    ));
    GDOX_TEST_CHECK(join_path(
        rejected_path, sizeof(rejected_path), content_root, "unrecognized"
    ));
    GDOX_TEST_CHECK(rejected_and_preserved(
        content_root, rejected_path, unknown_path
    ));
    GDOX_TEST_CHECK(!gdox_xenia_storage_open(
        &isolated_policy, &storage, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(gdox_test_access(unknown_path, 0) == 0);
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        content_root, "unrecognized", &error
    ));

    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/not-a-title/unknown.bin",
        "preserved",
        unknown_path
    ));
    GDOX_TEST_CHECK(join_path(
        rejected_path,
        sizeof(rejected_path),
        content_root,
        "0123456789ABCDEF/not-a-title"
    ));
    GDOX_TEST_CHECK(rejected_and_preserved(
        content_root, rejected_path, unknown_path
    ));
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        content_root, "0123456789ABCDEF/not-a-title", &error
    ));

    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E8/not-a-content-type/unknown.bin",
        "preserved",
        unknown_path
    ));
    GDOX_TEST_CHECK(join_path(
        rejected_path,
        sizeof(rejected_path),
        content_root,
        "0123456789ABCDEF/4D5307E8/not-a-content-type"
    ));
    GDOX_TEST_CHECK(rejected_and_preserved(
        content_root, rejected_path, unknown_path
    ));
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        content_root,
        "0123456789ABCDEF/4D5307E8/not-a-content-type",
        &error
    ));

    GDOX_TEST_CHECK(make_file(
        content_root,
        "0123456789ABCDEF/4D5307E9/00000001",
        "malformed-save-layout",
        unknown_path
    ));
    GDOX_TEST_CHECK(join_path(
        rejected_path,
        sizeof(rejected_path),
        content_root,
        "0123456789ABCDEF/4D5307E9/00000001"
    ));
    GDOX_TEST_CHECK(rejected_and_preserved(
        content_root, rejected_path, unknown_path
    ));
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        content_root, "0123456789ABCDEF/4D5307E9", &error
    ));

#if defined(__linux__) || defined(_WIN32)
    GDOX_TEST_CHECK(gdox_xenia_storage_open(
        catalog_policy, &storage, &error
    ));
    GDOX_TEST_CHECK(storage.session.active);
#if defined(__linux__)
    GDOX_TEST_CHECK(storage.session.memory_backed);
#else
    GDOX_TEST_CHECK(!storage.session.memory_backed);
#endif
    GDOX_TEST_CHECK(join_path(
        session_root, sizeof(session_root), storage.session.root, ""
    ));
    GDOX_TEST_CHECK(gdox_xenia_storage_close(&storage, &error));
    GDOX_TEST_CHECK(gdox_test_access(session_root, 0) != 0);

    GDOX_TEST_CHECK(gdox_xenia_storage_open(
        &isolated_policy, &storage, &error
    ));
    GDOX_TEST_CHECK(storage.session.active);
#if defined(__linux__)
    GDOX_TEST_CHECK(storage.session.memory_backed);
#else
    GDOX_TEST_CHECK(!storage.session.memory_backed);
#endif
    GDOX_TEST_CHECK(join_path(
        path, sizeof(path), storage.cache, "runtime.bin"
    ));
    GDOX_TEST_CHECK(write_text(path, "derived"));
    GDOX_TEST_CHECK(join_path(
        session_root, sizeof(session_root), storage.session.root, ""
    ));
    GDOX_TEST_CHECK(gdox_xenia_storage_close(&storage, &error));
    GDOX_TEST_CHECK(gdox_test_access(session_root, 0) != 0);
#else
    GDOX_TEST_CHECK(!gdox_xenia_storage_open(
        catalog_policy, &storage, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    GDOX_TEST_CHECK(!gdox_xenia_storage_open(
        &isolated_policy, &storage, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
#endif
    GDOX_TEST_CHECK(gdox_test_access(save_path, 0) == 0);

    GDOX_TEST_CHECK(set_environment("GDOX_DATA_HOME", saved_data));
    GDOX_TEST_CHECK(set_environment("GDOX_SESSION_HOME", saved_sessions));
    free(saved_data);
    free(saved_sessions);
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        root, "data", &error
    ));
    GDOX_TEST_CHECK(gdox_session_storage_remove_relative(
        root, "sessions", &error
    ));
    GDOX_TEST_CHECK(gdox_test_rmdir(root) == 0);
}

void gdox_test_xenia_storage(void)
{
    test_xenia_storage_lifecycle();
}
