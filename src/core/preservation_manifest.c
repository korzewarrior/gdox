#include "core/preservation_internal.h"

#include "platform/preservation_io.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct text_buffer {
    char *bytes;
    size_t length;
    size_t capacity;
} text_buffer;

static const char *format_slug(gdox_preservation_format format)
{
    return format == GDOX_PRESERVATION_REDUMP ? "redump" : "xiso";
}

static const char *status_slug(gdox_preservation_status status)
{
    switch (status) {
        case GDOX_PRESERVATION_PLAYABLE_XISO:
            return "playable-xiso";
        case GDOX_PRESERVATION_REDUMP_EVIDENCE_COMPLETE:
            return "redump-evidence-complete";
        case GDOX_PRESERVATION_REDUMP_CANDIDATE:
            return "redump-candidate";
    }
    return "unknown";
}

static const char *status_label(gdox_preservation_status status)
{
    switch (status) {
        case GDOX_PRESERVATION_PLAYABLE_XISO:
            return "Playable XISO";
        case GDOX_PRESERVATION_REDUMP_EVIDENCE_COMPLETE:
            return "Redump evidence complete";
        case GDOX_PRESERVATION_REDUMP_CANDIDATE:
            return "Redump candidate - additional evidence required";
    }
    return "Unknown";
}

static const char *map_source_slug(gdox_security_map_source source)
{
    switch (source) {
        case GDOX_SECURITY_MAP_AUTHENTICATED_SS:
            return "authenticated-security-sector";
        case GDOX_SECURITY_MAP_CATALOG:
            return "catalog-derived";
        case GDOX_SECURITY_MAP_USER:
            return "user-provided";
    }
    return "unknown";
}

static bool text_reserve(text_buffer *text, size_t additional)
{
    size_t required;
    size_t capacity;
    char *bytes;
    if (additional > SIZE_MAX - text->length - 1U) {
        return false;
    }
    required = text->length + additional + 1U;
    if (required <= text->capacity) {
        return true;
    }
    capacity = text->capacity == 0U ? 1024U : text->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    bytes = realloc(text->bytes, capacity);
    if (bytes == NULL) {
        return false;
    }
    text->bytes = bytes;
    text->capacity = capacity;
    return true;
}

static bool text_append(text_buffer *text, const char *bytes)
{
    const size_t length = strlen(bytes);
    if (!text_reserve(text, length)) {
        return false;
    }
    memcpy(text->bytes + text->length, bytes, length + 1U);
    text->length += length;
    return true;
}

static bool text_format(text_buffer *text, const char *format, ...)
{
    va_list arguments;
    va_list copy;
    int needed;

    va_start(arguments, format);
    va_copy(copy, arguments);
    needed = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (needed < 0 || !text_reserve(text, (size_t)needed)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(
        text->bytes + text->length,
        text->capacity - text->length,
        format,
        arguments
    );
    va_end(arguments);
    text->length += (size_t)needed;
    return true;
}

static bool text_json_string(text_buffer *text, const char *input)
{
    const unsigned char *cursor;

    if (input == NULL) {
        return text_append(text, "\"\"");
    }
    cursor = (const unsigned char *)input;
    if (!text_append(text, "\"")) {
        return false;
    }
    while (*cursor != 0U) {
        const unsigned char value = *cursor++;
        switch (value) {
            case '"':
                if (!text_append(text, "\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!text_append(text, "\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!text_append(text, "\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!text_append(text, "\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!text_append(text, "\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!text_append(text, "\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!text_append(text, "\\t")) {
                    return false;
                }
                break;
            default:
                if (value < 0x20U) {
                    if (!text_format(text, "\\u%04X", (unsigned int)value)) {
                        return false;
                    }
                } else {
                    char encoded[2] = {(char)value, '\0'};
                    if (!text_append(text, encoded)) {
                        return false;
                    }
                }
                break;
        }
    }
    return text_append(text, "\"");
}

static char *appended_path(const char *path, const char *suffix)
{
    const size_t path_bytes = strlen(path);
    const size_t suffix_bytes = strlen(suffix);
    char *output;
    if (path_bytes > SIZE_MAX - suffix_bytes - 1U) {
        return NULL;
    }
    output = malloc(path_bytes + suffix_bytes + 1U);
    if (output != NULL) {
        memcpy(output, path, path_bytes);
        memcpy(output + path_bytes, suffix, suffix_bytes + 1U);
    }
    return output;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator)) {
        separator = backslash;
    }
    return separator != NULL ? separator + 1 : path;
}

static bool write_new(
    const char *path,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
)
{
    gdox_preservation_file *file = NULL;
    if (!gdox_preservation_file_create(path, &file, error)) {
        return false;
    }
    if (!gdox_preservation_file_write(file, bytes, length, error)) {
        gdox_error close_error;
        (void)gdox_preservation_file_close(file, &close_error);
        return false;
    }
    return gdox_preservation_file_sync_close(file, error);
}

static bool write_suffix(
    const char *output_path,
    const char *suffix,
    const uint8_t *bytes,
    size_t length,
    gdox_error *error
)
{
    char *path = appended_path(output_path, suffix);
    bool success;
    if (path == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate preservation sidecar path");
        return false;
    }
    success = write_new(path, bytes, length, error);
    free(path);
    return success;
}

static bool suffix_available(
    const char *output_path,
    const char *suffix,
    gdox_error *error
)
{
    char *path = appended_path(output_path, suffix);
    bool available;
    if (path == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate preservation sidecar path");
        return false;
    }
    available = !gdox_preservation_path_exists(path);
    free(path);
    if (!available) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "a preservation sidecar already exists");
    }
    return available;
}

