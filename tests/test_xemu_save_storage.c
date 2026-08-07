#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "app/xemu_save_storage.h"
#include "core/xemu_save_migration.h"
#include "platform/xemu_helper_process.h"
#include "platform/xemu_save_migration.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_getcwd _getcwd
#define gdox_test_getpid _getpid
#define gdox_test_mkdir(path) _mkdir(path)
#define gdox_test_remove _unlink
#define gdox_test_rmdir _rmdir
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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
        ? setenv(name, value, 1) == 0 : unsetenv(name) == 0;
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

static void restore_environment(const char *name, char *saved)
{
    (void)set_environment(name, saved);
    free(saved);
}

static bool write_text_mode(
    const char *path,
    const char *text,
    const char *mode
)
{
    FILE *file = fopen(path, mode);
    const size_t bytes = strlen(text);
    bool success;

    if (file == NULL) {
        return false;
    }
    success = fwrite(text, 1U, bytes, file) == bytes;
    return fclose(file) == 0 && success;
}

static bool write_text(const char *path, const char *text)
{
    return write_text_mode(path, text, "wb");
}

static bool create_text(const char *path, const char *text)
{
    return write_text_mode(path, text, "wbx");
}

#if !defined(_WIN32)
static bool create_private_text(const char *path, const char *text)
{
    const size_t bytes = strlen(text);
    size_t offset = 0U;
    const int file = open(
        path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600
    );
    bool success = file >= 0;

    while (success && offset < bytes) {
        const ssize_t written = write(file, text + offset, bytes - offset);

        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            success = false;
        }
    }
    return file >= 0 && close(file) == 0 && success;
}
#endif

