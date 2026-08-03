#include "app/runtime_bundle.h"
#include "core/emulator_configuration.h"
#include "gdox/hash.h"
#include "platform/user_storage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_CONFIGURATION_MAX_BYTES ((size_t)16U * 1024U * 1024U)
#define GDOX_FIRMWARE_MAX_BYTES ((size_t)1024U * 1024U)
#define GDOX_EEPROM_BYTES ((size_t)256U)
#define GDOX_BUNDLED_HDD_BYTES UINT64_C(1638400)

static const uint8_t bundled_hdd_sha256[GDOX_SHA256_BYTES] = {
    0x00U, 0xd7U, 0xdfU, 0x7aU, 0x2bU, 0xc2U, 0x35U, 0xf8U,
    0x80U, 0x17U, 0x64U, 0xf0U, 0x0bU, 0x7fU, 0x40U, 0xe1U,
    0x94U, 0xd1U, 0xe3U, 0x92U, 0xf7U, 0xa9U, 0x61U, 0x9dU,
    0x6bU, 0x23U, 0x96U, 0xc8U, 0x97U, 0x70U, 0xf6U, 0xddU,
};

static const uint8_t mcpx_md5[GDOX_MD5_BYTES] = {
    0xd4U, 0x9cU, 0x52U, 0xa4U, 0x10U, 0x2fU, 0x6dU, 0xf7U,
    0xbcU, 0xf8U, 0xd0U, 0x61U, 0x7aU, 0xc4U, 0x75U, 0xedU,
};

static bool copy_path(
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    const char *input,
    gdox_error *error
)
{
    size_t bytes;

    if (input == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "runtime path is missing");
        return false;
    }
    bytes = strlen(input);
    if (bytes >= GDOX_EMULATOR_PATH_CAPACITY) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "runtime path is too long");
        return false;
    }
    memcpy(output, input, bytes + 1U);
    return true;
}

static bool valid_flash_size(size_t bytes)
{
    return bytes == (size_t)256U * 1024U
        || bytes == (size_t)512U * 1024U
        || bytes == (size_t)1024U * 1024U;
}

static bool validate_firmware_data(
    gdox_firmware_kind kind,
    const uint8_t *data,
    size_t bytes,
    gdox_error *error
)
{
    if (kind == GDOX_FIRMWARE_MCPX) {
        gdox_hashes hashes;
        if (bytes != 512U) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "MCPX 1.0 boot ROM must be exactly 512 bytes");
            return false;
        }
        if (!gdox_hash_buffer(data, bytes, &hashes, error)) {
            return false;
        }
        if (memcmp(hashes.md5, mcpx_md5, sizeof(mcpx_md5)) != 0) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "MCPX file does not match a correctly dumped MCPX 1.0 boot ROM");
            return false;
        }
        return true;
    }
    if (kind == GDOX_FIRMWARE_FLASH) {
        if (!valid_flash_size(bytes)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "Xbox-compatible BIOS must be 256 KiB, 512 KiB, or 1 MiB");
            return false;
        }
        return true;
    }
    gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "firmware kind is invalid");
    return false;
}

static bool read_validated_firmware(
    gdox_firmware_kind kind,
    const char *path,
    uint8_t **data,
    size_t *bytes,
    gdox_error *error
)
{
    bool found = false;

    if (!gdox_storage_read(
            path,
            GDOX_FIRMWARE_MAX_BYTES,
            data,
            bytes,
            &found,
            error
        )) {
        return false;
    }
    if (!found) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "firmware file was not found");
        return false;
    }
    if (!validate_firmware_data(kind, *data, *bytes, error)) {
        free(*data);
        *data = NULL;
        *bytes = 0U;
        return false;
    }
    return true;
}

static bool validate_firmware_file(
    gdox_firmware_kind kind,
    const char *path,
    gdox_error *error
)
{
    uint8_t *data = NULL;
    size_t bytes = 0U;
    const bool valid = read_validated_firmware(
        kind,
        path,
        &data,
        &bytes,
        error
    );

    free(data);
    return valid;
}

static bool managed_path(
    const char *relative,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    char storage_path[GDOX_STORAGE_PATH_CAPACITY];
    return gdox_user_data_path(relative, storage_path, error)
        && copy_path(output, storage_path, error);
}

