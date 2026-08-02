#include "app/runtime_bundle.h"

#include "gdox/hash.h"
#include "core/emulator_configuration.h"
#include "platform/user_storage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_CONFIGURATION_MAX_BYTES ((size_t)16U * 1024U * 1024U)
#define GDOX_FIRMWARE_MAX_BYTES ((size_t)1024U * 1024U)

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

static bool validate_firmware_file(
    gdox_firmware_kind kind,
    const char *path,
    gdox_error *error
)
{
    uint8_t *data = NULL;
    size_t bytes = 0U;
    bool found = false;
    bool valid;

    if (!gdox_storage_read(
            path,
            GDOX_FIRMWARE_MAX_BYTES,
            &data,
            &bytes,
            &found,
            error
        )) {
        return false;
    }
    if (!found) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "firmware file was not found");
        return false;
    }
    valid = validate_firmware_data(kind, data, bytes, error);
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
    char fallback[GDOX_STORAGE_PATH_CAPACITY];
    gdox_error ignored;
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
    return gdox_user_data_path(
        "runtime/hdd/xbox_hdd.qcow2",
        fallback,
        &ignored
    ) && copy_path(output, fallback, &ignored)
        && gdox_storage_file_size(output, &bytes)
        && bytes != 0U && bytes <= UINT64_C(64) * 1024U * 1024U;
}

static bool read_configuration(
    const char *managed,
    char **configuration,
    bool *managed_found,
    gdox_error *error
)
{
    uint8_t *data = NULL;
    size_t bytes = 0U;
    bool found = false;
    gdox_emulator_paths fallback;

    if (!gdox_storage_read(
            managed,
            GDOX_CONFIGURATION_MAX_BYTES,
            &data,
            &bytes,
            &found,
            error
        )) {
        return false;
    }
    if (found) {
        *configuration = (char *)data;
        *managed_found = true;
        return true;
    }
    if (gdox_emulator_discover(&fallback, error)
        && strcmp(fallback.configuration, managed) != 0) {
        if (!gdox_storage_read(
                fallback.configuration,
                GDOX_CONFIGURATION_MAX_BYTES,
                &data,
                &bytes,
                &found,
                error
            )) {
            return false;
        }
        if (found) {
            *configuration = (char *)data;
            *managed_found = false;
            return true;
        }
    }
    gdox_error_clear(error);
    *configuration = calloc(1U, 1U);
    *managed_found = false;
    if (*configuration == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate xemu configuration");
        return false;
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
        ) || !validate_firmware_file(kind, source, &ignored)) {
        gdox_error_clear(error);
        return true;
    }
    if (!gdox_storage_copy_private(source, managed, true, error)) {
        return false;
    }
    *ready = true;
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

static bool inspect_external_configuration(
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    gdox_emulator_paths paths;
    uint8_t *configuration = NULL;
    size_t bytes = 0U;
    bool found = false;
    char path[GDOX_EMULATOR_PATH_CAPACITY];
    uint64_t hdd_bytes;
    gdox_error ignored;

    if (!gdox_emulator_discover(&paths, error)
        || !gdox_storage_read(
            paths.configuration,
            GDOX_CONFIGURATION_MAX_BYTES,
            &configuration,
            &bytes,
            &found,
            error
        ) || !found) {
        gdox_error_clear(error);
        return true;
    }
    (void)copy_path(status->configuration, paths.configuration, &ignored);
    status->configuration_ready = true;
    status->mcpx_ready =
        gdox_emulator_configuration_get_file(
            (const char *)configuration,
            "bootrom_path",
            path,
            &ignored
        );
    if (status->mcpx_ready) {
        (void)copy_path(status->mcpx, path, &ignored);
        status->mcpx_ready =
            validate_firmware_file(GDOX_FIRMWARE_MCPX, path, &ignored);
    }
    status->flash_ready =
        gdox_emulator_configuration_get_file(
            (const char *)configuration,
            "flashrom_path",
            path,
            &ignored
        );
    if (status->flash_ready) {
        (void)copy_path(status->flash, path, &ignored);
        status->flash_ready =
            validate_firmware_file(GDOX_FIRMWARE_FLASH, path, &ignored);
    }
    status->hdd_ready =
        gdox_emulator_configuration_get_file(
            (const char *)configuration,
            "hdd_path",
            path,
            &ignored
        );
    if (status->hdd_ready) {
        (void)copy_path(status->hdd, path, &ignored);
        status->hdd_ready = gdox_storage_file_size(path, &hdd_bytes)
            && hdd_bytes != 0U;
    }
    free(configuration);
    return true;
}

typedef struct runtime_bundle_paths {
    char configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char hdd[GDOX_EMULATOR_PATH_CAPACITY];
    char mcpx[GDOX_EMULATOR_PATH_CAPACITY];
    char flash[GDOX_EMULATOR_PATH_CAPACITY];
} runtime_bundle_paths;

typedef enum hdd_preparation {
    HDD_PREPARATION_FAILED = 0,
    HDD_PREPARATION_READY,
    HDD_PREPARATION_USE_EXTERNAL_CONFIGURATION
} hdd_preparation;