bool gdox_preservation_sidecars_available(
    const gdox_preservation_request *request,
    const gdox_disc_evidence *evidence,
    bool has_security_map,
    gdox_error *error
)
{
    static const char *common[] = {
        ".gdox.json", ".crc32", ".md5", ".sha1", ".sha256", ".log",
    };
    size_t index;
    for (index = 0U; index < sizeof(common) / sizeof(common[0]); ++index) {
        if (!suffix_available(request->output_path, common[index], error)) {
            return false;
        }
    }
    if (evidence->pfi_present
        && !suffix_available(request->output_path, ".pfi.bin", error)) {
        return false;
    }
    if (evidence->dmi_present
        && !suffix_available(request->output_path, ".dmi.bin", error)) {
        return false;
    }
    if (evidence->security_sector_present
        && !suffix_available(request->output_path, ".ss.bin", error)) {
        return false;
    }
    if (has_security_map
        && !suffix_available(request->output_path, ".security-map.json", error)) {
        return false;
    }
    if (request->format == GDOX_PRESERVATION_REDUMP
        && (!suffix_available(request->output_path, ".dvd", error)
            || !suffix_available(request->output_path, ".sectors.txt", error))) {
        return false;
    }
    return true;
}

static void format_hashes(
    const gdox_hashes *hashes,
    char crc32[9],
    char md5[GDOX_MD5_BYTES * 2U + 1U],
    char sha1[GDOX_SHA1_BYTES * 2U + 1U],
    char sha256[GDOX_SHA256_BYTES * 2U + 1U]
)
{
    (void)snprintf(crc32, 9U, "%08X", hashes->crc32);
    gdox_hash_hex(hashes->md5, sizeof(hashes->md5), false, md5);
    gdox_hash_hex(hashes->sha1, sizeof(hashes->sha1), false, sha1);
    gdox_hash_hex(hashes->sha256, sizeof(hashes->sha256), false, sha256);
}

static bool append_ranges(
    text_buffer *text,
    const gdox_bad_sector_range *ranges,
    size_t range_count
)
{
    size_t index;
    if (!text_append(text, "[")) {
        return false;
    }
    for (index = 0U; index < range_count; ++index) {
        if (!text_format(
                text,
                "%s{\"start_lba\":%llu,\"end_lba\":%llu}",
                index == 0U ? "" : ",",
                (unsigned long long)ranges[index].start_lba,
                (unsigned long long)ranges[index].end_lba
            )) {
            return false;
        }
    }
    return text_append(text, "]");
}