static void test_result_parser(void)
{
    static const char migration[] =
        "{\"schema\":3,\"operation\":\"xbox-hdd-save-migration-removal\","
        "\"source_writes\":false,\"source_bytes\":7,"
        "\"source_identity_sha256\":"
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"source_sha256\":"
        "\"2222222222222222222222222222222222222222222222222222222222222222\","
        "\"source_hashed\":true,\"source_projection_complete\":true,"
        "\"clean_bytes\":14,\"clean_sha256\":"
        "\"3333333333333333333333333333333333333333333333333333333333333333\","
        "\"receipt_reused\":false,\"source_removal_safe\":true,"
        "\"source_removed\":true,\"vault_version\":2,\"generation\":7,"
        "\"scope\":\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\","
        "\"format\":\"logical-files-v2\","
        "\"entries\":3,\"logical_bytes\":42,\"tree_sha256\":"
        "\"1111111111111111111111111111111111111111111111111111111111111111\","
        "\"policy_revision\":1,\"policy_sha256\":"
        "\"4444444444444444444444444444444444444444444444444444444444444444\","
        "\"udata_entries\":2,\"udata_logical_bytes\":30,"
        "\"tdata_entries\":1,\"tdata_logical_bytes\":12,"
        "\"hdd_config_format\":\"hdd-config-v1\","
        "\"hdd_config_bytes\":524288,\"hdd_config_sha256\":"
        "\"5555555555555555555555555555555555555555555555555555555555555555\","
        "\"unclassified_tdata_entries\":0,"
        "\"unclassified_tdata_bytes\":0,\"unclassified_tdata_sha256\":"
        "\"6666666666666666666666666666666666666666666666666666666666666666\","
        "\"vault_reopened\":true,\"anonymous_cow\":true,"
        "\"clean_backing\":true,\"reprojected\":true,"
        "\"roundtrip_verified\":true}";
    static const char validation[] =
        "{\"schema\":3,\"operation\":\"xbox-save-vault-validation\","
        "\"vault_version\":2,\"generation\":7,"
        "\"scope\":\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\","
        "\"format\":\"logical-files-v2\","
        "\"entries\":3,\"logical_bytes\":42,\"tree_sha256\":"
        "\"1111111111111111111111111111111111111111111111111111111111111111\","
        "\"policy_revision\":1,\"policy_sha256\":"
        "\"4444444444444444444444444444444444444444444444444444444444444444\","
        "\"udata_entries\":2,\"udata_logical_bytes\":30,"
        "\"tdata_entries\":1,\"tdata_logical_bytes\":12,"
        "\"hdd_config_format\":\"hdd-config-v1\","
        "\"hdd_config_bytes\":524288,\"hdd_config_sha256\":"
        "\"5555555555555555555555555555555555555555555555555555555555555555\","
        "\"unclassified_tdata_entries\":0,"
        "\"unclassified_tdata_bytes\":0,\"unclassified_tdata_sha256\":"
        "\"6666666666666666666666666666666666666666666666666666666666666666\","
        "\"vault_reopened\":true,\"anonymous_cow\":true,"
        "\"clean_backing\":true,\"reprojected\":true,"
        "\"roundtrip_verified\":true}\r\n";
    gdox_xemu_save_migration_proof migrated;
    gdox_xemu_save_vault_proof validated;
    gdox_error error;
    char malformed[sizeof(migration) + 16U];
    char nonremoval[sizeof(migration)];
    char *removal_suffix;

    GDOX_TEST_CHECK(gdox_xemu_save_migration_result_parse(
        migration,
        strlen(migration),
        7U,
        &migrated,
        &error
    ));
    GDOX_TEST_CHECK(migrated.source_removed);
    GDOX_TEST_CHECK(migrated.source_hashed);
    GDOX_TEST_CHECK(migrated.vault.entries == 3U);
    GDOX_TEST_CHECK(migrated.vault.logical_bytes == 42U);
    GDOX_TEST_CHECK(migrated.vault.tree_sha256[0] == 0x11U);
    GDOX_TEST_CHECK(migrated.vault.policy_sha256[0] == 0x44U);
    GDOX_TEST_CHECK(migrated.vault.hdd_config_sha256[0] == 0x55U);
    GDOX_TEST_CHECK(gdox_xemu_save_validation_result_parse(
        validation,
        strlen(validation),
        &validated,
        &error
    ));
    GDOX_TEST_CHECK(gdox_xemu_save_vault_proof_equal(
        &migrated.vault, &validated
    ));
    GDOX_TEST_CHECK(!gdox_xemu_save_migration_result_parse(
        migration,
        strlen(migration),
        8U,
        &migrated,
        &error
    ));
    (void)snprintf(malformed, sizeof(malformed), "%s ", migration);
    GDOX_TEST_CHECK(!gdox_xemu_save_migration_result_parse(
        malformed,
        strlen(malformed),
        7U,
        &migrated,
        &error
    ));
    memcpy(nonremoval, migration, sizeof(migration));
    removal_suffix = strstr(nonremoval, "-removal");
    GDOX_TEST_CHECK(removal_suffix != NULL);
    memmove(
        removal_suffix,
        removal_suffix + strlen("-removal"),
        strlen(removal_suffix + strlen("-removal")) + 1U
    );
    GDOX_TEST_CHECK(!gdox_xemu_save_migration_result_parse(
        nonremoval,
        strlen(nonremoval),
        7U,
        &migrated,
        &error
    ));
    GDOX_TEST_CHECK(gdox_xemu_save_migration_timeout_ms(0U) == 600000U);
    GDOX_TEST_CHECK(
        gdox_xemu_save_migration_timeout_ms(UINT64_C(16) * 1024U * 1024U)
        == 601000U
    );
    GDOX_TEST_CHECK(
        gdox_xemu_save_migration_timeout_ms(
            UINT64_C(16) * 1024U * 1024U + 1U
        ) == 602000U
    );
    GDOX_TEST_CHECK(
        gdox_xemu_save_migration_timeout_ms(UINT64_MAX) == 3600000U
    );
}

static void test_managed_migration(void)
{
    char working[4096];
    char root[4096];
    char data[4096];
    char managed[4096];
    char legacy[4096];
    char clean[4096];
    char custom[4096];
    char vault[4096];
    char marker[4096];
    char vault_generation[4096];
    char expected_bytes[32];
    char *saved_data_home = save_environment("GDOX_DATA_HOME");
    char *saved_save_mode = save_environment("GDOX_TEST_XEMU_SAVE_MODE");
    char *saved_quarantine = save_environment("GDOX_TEST_XEMU_QUARANTINE");
    gdox_xemu_helper_result helper;
    gdox_xemu_legacy_migration_outcome outcome;
    gdox_error error;
    uint64_t source_bytes = 0U;
    int written;

    GDOX_TEST_CHECK(gdox_test_getcwd(working, sizeof(working)) != NULL);
    written = snprintf(
        root,
        sizeof(root),
        "%s/gdox xemu migration %d %lld",
        working,
        gdox_test_getpid(),
        (long long)time(NULL)
    );
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(root));
    written = snprintf(data, sizeof(data), "%s/data", root);
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(data));
    written = snprintf(managed, sizeof(managed), "%s/xemu", data);
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(managed));
    written = snprintf(
        legacy, sizeof(legacy), "%s/xbox_hdd.qcow2", managed
    );
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(legacy));
    written = snprintf(clean, sizeof(clean), "%s/clean hdd.qcow2", root);
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(clean));
    written = snprintf(custom, sizeof(custom), "%s/custom.qcow2", root);
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(custom));
    GDOX_TEST_CHECK(gdox_test_mkdir(root) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(data) == 0);
    GDOX_TEST_CHECK(gdox_test_mkdir(managed) == 0);
    GDOX_TEST_CHECK(set_environment("GDOX_DATA_HOME", data));
    GDOX_TEST_CHECK(set_environment("GDOX_TEST_XEMU_SAVE_MODE", NULL));
    GDOX_TEST_CHECK(create_text(clean, "verified-clean"));
    GDOX_TEST_CHECK(create_text(custom, "custom-preserved"));

    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.legacy_found);
    GDOX_TEST_CHECK(gdox_xemu_save_vault_prepare(vault, &error));
