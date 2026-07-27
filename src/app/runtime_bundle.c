#include "app/runtime_bundle.h"

#include "gdox/hash.h"
#include "platform/emulator_configuration.h"
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

bool gdox_runtime_bundle_prepare(
    const char *executable_override,
    const char *hdd_override,
    gdox_runtime_bundle_status *status,
    gdox_error *error
)
{
    char hdd_template[GDOX_EMULATOR_PATH_CAPACITY];
    char default_executable[GDOX_EMULATOR_PATH_CAPACITY];
    char managed_configuration[GDOX_EMULATOR_PATH_CAPACITY];
    char managed_hdd[GDOX_EMULATOR_PATH_CAPACITY];
    char managed_mcpx[GDOX_EMULATOR_PATH_CAPACITY];
    char managed_flash[GDOX_EMULATOR_PATH_CAPACITY];
    const char *active_hdd;
    char *configuration = NULL;
    char *original_configuration = NULL;
    bool managed_found = false;
    bool managed_hdd_found;
    uint64_t hdd_bytes;
    bool success = false;

    gdox_error_clear(error);
    if (status == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "runtime bundle status is required");
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->custom_executable =
        executable_override != NULL && executable_override[0] != '\0';
    status->custom_hdd =
        hdd_override != NULL && hdd_override[0] != '\0';
    if (status->custom_executable) {
        if (!gdox_emulator_validate_executable(executable_override, error)
            || !copy_path(status->executable, executable_override, error)) {
            return false;
        }
    } else if (!gdox_emulator_discover_executable(status->executable, error)) {
        if (error->code == GDOX_ERROR_NOT_FOUND) {
            gdox_error_clear(error);
            return true;
        }
        return false;
    }
    status->xemu_available = true;
    if (!managed_path(
            "xemu/xemu.toml",
            managed_configuration,
            error
        ) || !managed_path(
            "xemu/xbox_hdd.qcow2",
            managed_hdd,
            error
        ) || !managed_path(
            "xemu/firmware/mcpx_1.0.bin",
            managed_mcpx,
            error
        ) || !managed_path(
            "xemu/firmware/bios.bin",
            managed_flash,
            error
        )) {
        return false;
    }
    active_hdd = status->custom_hdd
        ? hdd_override
        : managed_hdd;
    managed_hdd_found = gdox_storage_file_size(active_hdd, &hdd_bytes);
    if (managed_hdd_found && hdd_bytes == 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            status->custom_hdd
                ? "selected Xbox hard disk is empty"
                : "managed xemu hard disk is empty"
        );
        return false;
    }
    if (!managed_hdd_found) {
        if (status->custom_hdd) {
            gdox_error_set(
                error,
                GDOX_ERROR_NOT_FOUND,
                "selected Xbox hard disk is unavailable"
            );
            return false;
        }
        const bool selected_template =
            find_hdd_template(status->executable, hdd_template);
        const bool default_template =
            !selected_template
            && gdox_emulator_discover_executable(default_executable, error)
            && strcmp(default_executable, status->executable) != 0
            && find_hdd_template(default_executable, hdd_template);
        if (!selected_template && !default_template) {
            gdox_error_clear(error);
            return inspect_external_configuration(status, error);
        }
        if (!gdox_storage_copy_private(
                hdd_template,
                managed_hdd,
                false,
                error
            )) {
            return false;
        }
    }
    status->bundled = !status->custom_executable;
    status->hdd_ready = true;
    if (!read_configuration(
            managed_configuration,
            &configuration,
            &managed_found,
            error
        ) || !adopt_configured_firmware(
            GDOX_FIRMWARE_MCPX,
            "bootrom_path",
            configuration,
            managed_mcpx,
            &status->mcpx_ready,
            error
        ) || !adopt_configured_firmware(
            GDOX_FIRMWARE_FLASH,
            "flashrom_path",
            configuration,
            managed_flash,
            &status->flash_ready,
            error
        )) {
        goto cleanup;
    }
    original_configuration = malloc(strlen(configuration) + 1U);
    if (original_configuration == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not retain xemu configuration");
        goto cleanup;
    }
    memcpy(
        original_configuration,
        configuration,
        strlen(configuration) + 1U
    );
    if (!set_file_if_ready(
            &configuration,
            "hdd_path",
            active_hdd,
            true,
            error
        )) {
        goto cleanup;
    }
    if (!set_file_if_ready(
            &configuration,
            "bootrom_path",
            managed_mcpx,
            status->mcpx_ready,
            error
        ) || !set_file_if_ready(
            &configuration,
            "flashrom_path",
            managed_flash,
            status->flash_ready,
            error
        )) {
        goto cleanup;
    }
    if ((!managed_found
            || strcmp(configuration, original_configuration) != 0)
        && !gdox_storage_write_private(
            managed_configuration,
            (const uint8_t *)configuration,
            strlen(configuration),
            true,
            error
        )) {
        goto cleanup;
    }
    if (!copy_path(
            status->configuration,
            managed_configuration,
            error
        ) || !copy_path(status->mcpx, managed_mcpx, error)
        || !copy_path(status->flash, managed_flash, error)
        || !copy_path(status->hdd, active_hdd, error)) {
        goto cleanup;
    }
    status->configuration_ready = true;
    success = true;

cleanup:
    free(original_configuration);
    free(configuration);
    return success;
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
