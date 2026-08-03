#define WIN32_LEAN_AND_MEAN

#include "gdox/xenia.h"

#include "platform/windows_support.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_PATH_CAPACITY 32768U

static bool wide_regular_file(const wchar_t *path)
{
    const DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

static bool regular_file(const char *path)
{
    gdox_error error;
    wchar_t *wide = gdox_windows_wide_path(path, &error);
    bool result;

    if (wide == NULL) {
        return false;
    }
    result = wide_regular_file(wide);
    free(wide);
    return result;
}

static bool wide_to_utf8(
    const wchar_t *wide,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    return WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide,
        -1,
        output,
        (int)GDOX_EMULATOR_PATH_CAPACITY,
        NULL,
        NULL
    ) > 0;
}

static bool module_directory(wchar_t output[GDOX_WINDOWS_PATH_CAPACITY])
{
    const DWORD length = GetModuleFileNameW(
        NULL,
        output,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    wchar_t *slash;

    if (length == 0U || length >= GDOX_WINDOWS_PATH_CAPACITY) {
        return false;
    }
    slash = wcsrchr(output, L'\\');
    if (slash == NULL) {
        return false;
    }
    *slash = L'\0';
    return true;
}

static bool append_wide_path(
    const wchar_t *base,
    const wchar_t *suffix,
    wchar_t output[GDOX_WINDOWS_PATH_CAPACITY]
)
{
    const int written = swprintf(
        output,
        GDOX_WINDOWS_PATH_CAPACITY,
        L"%ls\\%ls",
        base,
        suffix
    );

    return written >= 0
        && (size_t)written < GDOX_WINDOWS_PATH_CAPACITY;
}

static bool runtime_candidate(
    const wchar_t *root,
    const gdox_xenia_runtime *runtime,
    gdox_xenia_runtime_origin origin,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    wchar_t revision[48];
    wchar_t payload_name[64];
    wchar_t suffix[96];
    wchar_t executable[GDOX_WINDOWS_PATH_CAPACITY];
    int converted;
    int written;

    converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        runtime->revision,
        -1,
        revision,
        (int)(sizeof(revision) / sizeof(revision[0]))
    );
    if (converted > 0) {
        converted = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            runtime->payload_name,
            -1,
            payload_name,
            (int)(sizeof(payload_name) / sizeof(payload_name[0]))
        );
    }
    written = converted > 0
        ? swprintf(
            suffix,
            sizeof(suffix) / sizeof(suffix[0]),
            L"xenia\\%ls\\%ls",
            revision,
            payload_name
        )
        : -1;
    if (written < 0 || (size_t)written >= sizeof(suffix) / sizeof(suffix[0])
        || !append_wide_path(root, suffix, executable)
        || !wide_regular_file(executable)
        || !wide_to_utf8(executable, output->payload)
        || !gdox_xenia_verify_payload(output->payload, runtime, error)) {
        return false;
    }
    memcpy(
        output->launcher,
        output->payload,
        strlen(output->payload) + 1U
    );
    output->definition = runtime;
    output->origin = origin;
    return true;
}

static bool override_candidate(
    const char *override,
    const gdox_xenia_runtime *runtime,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    const size_t bytes = override != NULL ? strlen(override) : 0U;

    if (bytes == 0U || bytes >= sizeof(output->payload)
        || !regular_file(override)
        || !gdox_xenia_verify_payload(override, runtime, error)) {
        return false;
    }
    memcpy(output->payload, override, bytes + 1U);
    memcpy(output->launcher, override, bytes + 1U);
    output->definition = runtime;
    output->origin = GDOX_XENIA_RUNTIME_OVERRIDE;
    return true;
}

bool gdox_xenia_resolve_runtime(
    const gdox_xenia_runtime *runtime,
    const char *override,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
)
{
    wchar_t root[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t candidate_root[GDOX_WINDOWS_PATH_CAPACITY];
    DWORD length;

    gdox_error_clear(error);
    if (output != NULL) {
        memset(output, 0, sizeof(*output));
    }
    if (runtime == NULL || output == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "reviewed Xenia runtime and descriptor output are required"
        );
        return false;
    }
    if (override != NULL && override[0] != '\0') {
        if (override_candidate(override, runtime, output, error)) {
            return true;
        }
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "selected Xenia executable does not match the reviewed runtime"
            );
        }
        return false;
    }
    length = GetEnvironmentVariableW(
        L"GDOX_RUNTIME_DIR",
        root,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && runtime_candidate(
            root,
            runtime,
            GDOX_XENIA_RUNTIME_BUNDLED,
            output,
            error
        )) {
        return true;
    }
    gdox_error_clear(error);
    if (module_directory(root)
        && append_wide_path(root, L"runtime", candidate_root)
        && runtime_candidate(
            candidate_root,
            runtime,
            GDOX_XENIA_RUNTIME_BUNDLED,
            output,
            error
        )) {
        return true;
    }
    gdox_error_clear(error);
    length = GetEnvironmentVariableW(
        L"APPDATA",
        root,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && append_wide_path(
            root,
            L"gdox\\gdox\\data\\runtime",
            candidate_root
        ) && runtime_candidate(
            candidate_root,
            runtime,
            GDOX_XENIA_RUNTIME_BUNDLED,
            output,
            error
        )) {
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        "verified Xenia runtime was not found"
    );
    return false;
}

bool gdox_xenia_target_supported(gdox_xenia_target_kind kind)
{
    return kind == GDOX_XENIA_TARGET_IMAGE
        || kind == GDOX_XENIA_TARGET_PRIVATE_NBD;
}

bool gdox_xenia_runtime_target_supported(
    const gdox_xenia_runtime *runtime,
    gdox_xenia_target_kind kind
)
{
    return runtime != NULL && gdox_xenia_target_supported(kind)
        && (kind != GDOX_XENIA_TARGET_PRIVATE_NBD
            || runtime->supports_private_nbd);
}

bool gdox_xenia_target_preflight(
    gdox_xenia_target_kind kind,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (kind == GDOX_XENIA_TARGET_IMAGE) {
        return true;
    }
    if (kind == GDOX_XENIA_TARGET_PRIVATE_NBD) {
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_ARGUMENT,
        "Xenia target kind is invalid"
    );
    return false;
}