static bool select_executable(
    const char *override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    status->custom_executable = override != NULL && override[0] != '\0';
    if (status->custom_executable) {
        status->xemu_available =
            gdox_emulator_validate_executable(override, error)
            && copy_path(status->executable, override, error);
        return status->xemu_available;
    }
    if (gdox_emulator_discover_executable(status->executable, error)) {
        status->xemu_available = true;
        return true;
    }
    if (error->code == GDOX_ERROR_NOT_FOUND) {
        gdox_error_clear(error);
        return true;
    }
    return false;
}

static bool resolve_managed_paths(
    runtime_bundle_paths *paths,
    gdox_error *error
)
{
    return managed_path("xemu/xemu.toml", paths->configuration, error)
        && managed_path("xemu/xbox_hdd.qcow2", paths->hdd, error)
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
    return gdox_emulator_discover_executable(default_executable, error)
        && strcmp(default_executable, executable) != 0
        && find_hdd_template(default_executable, hdd_template);
}

static hdd_preparation prepare_hdd(
    const gdox_runtime_bundle_status *status,
    const runtime_bundle_paths *paths,
    const char *active_hdd,
    gdox_error *error
)
{
    char hdd_template[GDOX_EMULATOR_PATH_CAPACITY];
    uint64_t hdd_bytes;

    if (gdox_storage_file_size(active_hdd, &hdd_bytes)) {
        if (hdd_bytes != 0U) {
            return HDD_PREPARATION_READY;
        }
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            status->custom_hdd
                ? "selected Xbox hard disk is empty"
                : "managed xemu hard disk is empty"
        );
        return HDD_PREPARATION_FAILED;
    }
    if (status->custom_hdd) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "selected Xbox hard disk is unavailable"
        );
        return HDD_PREPARATION_FAILED;
    }
    if (!find_hdd_for_executable(status->executable, hdd_template, error)) {
        gdox_error_clear(error);
        return HDD_PREPARATION_USE_EXTERNAL_CONFIGURATION;
    }
    return gdox_storage_copy_private(
        hdd_template,
        paths->hdd,
        false,
        error
    ) ? HDD_PREPARATION_READY : HDD_PREPARATION_FAILED;
}

static char *duplicate_configuration(
    const char *configuration,
    gdox_error *error
)
{
    const size_t bytes = strlen(configuration) + 1U;
    char *copy = malloc(bytes);

    if (copy == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not retain xemu configuration"
        );
        return NULL;
    }
    memcpy(copy, configuration, bytes);
    return copy;
}

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
        && copy_path(status->hdd, active_hdd, error);
}

static bool prepare_configuration(
    gdox_runtime_bundle_status *status,
    const runtime_bundle_paths *paths,
    const char *active_hdd,
    gdox_error *error
)
{
    char *configuration = NULL;
    char *original = NULL;
    bool managed_found = false;
    bool success = false;

    if (!read_configuration(
            paths->configuration,
            &configuration,
            &managed_found,
            error
        ) || !adopt_configured_firmware(
            GDOX_FIRMWARE_MCPX,
            "bootrom_path",
            configuration,
            paths->mcpx,
            &status->mcpx_ready,
            error
        ) || !adopt_configured_firmware(
            GDOX_FIRMWARE_FLASH,
            "flashrom_path",
            configuration,
            paths->flash,
            &status->flash_ready,
            error
        )) {
        goto cleanup;
    }
    original = duplicate_configuration(configuration, error);
    if (original == NULL
        || !configure_runtime_files(
            &configuration,
            paths,
            active_hdd,
            status,
            error
        )) {
        goto cleanup;
    }
    if ((!managed_found || strcmp(configuration, original) != 0)
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
    free(original);
    free(configuration);
    return success;
}

bool gdox_runtime_bundle_prepare(
    const char *executable_override,
    const char *hdd_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    runtime_bundle_paths paths;
    const char *active_hdd;
    hdd_preparation hdd;

    gdox_error_clear(error);
    if (status == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "runtime bundle status is required");
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->custom_hdd =
        hdd_override != NULL && hdd_override[0] != '\0';
    if (!select_executable(executable_override, status, error)) {
        return false;
    }
    if (!status->xemu_available) {
        return true;
    }
    if (!resolve_managed_paths(&paths, error)) {
        return false;
    }
    active_hdd = status->custom_hdd
        ? hdd_override
        : paths.hdd;
    hdd = prepare_hdd(status, &paths, active_hdd, error);
    if (hdd == HDD_PREPARATION_FAILED) {
        return false;
    }
    if (hdd == HDD_PREPARATION_USE_EXTERNAL_CONFIGURATION) {
        return inspect_external_configuration(status, error);
    }
    status->bundled = !status->custom_executable;
    status->hdd_ready = true;
    return prepare_configuration(status, &paths, active_hdd, error);
}

bool gdox_runtime_bundle_import_firmware(
    gdox_firmware_kind kind,
    const char *source,
    const char *executable_override,
    const char *hdd_override,
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
        hdd_override,
        status,
        error
    );
}

bool gdox_runtime_bundle_import_firmware_auto(
    const char *source,
    const char *executable_override,
    const char *hdd_override,
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
            hdd_override,
            status,
            error
        )) {
        return false;
    }
    *kind = detected;
    return true;
}
