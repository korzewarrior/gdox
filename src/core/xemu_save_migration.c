#include "core/xemu_save_migration.h"

#include "gdox/hash.h"

#include <stdint.h>
#include <string.h>

static bool consume_literal(
    const char **cursor,
    const char *end,
    const char *literal
)
{
    const size_t bytes = strlen(literal);

    if ((size_t)(end - *cursor) < bytes
        || memcmp(*cursor, literal, bytes) != 0) {
        return false;
    }
    *cursor += bytes;
    return true;
}

static bool consume_u64(
    const char **cursor,
    const char *end,
    uint64_t maximum,
    uint64_t *value
)
{
    uint64_t parsed = 0U;
    const char *start = *cursor;

    if (start == end || *start < '0' || *start > '9') {
        return false;
    }
    if (*start == '0' && start + 1 != end
        && start[1] >= '0' && start[1] <= '9') {
        return false;
    }
    while (*cursor != end && **cursor >= '0' && **cursor <= '9') {
        const uint64_t digit = (uint64_t)(**cursor - '0');

        if (parsed > (maximum - digit) / UINT64_C(10)) {
            return false;
        }
        parsed = parsed * UINT64_C(10) + digit;
        ++*cursor;
    }
    *value = parsed;
    return true;
}

static bool consume_u32(
    const char **cursor,
    const char *end,
    uint32_t *value
)
{
    uint64_t parsed;

    if (!consume_u64(cursor, end, UINT32_MAX, &parsed)) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool consume_bool(
    const char **cursor,
    const char *end,
    bool *value
)
{
    if (consume_literal(cursor, end, "true")) {
        *value = true;
        return true;
    }
    if (consume_literal(cursor, end, "false")) {
        *value = false;
        return true;
    }
    return false;
}

static bool consume_sha256(
    const char **cursor,
    const char *end,
    uint8_t sha256[GDOX_SHA256_BYTES]
)
{
    size_t index;

    if ((size_t)(end - *cursor) < GDOX_SHA256_BYTES * 2U) {
        return false;
    }
    for (index = 0U; index < GDOX_SHA256_BYTES; ++index) {
        const char high = (*cursor)[index * 2U];
        const char low = (*cursor)[index * 2U + 1U];
        uint8_t high_value;
        uint8_t low_value;

        if (!((high >= '0' && high <= '9')
                || (high >= 'a' && high <= 'f'))
            || !((low >= '0' && low <= '9')
                || (low >= 'a' && low <= 'f'))) {
            return false;
        }
        high_value = high <= '9'
            ? (uint8_t)(high - '0') : (uint8_t)(high - 'a' + 10);
        low_value = low <= '9'
            ? (uint8_t)(low - '0') : (uint8_t)(low - 'a' + 10);
        sha256[index] = (uint8_t)((high_value << 4U) | low_value);
    }
    *cursor += GDOX_SHA256_BYTES * 2U;
    return true;
}

static bool consume_vault_proof(
    const char **cursor,
    const char *end,
    gdox_xemu_save_vault_proof *proof
)
{
    if (!consume_literal(cursor, end, ",\"vault_version\":")
        || !consume_u32(cursor, end, &proof->vault_version)
        || !consume_literal(cursor, end, ",\"generation\":")
        || !consume_u64(cursor, end, UINT64_MAX, &proof->generation)
        || !consume_literal(
            cursor,
            end,
            ",\"scope\":\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\","
            "\"format\":\"logical-files-v2\",\"entries\":"
        )
        || !consume_u32(cursor, end, &proof->entries)
        || !consume_literal(cursor, end, ",\"logical_bytes\":")
        || !consume_u64(cursor, end, UINT64_MAX, &proof->logical_bytes)
        || !consume_literal(cursor, end, ",\"tree_sha256\":\"")
        || !consume_sha256(cursor, end, proof->tree_sha256)
        || !consume_literal(cursor, end, "\",\"policy_revision\":")
        || !consume_u32(cursor, end, &proof->policy_revision)
        || !consume_literal(cursor, end, ",\"policy_sha256\":\"")
        || !consume_sha256(cursor, end, proof->policy_sha256)
        || !consume_literal(cursor, end, "\",\"udata_entries\":")
        || !consume_u32(cursor, end, &proof->udata_entries)
        || !consume_literal(cursor, end, ",\"udata_logical_bytes\":")
        || !consume_u64(
            cursor, end, UINT64_MAX, &proof->udata_logical_bytes
        )
        || !consume_literal(cursor, end, ",\"tdata_entries\":")
        || !consume_u32(cursor, end, &proof->tdata_entries)
        || !consume_literal(cursor, end, ",\"tdata_logical_bytes\":")
        || !consume_u64(
            cursor, end, UINT64_MAX, &proof->tdata_logical_bytes
        )
        || !consume_literal(
            cursor,
            end,
            ",\"hdd_config_format\":\"hdd-config-v1\","
            "\"hdd_config_bytes\":"
        )
        || !consume_u64(cursor, end, UINT64_MAX, &proof->hdd_config_bytes)
        || !consume_literal(cursor, end, ",\"hdd_config_sha256\":\"")
        || !consume_sha256(cursor, end, proof->hdd_config_sha256)
        || !consume_literal(
            cursor, end, "\",\"unclassified_tdata_entries\":"
        )
        || !consume_u32(
            cursor, end, &proof->unclassified_tdata_entries
        )
        || !consume_literal(
            cursor, end, ",\"unclassified_tdata_bytes\":"
        )
        || !consume_u64(
            cursor, end, UINT64_MAX, &proof->unclassified_tdata_bytes
        )
        || !consume_literal(
            cursor, end, ",\"unclassified_tdata_sha256\":\""
        )
        || !consume_sha256(
            cursor, end, proof->unclassified_tdata_sha256
        )
        || !consume_literal(cursor, end, "\",\"vault_reopened\":")
        || !consume_bool(cursor, end, &proof->vault_reopened)
        || !consume_literal(cursor, end, ",\"anonymous_cow\":")
        || !consume_bool(cursor, end, &proof->anonymous_cow)
        || !consume_literal(cursor, end, ",\"clean_backing\":")
        || !consume_bool(cursor, end, &proof->clean_backing)
        || !consume_literal(cursor, end, ",\"reprojected\":")
        || !consume_bool(cursor, end, &proof->reprojected)
        || !consume_literal(cursor, end, ",\"roundtrip_verified\":")
        || !consume_bool(cursor, end, &proof->roundtrip_verified)
        || !consume_literal(cursor, end, "}")) {
        return false;
    }
    return proof->vault_version == GDOX_XEMU_SAVE_VAULT_VERSION
        && proof->policy_revision == GDOX_XEMU_SAVE_POLICY_REVISION
        && proof->hdd_config_bytes == GDOX_XEMU_SAVE_HDD_CONFIG_BYTES
        && proof->vault_reopened
        && proof->anonymous_cow
        && proof->clean_backing
        && proof->reprojected
        && proof->roundtrip_verified
        && (proof->unclassified_tdata_entries != 0U
            || proof->unclassified_tdata_bytes == 0U);
}

static const char *trim_one_line_ending(
    const char *output,
    size_t *bytes
)
{
    if (*bytes != 0U && output[*bytes - 1U] == '\n') {
        --*bytes;
        if (*bytes != 0U && output[*bytes - 1U] == '\r') {
            --*bytes;
        }
    }
    return output + *bytes;
}

static bool migration_state_is_consistent(
    const gdox_xemu_save_migration_proof *proof
)
{
    const bool has_unclassified =
        proof->vault.unclassified_tdata_entries != 0U;

    if (proof->source_writes || proof->source_bytes == 0U
        || proof->clean_bytes == 0U) {
        return false;
    }
    if (proof->receipt_reused) {
        return has_unclassified
            && !proof->source_hashed
            && !proof->source_projection_complete
            && !proof->source_removal_safe
            && !proof->source_removed;
    }
    if (!proof->source_hashed) {
        return false;
    }
    if (proof->source_removal_safe) {
        return proof->source_projection_complete
            && !has_unclassified
            && proof->source_removed;
    }
    if (proof->source_removed) {
        return false;
    }
    return has_unclassified || !proof->source_projection_complete;
}

static void invalid_result(gdox_error *error)
{
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "xemu did not report a valid verified logical-save result"
    );
}