static bool append_hdd_candidate(
    const char *directory,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    static const char suffix[] = "/hdd/xbox_hdd.qcow2";
    const size_t directory_bytes = strlen(directory);
    int written;

    if (directory_bytes + sizeof(suffix) > GDOX_EMULATOR_PATH_CAPACITY) {
        return false;
    }
    written = snprintf(
        output,
        GDOX_EMULATOR_PATH_CAPACITY,
        "%s%s",
        directory,
        suffix
    );
    return written >= 0 && (size_t)written < GDOX_EMULATOR_PATH_CAPACITY;
}

static bool find_hdd_template(
    const char *executable,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    char directory[GDOX_EMULATOR_PATH_CAPACITY];
    uint64_t bytes;
    size_t depth;

    if (strlen(executable) >= sizeof(directory)) {
        return false;
    }
    memcpy(directory, executable, strlen(executable) + 1U);
    for (depth = 0U; depth < 10U; ++depth) {
        char *slash = strrchr(directory, '/');
        char *backslash = strrchr(directory, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) {
            slash = backslash;
        }
        if (slash == NULL) {
            break;
        }
        *slash = '\0';
        if (append_hdd_candidate(directory, output)
            && gdox_storage_file_size(output, &bytes)
            && bytes != 0U && bytes <= UINT64_C(64) * 1024U * 1024U) {
            return true;
        }
    }
    return false;
}

static bool read_optional_configuration(
    const char *path,
    char **configuration,
    bool *found,
    gdox_error *error
)
{
    uint8_t *data = NULL;
    size_t bytes = 0U;

    if (!gdox_storage_read(
            path,
            GDOX_CONFIGURATION_MAX_BYTES,
            &data,
            &bytes,
            found,
            error
        )) {
        return false;
    }
    *configuration = (char *)data;
    return true;
}

static bool discover_external_configuration(
    const char *executable,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    bool *found,
    bool *required,
    gdox_error *error
)
{
    *found = gdox_emulator_discover_configuration(
        executable,
        output,
        required,
        error
    );
    if (*found) {
        return true;
    }
    if (error->code != GDOX_ERROR_NOT_FOUND) {
        return false;
    }
    gdox_error_clear(error);
    output[0] = '\0';
    return true;
}

static bool read_external_configuration(
    const char *executable,
    const char *managed,
    char **external_configuration,
    bool *external_found,
    gdox_error *error
)
{
    char external_path[GDOX_EMULATOR_PATH_CAPACITY];
    bool required = false;

    *external_configuration = NULL;
    if (!discover_external_configuration(
            executable,
            external_path,
            external_found,
            &required,
            error
        )) {
        return false;
    }
    if (*external_found) {
        if (strcmp(external_path, managed) == 0) {
            *external_found = false;
        } else if (!read_optional_configuration(
                external_path,
                external_configuration,
                external_found,
                error
            )) {
            if (required || error->code == GDOX_ERROR_INTERNAL
                || error->code == GDOX_ERROR_INVALID_ARGUMENT) {
                return false;
            }
            gdox_error_clear(error);
            *external_found = false;
        } else if (required && !*external_found) {
            gdox_error_set(
                error,
                GDOX_ERROR_NOT_FOUND,
                "selected xemu configuration was not found"
            );
            return false;
        }
    }
    return true;
}

static bool adopt_configured_firmware(
    gdox_firmware_kind kind,
    const char *key,
    const char *configuration,
    const char *managed,
    bool *ready,
    gdox_error *error
)
{
    char source[GDOX_EMULATOR_PATH_CAPACITY];
    uint8_t *data = NULL;
    size_t bytes = 0U;
    gdox_error ignored;

    *ready = validate_firmware_file(kind, managed, &ignored);
    if (*ready) {
        return true;
    }
    if (!gdox_emulator_configuration_get_file(
            configuration,
            key,
            source,
            &ignored
        ) || !read_validated_firmware(
            kind,
            source,
            &data,
            &bytes,
            &ignored
        )) {
        gdox_error_clear(error);
        return true;
    }
    if (!gdox_storage_write_private(
            managed,
            data,
            bytes,
            true,
            error
        )) {
        free(data);
        return false;
    }
    free(data);
    *ready = true;
    return true;
}

static bool file_exists(const char *path)
{
    uint64_t bytes;
    return gdox_storage_file_size(path, &bytes);
}

static bool validate_managed_eeprom(
    const char *path,
    bool *present,
    gdox_error *error
)
{
    uint64_t bytes;

    *present = gdox_storage_file_size(path, &bytes);
    if (!*present || bytes == GDOX_EEPROM_BYTES) {
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_SOURCE,
        "managed xemu EEPROM must be exactly 256 bytes"
    );
    return false;
}