#if !defined(_WIN32)
    GDOX_TEST_CHECK(chmod(vault, 0755) == 0);
    GDOX_TEST_CHECK(!gdox_xemu_save_vault_prepare(vault, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    GDOX_TEST_CHECK(chmod(vault, 0700) == 0);
#endif

    GDOX_TEST_CHECK(create_text(legacy, "verified-clean"));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(outcome.legacy_found);
    GDOX_TEST_CHECK(outcome.source_removed);
    GDOX_TEST_CHECK(!outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(!outcome.retained_due_to_unclassified);
    GDOX_TEST_CHECK(!outcome.retained_due_to_save_conflict);

    written = snprintf(marker, sizeof(marker), "%s/current", vault);
    GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(marker));
    GDOX_TEST_CHECK(write_text(marker, "existing-generation"));
    GDOX_TEST_CHECK(set_environment("GDOX_TEST_XEMU_SAVE_MODE", NULL));
    GDOX_TEST_CHECK(create_text(legacy, "dirty-saves"));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd(
        gdox_test_program_path, clean, &error
    ));

    GDOX_TEST_CHECK(create_text(legacy, "unknown-tdata"));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "preserve-unclassified"
    ));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(outcome.retained_due_to_unclassified);
    GDOX_TEST_CHECK(!outcome.source_removed);
    GDOX_TEST_CHECK(!outcome.receipt_reused);
    GDOX_TEST_CHECK(outcome.unclassified_tdata_entries == 1U);
    GDOX_TEST_CHECK(outcome.unclassified_tdata_bytes == 5U);
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "receipt"
    ));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(outcome.retained_due_to_unclassified);
    GDOX_TEST_CHECK(outcome.receipt_reused);
    GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);

    GDOX_TEST_CHECK(create_text(legacy, "save-conflict"));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "preserve-conflict"
    ));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.source_removed);
    GDOX_TEST_CHECK(!outcome.retained_due_to_unclassified);
    GDOX_TEST_CHECK(outcome.retained_due_to_save_conflict);
    GDOX_TEST_CHECK(outcome.unclassified_tdata_entries == 0U);
    GDOX_TEST_CHECK(outcome.unclassified_tdata_bytes == 0U);
    GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);

#if !defined(_WIN32)
    {
        char pending[4096];
        char ignored[4096];
        char ambiguous[4096];

        written = snprintf(
            ignored,
            sizeof(ignored),
            "%s/.gdox-xbox-hdd-removal.not-a-proof",
            managed
        );
        GDOX_TEST_CHECK(
            written >= 0 && (size_t)written < sizeof(ignored)
        );
        GDOX_TEST_CHECK(create_private_text(ignored, "unrelated"));
        GDOX_TEST_CHECK(set_environment(
            "GDOX_TEST_XEMU_SAVE_MODE", "recover-quarantine"
        ));
        GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
            gdox_test_program_path, clean, &outcome, &error
        ));
        GDOX_TEST_CHECK(!outcome.legacy_found);
        GDOX_TEST_CHECK(gdox_test_remove(ignored) == 0);

        written = snprintf(
            pending,
            sizeof(pending),
            "%s/.gdox-xbox-hdd-removal.00000001.00000002",
            managed
        );
        GDOX_TEST_CHECK(
            written >= 0 && (size_t)written < sizeof(pending)
        );
        GDOX_TEST_CHECK(create_private_text(pending, "interrupted-removal"));
        GDOX_TEST_CHECK(set_environment(
            "GDOX_TEST_XEMU_QUARANTINE", pending
        ));
        GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
            gdox_test_program_path, clean, &outcome, &error
        ));
        GDOX_TEST_CHECK(outcome.legacy_found);
        GDOX_TEST_CHECK(outcome.source_removed);
        GDOX_TEST_CHECK(create_private_text(pending, "first-pending-removal"));
        written = snprintf(
            ambiguous,
            sizeof(ambiguous),
            "%s/.gdox-xbox-hdd-removal.00000003.00000004",
            managed
        );
        GDOX_TEST_CHECK(
            written >= 0 && (size_t)written < sizeof(ambiguous)
        );
        GDOX_TEST_CHECK(create_private_text(ambiguous, "second-pending-removal"));
        GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd(
            gdox_test_program_path, clean, &error
        ));
        GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
        GDOX_TEST_CHECK(gdox_test_remove(pending) == 0);
        GDOX_TEST_CHECK(gdox_test_remove(ambiguous) == 0);
        GDOX_TEST_CHECK(set_environment(
            "GDOX_TEST_XEMU_QUARANTINE", NULL
        ));
    }
