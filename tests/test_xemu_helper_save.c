#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_xemu_helper.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

static bool absolute_path(const char *path)
{
#if defined(_WIN32)
    const size_t bytes = path != NULL ? strlen(path) : 0U;

    return (bytes >= 3U && path[1] == ':'
            && (path[2] == '\\' || path[2] == '/'))
        || (bytes >= 2U && path[0] == '\\' && path[1] == '\\');
#else
    return path != NULL && path[0] == '/';
#endif
}

int gdox_test_xemu_save(int argc, char **argv)
{
    static const char validation_format[] =
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
        "\"unclassified_tdata_entries\":%u,"
        "\"unclassified_tdata_bytes\":%u,\"unclassified_tdata_sha256\":"
        "\"6666666666666666666666666666666666666666666666666666666666666666\","
        "\"vault_reopened\":true,\"anonymous_cow\":true,"
        "\"clean_backing\":true,\"reprojected\":true,"
        "\"roundtrip_verified\":true}";
    static const char mismatched_validation[] =
        "{\"schema\":3,\"operation\":\"xbox-save-vault-validation\","
        "\"vault_version\":2,\"generation\":7,"
        "\"scope\":\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\","
        "\"format\":\"logical-files-v2\","
        "\"entries\":4,\"logical_bytes\":42,\"tree_sha256\":"
        "\"2222222222222222222222222222222222222222222222222222222222222222\","
        "\"policy_revision\":1,\"policy_sha256\":"
        "\"4444444444444444444444444444444444444444444444444444444444444444\","
        "\"udata_entries\":2,\"udata_logical_bytes\":30,"
        "\"tdata_entries\":1,\"tdata_logical_bytes\":12,"
        "\"hdd_config_format\":\"hdd-config-v1\","
        "\"hdd_config_bytes\":524288,\"hdd_config_sha256\":"
        "\"5555555555555555555555555555555555555555555555555555555555555555\","
        "\"unclassified_tdata_entries\":1,"
        "\"unclassified_tdata_bytes\":5,\"unclassified_tdata_sha256\":"
        "\"6666666666666666666666666666666666666666666666666666666666666666\","
        "\"vault_reopened\":true,\"anonymous_cow\":true,"
        "\"clean_backing\":true,\"reprojected\":true,"
        "\"roundtrip_verified\":true}";
    const char *mode = getenv("GDOX_TEST_XEMU_SAVE_MODE");
    const bool migration = strcmp(argv[1], "--gdox-migrate-hdd") == 0;
    const char *source = migration && argc > 2 ? argv[2] : NULL;
    const bool receipt = mode != NULL && strcmp(mode, "receipt") == 0;
    const bool source_write =
        mode != NULL && strcmp(mode, "source-write") == 0;
    const bool conflict =
        mode != NULL && strcmp(mode, "preserve-conflict") == 0;
    const bool recover =
        mode != NULL && strcmp(mode, "recover-quarantine") == 0;
    const bool unclassified = receipt || source_write
        || (mode != NULL
            && (strcmp(mode, "mismatch") == 0
                || strcmp(mode, "preserve-unclassified") == 0));
    const bool preserve = conflict || unclassified;

    if ((migration
            && (argc != 10
                || strcmp(argv[3], "--gdox-remove-migrated-source") != 0
                || strcmp(argv[4], "--gdox-clean-hdd") != 0
                || strcmp(argv[6], "--gdox-save-vault") != 0
                || strcmp(argv[8], "--gdox-expected-source-bytes") != 0
                || !absolute_path(argv[2])
                || !absolute_path(argv[5])
                || !absolute_path(argv[7])
                || argv[9][0] == '\0'))
        || (!migration
            && (argc != 5
                || strcmp(argv[1], "--gdox-validate-save-vault") != 0
                || strcmp(argv[3], "--gdox-clean-hdd") != 0
                || !absolute_path(argv[2])
                || !absolute_path(argv[4])))) {
        return 2;
    }
    if (mode != NULL && strcmp(mode, "nonzero") == 0) {
        (void)fputs("save operation rejected\n", stderr);
        return 7;
    }
    if (mode != NULL && strcmp(mode, "hang") == 0) {
#if defined(_WIN32)
        Sleep(10000U);
#else
        const struct timespec duration = {10, 0};
        (void)nanosleep(&duration, NULL);
#endif
    }
    if (mode != NULL && strcmp(mode, "malformed") == 0) {
        (void)puts("{\"schema\":3}");
        return 0;
    }
    if (migration && recover) {
        const char *quarantine = getenv("GDOX_TEST_XEMU_QUARANTINE");

        if (quarantine == NULL || quarantine[0] == '\0'
            || rename(quarantine, source) != 0) {
            return 8;
        }
    }
    if (migration && source_write) {
        FILE *file = fopen(source, "ab");

        if (file == NULL || fputc('x', file) == EOF || fclose(file) != 0) {
            return 8;
        }
    }
    if (migration) {
        if (!preserve && remove(source) != 0) {
            return 8;
        }
        (void)printf(
            "{\"schema\":3,\"operation\":\"xbox-hdd-save-migration-removal\","
            "\"source_writes\":%s,\"source_bytes\":%s,"
            "\"source_identity_sha256\":"
            "\"0000000000000000000000000000000000000000000000000000000000000000\","
            "\"source_sha256\":"
            "\"2222222222222222222222222222222222222222222222222222222222222222\","
            "\"source_hashed\":%s,\"source_projection_complete\":%s,"
            "\"clean_bytes\":14,"
            "\"clean_sha256\":"
            "\"3333333333333333333333333333333333333333333333333333333333333333\","
            "\"receipt_reused\":%s,\"source_removal_safe\":%s,"
            "\"source_removed\":%s,\"vault_version\":2,"
            "\"generation\":7,"
            "\"scope\":\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\","
            "\"format\":\"logical-files-v2\",\"entries\":3,"
            "\"logical_bytes\":42,\"tree_sha256\":"
            "\"1111111111111111111111111111111111111111111111111111111111111111\","
            "\"policy_revision\":1,\"policy_sha256\":"
            "\"4444444444444444444444444444444444444444444444444444444444444444\","
            "\"udata_entries\":2,\"udata_logical_bytes\":30,"
            "\"tdata_entries\":1,\"tdata_logical_bytes\":12,"
            "\"hdd_config_format\":\"hdd-config-v1\","
            "\"hdd_config_bytes\":524288,\"hdd_config_sha256\":"
            "\"5555555555555555555555555555555555555555555555555555555555555555\","
            "\"unclassified_tdata_entries\":%u,"
            "\"unclassified_tdata_bytes\":%u,"
            "\"unclassified_tdata_sha256\":"
            "\"6666666666666666666666666666666666666666666666666666666666666666\","
            "\"vault_reopened\":true,\"anonymous_cow\":true,"
            "\"clean_backing\":true,\"reprojected\":true,"
            "\"roundtrip_verified\":true}\n",
            source_write ? "true" : "false",
            argv[9],
            receipt ? "false" : "true",
            conflict || receipt ? "false" : "true",
            receipt ? "true" : "false",
            preserve ? "false" : "true",
            preserve ? "false" : "true",
            unclassified ? 1U : 0U,
            unclassified ? 5U : 0U
        );
    } else if (mode != NULL && strcmp(mode, "mismatch") == 0) {
        (void)puts(mismatched_validation);
    } else {
        (void)printf(validation_format, 0U, 0U);
    }
    return 0;
}