static bool adopt_configured_eeprom(
    const char *configuration,
    const char *managed,
    gdox_error *error
)
{
    char source[GDOX_EMULATOR_PATH_CAPACITY];
    uint8_t *data = NULL;
    size_t bytes = 0U;
    bool found = false;
    bool managed_present = false;
    gdox_error ignored;

    if (!validate_managed_eeprom(managed, &managed_present, error)) {
        return false;
    }
    if (managed_present || configuration == NULL
        || !gdox_emulator_configuration_get_file(
            configuration,
            "eeprom_path",
            source,
            &ignored
        ) || !gdox_storage_read(
            source,
            GDOX_EEPROM_BYTES,
            &data,
            &bytes,
            &found,
            &ignored
        ) || !found || bytes != GDOX_EEPROM_BYTES) {
        free(data);
        return true;
    }
    if (!gdox_storage_write_private(
            managed,
            data,
            bytes,
            false,
            error
        )) {
        free(data);
        return false;
    }
    free(data);
    return true;
}

static bool set_file_if_ready(
    char **configuration,
    const char *key,
    const char *path,
    bool ready,
    gdox_error *error
)
{
    char *updated;
    if (!ready) {
        return true;
    }
    if (!gdox_emulator_configuration_set_file(
            *configuration,
            key,
            path,
            &updated,
            error
        )) {
        return false;
    }
    free(*configuration);
    *configuration = updated;
    return true;
}

typedef struct runtime_bundle_paths {
    char configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd[GDOX_EMULATOR_PATH_CAPACITY];
    char mcpx[GDOX_EMULATOR_PATH_CAPACITY];
    char flash[GDOX_EMULATOR_PATH_CAPACITY];
    char eeprom[GDOX_EMULATOR_PATH_CAPACITY];
} runtime_bundle_paths;

static bool adopt_firmware_from_configuration(
    const char *configuration,
    const runtime_bundle_paths *paths,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    return adopt_configured_firmware(
                GDOX_FIRMWARE_MCPX,
                "bootrom_path",
                configuration,
                paths->mcpx,
                &status->mcpx_ready,
                error
            )
            && adopt_configured_firmware(
                GDOX_FIRMWARE_FLASH,
                "flashrom_path",
                configuration,
                paths->flash,
                &status->flash_ready,
                error
            );
}

static bool adopt_configuration_assets(
    const char *configuration,
    const runtime_bundle_paths *paths,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    return adopt_firmware_from_configuration(
            configuration,
            paths,
            status,
            error
        )
        && adopt_configured_eeprom(
            configuration,
            paths->eeprom,
            error
        );
}

static bool select_executable(
    const char *override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    const char *environment_override = getenv("GDOX_XEMU");

    status->custom_executable = (override != NULL && override[0] != '\0')
        || (environment_override != NULL
            && environment_override[0] != '\0');
    if (status->custom_executable) {
        const char *selected = override != NULL && override[0] != '\0'
            ? override : environment_override;

        status->xemu_available =
            gdox_emulator_validate_executable(selected, error)
            && copy_path(status->executable, selected, error);
    } else if (gdox_emulator_discover_executable(
                   status->executable, error
               )) {
        status->xemu_available = true;
    } else if (error->code == GDOX_ERROR_NOT_FOUND) {
        gdox_error_clear(error);
        return true;
    } else {
        return false;
    }
    if (!status->xemu_available
        || !gdox_emulator_query_storage_capabilities(
            status->executable,
            &status->persistent_save_export,
            error
        )) {
        status->xemu_available = false;
        return false;
    }
    status->full_hdd_isolation = true;
    if (!status->persistent_save_export) {
        status->xemu_available = false;
        status->full_hdd_isolation = false;
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "xemu does not provide persistent logical save export"
        );
        return false;
    }
    return true;
}

static bool resolve_managed_paths(
    runtime_bundle_paths *paths,
    gdox_error *error
)
{
    return managed_path("xemu/xemu.toml", paths->configuration, error)
        && managed_path("xemu/xbox_hdd.qcow2", paths->hdd, error)
        && managed_path("xemu/eeprom.bin", paths->eeprom, error)
        && managed_path(
            "xemu/firmware/mcpx_1.0.bin",
            paths->mcpx,
            error
        )
        && managed_path("xemu/firmware/bios.bin", paths->flash, error);
}