static bool build_manifest(
    const gdox_preservation_request *request,
    const gdox_preservation_input *input,
    const gdox_preservation_map *map,
    const gdox_preservation_result *result,
    text_buffer *text
)
{
    char crc32[9];
    char md5[GDOX_MD5_BYTES * 2U + 1U];
    char sha1[GDOX_SHA1_BYTES * 2U + 1U];
    char sha256[GDOX_SHA256_BYTES * 2U + 1U];
    const long long created = (long long)time(NULL);
    const bool ss_authenticated = result->evidence.security_sector_present
        && map != NULL
        && map->source == GDOX_SECURITY_MAP_AUTHENTICATED_SS;

    format_hashes(&result->hashes, crc32, md5, sha1, sha256);
    if (!text_append(text, "{\n  \"schema\":\"https://gdox.korze.org/schemas/preservation-manifest-v2.json\",\n")
        || !text_format(
            text,
            "  \"created_by\":\"GDOX %s\",\n"
            "  \"created_unix_seconds\":%lld,\n"
            "  \"format\":\"%s\",\n"
            "  \"title\":",
            GDOX_VERSION,
            created,
            format_slug(request->format)
        )
        || !text_json_string(text, input->title)
        || !text_append(text, ",\n  \"title_id\":")
        || (input->title_id_present
            ? !text_format(text, "\"%08X\"", input->title_id)
            : !text_append(text, "null"))
        || !text_append(text, ",\n  \"source\":")
        || !text_json_string(text, input->source_description)
        || !text_format(
            text,
            ",\n  \"source_lba_offset\":%llu,\n"
            "  \"image\":",
            (unsigned long long)input->source_lba_offset
        )
        || !text_json_string(text, base_name(request->output_path))
        || !text_format(
            text,
            ",\n  \"bytes\":%llu,\n"
            "  \"sectors\":%llu,\n"
            "  \"sector_size\":%u,\n"
            "  \"claims\":{\"readback_verified\":%s,"
            "\"filesystem_inventory_verified\":%s,"
            "\"full_logical_length\":%s,"
            "\"canonical_hash_match\":",
            (unsigned long long)result->bytes,
            (unsigned long long)(result->bytes / GDOX_LOGICAL_SECTOR_BYTES),
            GDOX_LOGICAL_SECTOR_BYTES,
            result->readback_verified ? "true" : "false",
            input->filesystem_inventory_verified ? "true" : "false",
            request->format == GDOX_PRESERVATION_REDUMP
                    && result->bytes
                        == GDOX_XGD1_REDUMP_SECTORS * GDOX_LOGICAL_SECTOR_BYTES
                ? "true"
                : "false"
        )
        || (result->expected_hashes_match < 0
            ? !text_append(text, "null")
            : !text_append(
                text,
                result->expected_hashes_match != 0 ? "true" : "false"
            ))
        || !text_format(
            text,
            ",\"ss_present\":%s,\"ss_authenticated\":%s},\n"
            "  \"compacted\":%s,\n"
            "  \"media_patches_applied\":%llu,\n"
            "  \"hashes\":{\"crc32\":\"%s\",\"md5\":\"%s\","
            "\"sha1\":\"%s\",\"sha256\":\"%s\"},\n"
            "  \"unreadable_ranges\":",
            result->evidence.security_sector_present ? "true" : "false",
            ss_authenticated ? "true" : "false",
            request->format == GDOX_PRESERVATION_XISO_COMPACT
                    && input->filesystem_inventory_verified
                ? "true"
                : "false",
            (unsigned long long)(request->format == GDOX_PRESERVATION_XISO_COMPACT
                ? input->media_patches
                : 0U),
            crc32,
            md5,
            sha1,
            sha256
        )
        || !append_ranges(
            text,
            result->unreadable_ranges,
            result->unreadable_range_count
        )
        || !text_format(
            text,
            ",\n  \"unreadable_sectors\":%llu,\n"
            "  \"normalized_security_sectors\":%llu,\n"
            "  \"status\":\"%s\",\n"
            "  \"status_label\":",
            (unsigned long long)result->unreadable_sectors,
            (unsigned long long)result->normalized_security_sectors,
            status_slug(result->status)
        )
        || !text_json_string(text, status_label(result->status))
        || !text_format(
            text,
            ",\n  \"evidence\":{\"pfi\":%s,\"dmi\":%s,"
            "\"security_sector\":%s,\"security_ranges_valid\":%s,\"note\":",
            result->evidence.pfi_present ? "true" : "false",
            result->evidence.dmi_present ? "true" : "false",
            result->evidence.security_sector_present ? "true" : "false",
            ss_authenticated ? "true" : "false"
        )
        || !text_json_string(text, result->evidence.note)
        || !text_append(text, "}")) {
        return false;
    }
    if (map != NULL) {
        if (!text_format(
                text,
                ",\n  \"security_map\":{\"source\":\"%s\","
                "\"authenticated\":%s,"
                "\"coordinate_space\":\"canonical-iso-lba\","
                "\"title\":",
                map_source_slug(map->source),
                map->source == GDOX_SECURITY_MAP_AUTHENTICATED_SS
                    ? "true"
                    : "false"
            )
            || !text_json_string(text, map->title)
            || !text_append(text, ",\"mastering_id\":")
            || !text_json_string(text, map->mastering_id)
            || !text_append(text, "}")) {
            return false;
        }
    }
    return text_append(text, "\n}\n");
}