#endif

    GDOX_TEST_CHECK(create_text(legacy, "mismatched-saves"));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "mismatch"
    ));
    GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);

    GDOX_TEST_CHECK(create_text(legacy, "source-changes"));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "source-write"
    ));
    GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);

    GDOX_TEST_CHECK(create_text(legacy, "operation-fails"));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "nonzero"
    ));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "nonzero-source-write"
    ));
    GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);
    GDOX_TEST_CHECK(create_text(legacy, "operation-fails"));
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "nonzero"
    ));
    written = snprintf(
        vault_generation,
        sizeof(vault_generation),
        "%s/original-xbox-udata-0.gdox",
        vault
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(vault_generation)
    );
    GDOX_TEST_CHECK(create_text(vault_generation, "invalid-vault"));
    GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "nonzero-valid-vault"
    ));
    GDOX_TEST_CHECK(gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(outcome.retained_due_to_rejected_migration);
    GDOX_TEST_CHECK(gdox_test_remove(vault_generation) == 0);
    GDOX_TEST_CHECK(set_environment(
        "GDOX_TEST_XEMU_SAVE_MODE", "malformed"
    ));
    GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd_with_outcome(
        gdox_test_program_path, clean, &outcome, &error
    ));
    GDOX_TEST_CHECK(!outcome.retained_due_to_rejected_migration);
    source_bytes = sizeof("operation-fails") - 1U;
    written = snprintf(
        expected_bytes,
        sizeof(expected_bytes),
        "%llu",
        (unsigned long long)source_bytes
    );
    GDOX_TEST_CHECK(
        written >= 0 && (size_t)written < sizeof(expected_bytes)
    );
    {
        const char *const arguments[] = {
            "--gdox-migrate-hdd", legacy,
            "--gdox-remove-migrated-source",
            "--gdox-clean-hdd", clean,
            "--gdox-save-vault", vault,
            "--gdox-expected-source-bytes", expected_bytes,
            NULL,
        };

        GDOX_TEST_CHECK(set_environment(
            "GDOX_TEST_XEMU_SAVE_MODE", "hang"
        ));
        GDOX_TEST_CHECK(gdox_xemu_helper_run(
            gdox_test_program_path, arguments, 20U, &helper, &error
        ));
        GDOX_TEST_CHECK(helper.timed_out);
    }

#if !defined(_WIN32)
    {
        char target[4096];

        GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);
        written = snprintf(
            target, sizeof(target), "%s/symlink-target.qcow2", root
        );
        GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(target));
        GDOX_TEST_CHECK(create_text(target, "custom-preserved"));
        GDOX_TEST_CHECK(symlink(target, legacy) == 0);
        GDOX_TEST_CHECK(!gdox_xemu_migrate_legacy_managed_hdd(
            gdox_test_program_path, clean, &error
        ));
        GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);
        GDOX_TEST_CHECK(gdox_test_remove(target) == 0);
    }
#else
    GDOX_TEST_CHECK(gdox_test_remove(legacy) == 0);
#endif

    GDOX_TEST_CHECK(set_environment("GDOX_TEST_XEMU_SAVE_MODE", NULL));
    GDOX_TEST_CHECK(gdox_test_remove(marker) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(custom) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(clean) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(vault) == 0);
    {
        char saves[4096];

        written = snprintf(saves, sizeof(saves), "%s/saves", managed);
        GDOX_TEST_CHECK(written >= 0 && (size_t)written < sizeof(saves));
        GDOX_TEST_CHECK(gdox_test_rmdir(saves) == 0);
    }
    GDOX_TEST_CHECK(gdox_test_rmdir(managed) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(data) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(root) == 0);
    restore_environment("GDOX_TEST_XEMU_SAVE_MODE", saved_save_mode);
    restore_environment("GDOX_TEST_XEMU_QUARANTINE", saved_quarantine);
    restore_environment("GDOX_DATA_HOME", saved_data_home);
}

void gdox_test_xemu_save_storage(void)
{
    test_result_parser();
    test_managed_migration();
}