static bool find_hdd_for_executable(
    const char *executable,
    char hdd_template[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    char default_executable[GDOX_EMULATOR_PATH_CAPACITY];

    if (find_hdd_template(executable, hdd_template)) {
        return true;
    }
    if (gdox_emulator_discover_executable(default_executable, error)
        && strcmp(default_executable, executable) != 0
        && find_hdd_template(default_executable, hdd_template)) {
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        "included clean Xbox hard disk was not found beside the xemu runtime"
    );
    return false;
}

static bool validate_bundled_hdd(
    const char *path,
    gdox_error *error
)
{
    gdox_hashes hashes;
    uint64_t bytes = 0U;

    if (!gdox_hash_file(path, &hashes, &bytes, error)) {
        return false;
    }
    if (bytes != GDOX_BUNDLED_HDD_BYTES
        || memcmp(
            hashes.sha256,
            bundled_hdd_sha256,
            sizeof(bundled_hdd_sha256)
        ) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "included Xbox hard disk is not the verified clean dashboard image"
        );
        return false;
    }
    return true;
}

static bool prepare_hdd(
    const gdox_runtime_bundle_status *status,
    char active_hdd[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    if (!find_hdd_for_executable(status->executable, active_hdd, error)) {
        return false;
    }
    return validate_bundled_hdd(active_hdd, error);
}

#ifdef GDOX_RUNTIME_BUNDLE_TESTING
static bool prepare_fixture_hdd(
    const char *path,
    char active_hdd[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    bool found = false;
    uint64_t bytes = 0U;

    if (!gdox_storage_ordinary_file(path, &found, error)
        || !found
        || !gdox_storage_file_size(path, &bytes)
        || bytes == 0U
        || bytes > UINT64_C(64) * 1024U * 1024U) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "clean HDD fixture is not a bounded ordinary file"
            );
        }
        return false;
    }
    return copy_path(active_hdd, path, error);
}
#endif

static bool configure_runtime_files(
    char **configuration,
    const runtime_bundle_paths *paths,
    const char *active_hdd,
    const gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    return set_file_if_ready(
            configuration,
            "hdd_path",
            active_hdd,
            true,
            error
        )
        && set_file_if_ready(
            configuration,
            "eeprom_path",
            paths->eeprom,
            true,
            error
        )
        && set_file_if_ready(
            configuration,
            "bootrom_path",
            paths->mcpx,
            status->mcpx_ready,
            error
        )
        && set_file_if_ready(
            configuration,
            "flashrom_path",
            paths->flash,
            status->flash_ready,
            error
        );
}

static bool publish_bundle_paths(
    gdox_runtime_bundle_status *status,
    const runtime_bundle_paths *paths,
    const char *active_hdd,
    gdox_error *error
)
{
    return copy_path(status->configuration, paths->configuration, error)
        && copy_path(status->mcpx, paths->mcpx, error)
        && copy_path(status->flash, paths->flash, error)
        && copy_path(status->hdd, active_hdd, error)
        && copy_path(status->eeprom, paths->eeprom, error);
}

static bool prepare_configuration(
    gdox_runtime_bundle_status *status,
    const runtime_bundle_paths *paths,
    const char *active_hdd,
    const char *known_external_configuration,
    bool external_checked,
    gdox_error *error
)
{
    char *configuration = NULL;
    char *managed_configuration = NULL;
    char *external_configuration = NULL;
    bool external_found = false;
    bool managed_found = false;
    bool success = false;

    if (!read_optional_configuration(
            paths->configuration,
            &managed_configuration,
            &managed_found,
            error
        ) || !adopt_configuration_assets(
            managed_configuration,
            paths,
            status,
            error
        )) {
        goto cleanup;
    }
    if (known_external_configuration != NULL
        && !adopt_configuration_assets(
            known_external_configuration,
            paths,
            status,
            error
        )) {
        goto cleanup;
    }
    if (!external_checked
        && (!status->mcpx_ready || !status->flash_ready
            || !file_exists(paths->eeprom))
        && (!read_external_configuration(
            status->executable,
            paths->configuration,
            &external_configuration,
            &external_found,
            error
        ) || (external_found
            && !adopt_configuration_assets(
                external_configuration,
                paths,
                status,
                error
            )))) {
        goto cleanup;
    }
    configuration = calloc(1U, 1U);
    if (configuration == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate xemu configuration"
        );
        goto cleanup;
    }
    if (!configure_runtime_files(
            &configuration,
            paths,
            active_hdd,
            status,
            error
        )) {
        goto cleanup;
    }
    if ((!managed_found
            || strcmp(configuration, managed_configuration) != 0)
        && !gdox_storage_write_private(
            paths->configuration,
            (const uint8_t *)configuration,
            strlen(configuration),
            true,
            error
        )) {
        goto cleanup;
    }
    if (!publish_bundle_paths(status, paths, active_hdd, error)) {
        goto cleanup;
    }
    status->configuration_ready = true;
    success = true;