bool gdox_xemu_save_migration_result_parse(
    const char *output,
    size_t bytes,
    uint64_t expected_source_bytes,
    gdox_xemu_save_migration_proof *proof,
    gdox_error *error
)
{
    const char *cursor = output;
    const char *end;

    gdox_error_clear(error);
    if (output == NULL || proof == NULL || expected_source_bytes == 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu migration output, source size, and proof are required"
        );
        return false;
    }
    memset(proof, 0, sizeof(*proof));
    end = trim_one_line_ending(output, &bytes);
    if (!consume_literal(
            &cursor,
            end,
            "{\"schema\":3,\"operation\":\"xbox-hdd-save-migration-removal\","
            "\"source_writes\":"
        )
        || !consume_bool(&cursor, end, &proof->source_writes)
        || !consume_literal(&cursor, end, ",\"source_bytes\":")
        || !consume_u64(&cursor, end, UINT64_MAX, &proof->source_bytes)
        || !consume_literal(
            &cursor, end, ",\"source_identity_sha256\":\""
        )
        || !consume_sha256(
            &cursor, end, proof->source_identity_sha256
        )
        || !consume_literal(&cursor, end, "\",\"source_sha256\":\"")
        || !consume_sha256(&cursor, end, proof->source_sha256)
        || !consume_literal(&cursor, end, "\",\"source_hashed\":")
        || !consume_bool(&cursor, end, &proof->source_hashed)
        || !consume_literal(
            &cursor, end, ",\"source_projection_complete\":"
        )
        || !consume_bool(
            &cursor, end, &proof->source_projection_complete
        )
        || !consume_literal(&cursor, end, ",\"clean_bytes\":")
        || !consume_u64(&cursor, end, UINT64_MAX, &proof->clean_bytes)
        || !consume_literal(&cursor, end, ",\"clean_sha256\":\"")
        || !consume_sha256(&cursor, end, proof->clean_sha256)
        || !consume_literal(&cursor, end, "\",\"receipt_reused\":")
        || !consume_bool(&cursor, end, &proof->receipt_reused)
        || !consume_literal(&cursor, end, ",\"source_removal_safe\":")
        || !consume_bool(&cursor, end, &proof->source_removal_safe)
        || !consume_literal(&cursor, end, ",\"source_removed\":")
        || !consume_bool(&cursor, end, &proof->source_removed)
        || !consume_vault_proof(&cursor, end, &proof->vault)
        || cursor != end
        || proof->source_bytes != expected_source_bytes
        || !migration_state_is_consistent(proof)) {
        invalid_result(error);
        memset(proof, 0, sizeof(*proof));
        return false;
    }
    return true;
}