static bool write_checksum(
    const char *output_path,
    const char *suffix,
    const char *hash,
    gdox_error *error
)
{
    text_buffer line = {0};
    bool success = text_format(
        &line,
        "%s  %s\n",
        hash,
        base_name(output_path)
    )
        && write_suffix(
            output_path,
            suffix,
            (const uint8_t *)line.bytes,
            line.length,
            error
        );
    free(line.bytes);
    if (!success && !gdox_error_is_set(error)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not build checksum sidecar");
    }
    return success;
}

static bool write_map(
    const char *output_path,
    const gdox_preservation_map *map,
    gdox_error *error
)
{
    text_buffer text = {0};
    size_t index;
    bool success = text_format(
        &text,
        "{\"schema\":\"https://gdox.korze.org/schemas/security-map-v1.json\","
        "\"source\":\"%s\",\"coordinate_space\":\"canonical-iso-lba\","
        "\"authenticated\":%s,\"title\":",
        map_source_slug(map->source),
        map->source == GDOX_SECURITY_MAP_AUTHENTICATED_SS ? "true" : "false"
    )
        && text_json_string(&text, map->title)
        && text_append(&text, ",\"mastering_id\":")
        && text_json_string(&text, map->mastering_id)
        && text_append(&text, ",\"ranges\":[");
    for (index = 0U; success && index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
        success = text_format(
            &text,
            "%s{\"start_lba\":%llu,\"end_lba\":%llu}",
            index == 0U ? "" : ",",
            (unsigned long long)map->ranges[index].start_lba,
            (unsigned long long)map->ranges[index].end_lba
        );
    }
    success = success
        && text_append(&text, "]}\n")
        && write_suffix(
            output_path,
            ".security-map.json",
            (const uint8_t *)text.bytes,
            text.length,
            error
        );
    free(text.bytes);
    if (!success && !gdox_error_is_set(error)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not build security-map sidecar");
    }
    return success;
}

static bool write_redump_sidecars(
    const char *output_path,
    const gdox_preservation_map *map,
    gdox_error *error
)
{
    text_buffer dvd = {0};
    text_buffer sectors = {0};
    size_t index;
    bool success = text_format(
        &dvd,
        "LayerBreak=1913776\n%s\n",
        base_name(output_path)
    );
    if (map != NULL) {
        for (index = 0U; success && index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
            success = text_format(
                &sectors,
                "%llu-%llu\n",
                (unsigned long long)map->ranges[index].start_lba,
                (unsigned long long)map->ranges[index].end_lba
            );
        }
    } else {
        success = text_append(&sectors, "\n");
    }
    success = success
        && write_suffix(
            output_path,
            ".dvd",
            (const uint8_t *)dvd.bytes,
            dvd.length,
            error
        )
        && write_suffix(
            output_path,
            ".sectors.txt",
            (const uint8_t *)sectors.bytes,
            sectors.length,
            error
        );
    free(dvd.bytes);
    free(sectors.bytes);
    if (!success && !gdox_error_is_set(error)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not build Redump sidecars");
    }
    return success;
}