cleanup:
    free(managed_configuration);
    free(external_configuration);
    free(configuration);
    return success;
}

static bool begin_bundle_preparation(
    const char *executable_override,
    gdox_runtime_bundle_status *status,
    runtime_bundle_paths *paths,
    bool *available,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (status == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "runtime bundle status is required");
        return false;
    }
    memset(status, 0, sizeof(*status));
    if (!select_executable(executable_override, status, error)) {
        return false;
    }
    *available = status->xemu_available;
    if (!*available) {
        return true;
    }
    return resolve_managed_paths(paths, error);
}

static bool finish_bundle_preparation(
    gdox_runtime_bundle_status *status,
    const runtime_bundle_paths *paths,
    const char *active_hdd,
    gdox_error *error
)
{
    status->bundled = !status->custom_executable;
    status->hdd_ready = true;
    return prepare_configuration(
        status,
        paths,
        active_hdd,
        NULL,
        false,
        error
    );
}

static bool prepare_bundle(
    const char *executable_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    runtime_bundle_paths paths;
    char active_hdd[GDOX_EMULATOR_PATH_CAPACITY];
    bool available;

    if (!begin_bundle_preparation(
            executable_override,
            status,
            &paths,
            &available,
            error
        )) {
        return false;
    }
    if (!available) {
        return true;
    }
    if (!prepare_hdd(status, active_hdd, error)) {
        return false;
    }
    return finish_bundle_preparation(
        status, &paths, active_hdd, error
    );
}

bool gdox_runtime_bundle_prepare(
    const char *executable_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    return prepare_bundle(executable_override, status, error);
}

#ifdef GDOX_RUNTIME_BUNDLE_TESTING
bool gdox_runtime_bundle_prepare_fixture(
    const char *executable_override,
    const char *clean_hdd_fixture,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    runtime_bundle_paths paths;
    char active_hdd[GDOX_EMULATOR_PATH_CAPACITY];
    bool available;

    if (clean_hdd_fixture == NULL || clean_hdd_fixture[0] == '\0') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "clean HDD fixture is required"
        );
        return false;
    }
    if (!begin_bundle_preparation(
            executable_override,
            status,
            &paths,
            &available,
            error
        )) {
        return false;
    }
    if (!available) {
        return true;
    }
    if (!prepare_fixture_hdd(clean_hdd_fixture, active_hdd, error)) {
        return false;
    }
    return finish_bundle_preparation(
        status, &paths, active_hdd, error
    );
}
#endif

bool gdox_runtime_bundle_import_firmware(
    gdox_firmware_kind kind,
    const char *source,
    const char *executable_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    char destination[GDOX_EMULATOR_PATH_CAPACITY];
    const char *relative;

    gdox_error_clear(error);
    if (source == NULL || source[0] == '\0' || status == NULL
        || (kind != GDOX_FIRMWARE_MCPX
            && kind != GDOX_FIRMWARE_FLASH)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "firmware kind, source, and runtime status are required");
        return false;
    }
    if (!validate_firmware_file(kind, source, error)) {
        return false;
    }
    relative = kind == GDOX_FIRMWARE_MCPX
        ? "xemu/firmware/mcpx_1.0.bin"
        : "xemu/firmware/bios.bin";
    if (!managed_path(relative, destination, error)
        || !gdox_storage_copy_private(source, destination, true, error)) {
        return false;
    }
    return gdox_runtime_bundle_prepare(
        executable_override,
        status,
        error
    );
}

bool gdox_runtime_bundle_import_firmware_auto(
    const char *source,
    const char *executable_override,
    gdox_firmware_kind *kind,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    uint64_t bytes;
    gdox_firmware_kind detected;

    if (source == NULL || kind == NULL || status == NULL
        || !gdox_storage_file_size(source, &bytes)) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "dropped firmware file is unavailable");
        return false;
    }
    detected = bytes == 512U
        ? GDOX_FIRMWARE_MCPX
        : GDOX_FIRMWARE_FLASH;
    if (!gdox_runtime_bundle_import_firmware(
            detected,
            source,
            executable_override,
            status,
            error
        )) {
        return false;
    }
    *kind = detected;
    return true;
}