bool gdox_xemu_save_validation_result_parse(
    const char *output,
    size_t bytes,
    gdox_xemu_save_vault_proof *proof,
    gdox_error *error
)
{
    const char *cursor = output;
    const char *end;

    gdox_error_clear(error);
    if (output == NULL || proof == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu validation output and proof are required"
        );
        return false;
    }
    memset(proof, 0, sizeof(*proof));
    end = trim_one_line_ending(output, &bytes);
    if (!consume_literal(
            &cursor,
            end,
            "{\"schema\":3,\"operation\":\"xbox-save-vault-validation\""
        )
        || !consume_vault_proof(&cursor, end, proof)
        || cursor != end) {
        invalid_result(error);
        memset(proof, 0, sizeof(*proof));
        return false;
    }
    return true;
}

bool gdox_xemu_save_vault_proof_equal(
    const gdox_xemu_save_vault_proof *left,
    const gdox_xemu_save_vault_proof *right
)
{
    return left != NULL && right != NULL
        && left->vault_version == right->vault_version
        && left->generation == right->generation
        && left->entries == right->entries
        && left->logical_bytes == right->logical_bytes
        && memcmp(left->tree_sha256, right->tree_sha256, 32U) == 0
        && left->policy_revision == right->policy_revision
        && memcmp(left->policy_sha256, right->policy_sha256, 32U) == 0
        && left->udata_entries == right->udata_entries
        && left->udata_logical_bytes == right->udata_logical_bytes
        && left->tdata_entries == right->tdata_entries
        && left->tdata_logical_bytes == right->tdata_logical_bytes
        && left->hdd_config_bytes == right->hdd_config_bytes
        && memcmp(
            left->hdd_config_sha256,
            right->hdd_config_sha256,
            32U
        ) == 0
        && left->vault_reopened == right->vault_reopened
        && left->anonymous_cow == right->anonymous_cow
        && left->clean_backing == right->clean_backing
        && left->reprojected == right->reprojected
        && left->roundtrip_verified == right->roundtrip_verified;
}