static bool write_log(
    const gdox_preservation_request *request,
    const gdox_preservation_map *map,
    const gdox_preservation_result *result,
    gdox_error *error
)
{
    char crc32[9];
    char md5[GDOX_MD5_BYTES * 2U + 1U];
    char sha1[GDOX_SHA1_BYTES * 2U + 1U];
    char sha256[GDOX_SHA256_BYTES * 2U + 1U];
    text_buffer text = {0};
    bool success;
    format_hashes(&result->hashes, crc32, md5, sha1, sha256);
    success = text_format(
        &text,
        "GDOX %s\nFormat: %s\nImage: %s\nBytes: %llu\n"
        "Readback verified: %s\nCanonical hash match: %s\n"
        "Status: %s\nCRC32: %s\nMD5: %s\nSHA1: %s\nSHA256: %s\n"
        "Unreadable sectors: %llu\nNormalized security sectors: %llu\n"
        "Evidence note: %s\n",
        GDOX_VERSION,
        format_slug(request->format),
        request->output_path,
        (unsigned long long)result->bytes,
        result->readback_verified ? "true" : "false",
        result->expected_hashes_match < 0
            ? "not checked"
            : (result->expected_hashes_match != 0 ? "true" : "false"),
        status_label(result->status),
        crc32,
        md5,
        sha1,
        sha256,
        (unsigned long long)result->unreadable_sectors,
        (unsigned long long)result->normalized_security_sectors,
        result->evidence.note
    );
    if (success && map != NULL) {
        success = text_format(
            &text,
            "Security map: %s; authenticated: %s\n",
            map_source_slug(map->source),
            map->source == GDOX_SECURITY_MAP_AUTHENTICATED_SS ? "true" : "false"
        );
    }
    success = success
        && write_suffix(
            request->output_path,
            ".log",
            (const uint8_t *)text.bytes,
            text.length,
            error
        );
    free(text.bytes);
    if (!success && !gdox_error_is_set(error)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not build preservation log");
    }
    return success;
}

bool gdox_preservation_write_bundle(
    const gdox_preservation_request *request,
    const gdox_preservation_input *input,
    const gdox_preservation_map *security_map,
    gdox_preservation_result *result,
    gdox_error *error
)
{
    text_buffer manifest = {0};
    char crc32[9];
    char md5[GDOX_MD5_BYTES * 2U + 1U];
    char sha1[GDOX_SHA1_BYTES * 2U + 1U];
    char sha256[GDOX_SHA256_BYTES * 2U + 1U];
    char *manifest_path;
    bool success;

    format_hashes(&result->hashes, crc32, md5, sha1, sha256);
    manifest_path = appended_path(request->output_path, ".gdox.json");
    if (manifest_path == NULL
        || !build_manifest(request, input, security_map, result, &manifest)) {
        free(manifest_path);
        free(manifest.bytes);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not build preservation manifest");
        return false;
    }
    success = write_new(
        manifest_path,
        (const uint8_t *)manifest.bytes,
        manifest.length,
        error
    );
    if (success) {
        (void)snprintf(
            result->manifest_path,
            sizeof(result->manifest_path),
            "%s",
            manifest_path
        );
    }
    free(manifest_path);
    free(manifest.bytes);
    if (!success
        || !write_checksum(request->output_path, ".crc32", crc32, error)
        || !write_checksum(request->output_path, ".md5", md5, error)
        || !write_checksum(request->output_path, ".sha1", sha1, error)
        || !write_checksum(request->output_path, ".sha256", sha256, error)) {
        return false;
    }
    if (result->evidence.pfi_present
        && !write_suffix(
            request->output_path,
            ".pfi.bin",
            result->evidence.pfi,
            sizeof(result->evidence.pfi),
            error
        )) {
        return false;
    }
    if (result->evidence.dmi_present
        && !write_suffix(
            request->output_path,
            ".dmi.bin",
            result->evidence.dmi,
            sizeof(result->evidence.dmi),
            error
        )) {
        return false;
    }
    if (result->evidence.security_sector_present
        && !write_suffix(
            request->output_path,
            ".ss.bin",
            result->evidence.security_sector,
            sizeof(result->evidence.security_sector),
            error
        )) {
        return false;
    }
    if (security_map != NULL
        && !write_map(request->output_path, security_map, error)) {
        return false;
    }
    if (request->format == GDOX_PRESERVATION_REDUMP
        && !write_redump_sidecars(
            request->output_path,
            security_map,
            error
        )) {
        return false;
    }
    return write_log(request, security_map, result, error);
}
