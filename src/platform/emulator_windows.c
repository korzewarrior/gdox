#define WIN32_LEAN_AND_MEAN

#include "gdox/emulator.h"

#include "core/emulator_configuration.h"
#include "platform/portable_sync.h"
#include "platform/session_storage.h"
#include "platform/windows_command.h"
#include "platform/windows_support.h"
#include "platform/xemu_runtime_session.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_PATH_CAPACITY 32768U
#define GDOX_CONFIGURATION_MAX_BYTES (UINT64_C(16) * 1024U * 1024U)

struct gdox_emulator_process {
    HANDLE handle;
    DWORD identifier;
    bool reaped;
    int exit_code;
    gdox_session_storage session;
};

static bool cleanup_process_session(
    gdox_emulator_process *process,
    gdox_error *error
)
{
    return !process->session.active
        || gdox_session_storage_cleanup(&process->session, error);
}

static bool wide_regular_file(const wchar_t *path)
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

static bool wide_directory(const wchar_t *path)
{
    const DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

static bool wide_absolute_path(const wchar_t *path)
{
    const size_t characters = path != NULL ? wcslen(path) : 0U;

    return (characters >= 3U
            && ((path[0] >= L'A' && path[0] <= L'Z')
                || (path[0] >= L'a' && path[0] <= L'z'))
            && path[1] == L':'
            && (path[2] == L'\\' || path[2] == L'/'))
        || (characters >= 2U && path[0] == L'\\' && path[1] == L'\\');
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
    const int capacity = (int)GDOX_EMULATOR_PATH_CAPACITY;
    return WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide,
        -1,
        output,
        capacity,
        NULL,
        NULL
    ) > 0;
}

static bool environment_path(
    const wchar_t *name,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    wchar_t value[GDOX_WINDOWS_PATH_CAPACITY];
    const DWORD length = GetEnvironmentVariableW(
        name,
        value,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length == 0U || length >= GDOX_WINDOWS_PATH_CAPACITY
        || !wide_regular_file(value)) {
        return false;
    }
    return wide_to_utf8(value, output);
}

static bool environment_value(
    const wchar_t *name,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    wchar_t value[GDOX_WINDOWS_PATH_CAPACITY];
    const DWORD length = GetEnvironmentVariableW(
        name,
        value,
        GDOX_WINDOWS_PATH_CAPACITY
    );

    return length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && wide_to_utf8(value, output);
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

static bool candidate(
    const wchar_t *base,
    const wchar_t *suffix,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    wchar_t path[GDOX_WINDOWS_PATH_CAPACITY];
    return append_wide_path(base, suffix, path)
        && wide_regular_file(path)
        && wide_to_utf8(path, output);
}

static bool find_xemu(char output[GDOX_EMULATOR_PATH_CAPACITY])
{
    wchar_t base[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t path[GDOX_WINDOWS_PATH_CAPACITY];
    DWORD length;

    if (environment_path(L"GDOX_XEMU", output)) {
        return true;
    }
    length = GetEnvironmentVariableW(
        L"GDOX_RUNTIME_DIR",
        base,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && candidate(base, L"xemu\\xemu.exe", output)) {
        return true;
    }
    if (module_directory(base)
        && (candidate(base, L"runtime\\xemu\\xemu.exe", output)
            || candidate(base, L"xemu.exe", output))) {
        return true;
    }
    length = GetEnvironmentVariableW(
        L"APPDATA",
        base,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && candidate(
            base,
            L"gdox\\gdox\\data\\runtime\\xemu\\xemu.exe",
            output
        )) {
        return true;
    }
    length = SearchPathW(
        NULL,
        L"xemu.exe",
        NULL,
        GDOX_WINDOWS_PATH_CAPACITY,
        path,
        NULL
    );
    return length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && wide_regular_file(path) && wide_to_utf8(path, output);
}

static bool external_configuration(
    const char *executable,
    wchar_t output[GDOX_WINDOWS_PATH_CAPACITY]
)
{
    gdox_error error;
    wchar_t base[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t *wide = gdox_windows_wide_path(executable, &error);
    DWORD length;
    bool found = false;

    if (wide != NULL) {
        wchar_t *slash = wcsrchr(wide, L'\\');
        if (slash != NULL) {
            *slash = L'\0';
            found = append_wide_path(wide, L"xemu.toml", output)
                && wide_regular_file(output);
        }
        free(wide);
    }
    if (found) {
        return true;
    }
    length = GetEnvironmentVariableW(
        L"APPDATA",
        base,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    return length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && append_wide_path(base, L"xemu\\xemu\\xemu.toml", output)
        && wide_regular_file(output);
}

static bool find_configuration(
    const char *executable,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    bool *required
)
{
    wchar_t base[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t external[GDOX_WINDOWS_PATH_CAPACITY];
    DWORD length;

    *required = environment_value(L"GDOX_XEMU_CONFIG", output);
    if (*required) {
        return true;
    }
    length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        base,
        GDOX_WINDOWS_PATH_CAPACITY
    );
    if (length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && candidate(base, L"GDOX\\xemu\\xemu.toml", output)) {
        return true;
    }
    return external_configuration(executable, external)
        && wide_to_utf8(external, output);
}

bool gdox_emulator_discover_executable(
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator path output is required");
        return false;
    }
    output[0] = '\0';
    if (!find_xemu(output)) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "xemu executable was not found");
        return false;
    }
    return true;
}

bool gdox_emulator_validate_executable(
    const char *path,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (!regular_file(path)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "selected xemu executable is not a runnable file"
        );
        return false;
    }
    return true;
}

bool gdox_emulator_discover_configuration(
    const char *executable,
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    bool *required,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (output == NULL || required == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "emulator configuration outputs are required"
        );
        return false;
    }
    output[0] = '\0';
    *required = false;
    if (!find_configuration(executable, output, required)) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "xemu configuration was not found");
        return false;
    }
    return true;
}

static bool read_text_file(
    const char *path,
    char **text,
    gdox_error *error
)
{
    wchar_t *wide = gdox_windows_wide_path(path, error);
    HANDLE file;
    LARGE_INTEGER size;
    char *data;
    size_t completed = 0U;

    if (wide == NULL) {
        return false;
    }
    file = CreateFileW(
        wide,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    free(wide);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)
        || size.QuadPart < 0
        || (uint64_t)size.QuadPart > GDOX_CONFIGURATION_MAX_BYTES) {
        const DWORD code = GetLastError();
        if (file != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(file);
        }
        gdox_windows_io_error(error, "could not open xemu configuration", code);
        return false;
    }
    data = malloc((size_t)size.QuadPart + 1U);
    if (data == NULL) {
        (void)CloseHandle(file);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate xemu configuration");
        return false;
    }
    while (completed < (size_t)size.QuadPart) {
        const size_t remaining = (size_t)size.QuadPart - completed;
        const DWORD request =
            remaining > UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        DWORD received = 0U;
        if (!ReadFile(file, data + completed, request, &received, NULL)
            || received == 0U) {
            const DWORD code = GetLastError();
            (void)CloseHandle(file);
            free(data);
            gdox_windows_io_error(error, "could not read xemu configuration", code);
            return false;
        }
        completed += received;
    }
    (void)CloseHandle(file);
    data[completed] = '\0';
    *text = data;
    return true;
}

static bool write_text_file(
    HANDLE file,
    const char *text,
    gdox_error *error
)
{
    const size_t bytes = strlen(text);
    size_t completed = 0U;
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const DWORD request =
            remaining > UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        DWORD written = 0U;
        if (!WriteFile(file, text + completed, request, &written, NULL)
            || written != request) {
            gdox_windows_io_error(error, "could not write xemu configuration", GetLastError());
            return false;
        }
        completed += written;
    }
    return true;
}

static bool write_private_atomic(
    const char *path,
    const char *text,
    gdox_error *error
)
{
    wchar_t *wide = gdox_windows_wide_path(path, error);
    wchar_t temporary[GDOX_WINDOWS_PATH_CAPACITY];
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD attempt;
    bool success;

    if (wide == NULL) {
        return false;
    }
    for (attempt = 0U; attempt < 32U; ++attempt) {
        const int written = swprintf(
            temporary,
            GDOX_WINDOWS_PATH_CAPACITY,
            L"%ls.gdox.%lu.%lu.tmp",
            wide,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long)(GetTickCount64() + attempt)
        );
        if (written < 0
            || (size_t)written >= GDOX_WINDOWS_PATH_CAPACITY) {
            free(wide);
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "xemu configuration path is too long");
            return false;
        }
        file = CreateFileW(
            temporary,
            GENERIC_WRITE,
            0U,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            NULL
        );
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) {
            break;
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        free(wide);
        gdox_windows_io_error(error, "could not create xemu configuration update", GetLastError());
        return false;
    }
    success = write_text_file(file, text, error);
    if (success && !FlushFileBuffers(file)) {
        gdox_windows_io_error(error, "could not synchronize xemu configuration", GetLastError());
        success = false;
    }
    if (!CloseHandle(file) && success) {
        gdox_windows_io_error(error, "could not close xemu configuration", GetLastError());
        success = false;
    }
    if (success && !MoveFileExW(
            temporary,
            wide,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
        gdox_windows_io_error(error, "could not commit xemu configuration", GetLastError());
        success = false;
    }
    if (!success) {
        (void)DeleteFileW(temporary);
    }
    free(wide);
    return success;
}

bool gdox_emulator_prepare(
    const gdox_emulator_options *options,
    gdox_error *error
)
{
    char *original = NULL;
    char *updated = NULL;
    bool persistent_save_export;
    bool success = false;

    gdox_error_clear(error);
    if (options == NULL || !regular_file(options->executable)
        || !regular_file(options->configuration)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid xemu paths are required");
        return false;
    }
    if (!gdox_emulator_query_storage_capabilities(
            options->executable, &persistent_save_export, error
        )) {
        goto cleanup;
    }
    if (!persistent_save_export) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "xemu does not provide persistent logical save export"
        );
        goto cleanup;
    }
    if (!read_text_file(options->configuration, &original, error)
        || !gdox_emulator_configuration_update(
            options,
            original,
            &updated,
            error
        )) {
        goto cleanup;
    }
    if (strcmp(original, updated) != 0
        && !write_private_atomic(options->configuration, updated, error)) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(original);
    free(updated);
    return success;
}

bool gdox_emulator_launch(
    const gdox_emulator_options *options,
    const char *dvd_uri,
    gdox_emulator_process **process_output,
    gdox_error *error
)
{
    gdox_emulator_process *process = NULL;
    wchar_t *executable = NULL;
    wchar_t *configuration = NULL;
    wchar_t *save_vault = NULL;
    wchar_t *session_directory = NULL;
    wchar_t executable_path[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t configuration_path[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t save_vault_path[GDOX_WINDOWS_PATH_CAPACITY];
    gdox_windows_command command = {0};
    gdox_xemu_environment environment = {0};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION information = {0};
    HANDLE null_output = INVALID_HANDLE_VALUE;
    DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;
    bool success = false;
    DWORD characters;

    gdox_error_clear(error);
    if (options == NULL || options->save_vault == NULL
        || options->save_vault[0] == '\0'
        || dvd_uri == NULL || dvd_uri[0] == '\0'
        || process_output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator options, save vault, disc URI, and process output are required");
        return false;
    }
    *process_output = NULL;
    if (!gdox_emulator_prepare(options, error)) {
        return false;
    }
    executable = gdox_windows_wide_path(options->executable, error);
    if (executable == NULL) {
        goto cleanup;
    }
    configuration = gdox_windows_wide_path(options->configuration, error);
    save_vault = gdox_windows_wide_path(options->save_vault, error);
    if (configuration == NULL || save_vault == NULL) {
        goto cleanup;
    }
    characters = executable != NULL ? GetFullPathNameW(
        executable, GDOX_WINDOWS_PATH_CAPACITY, executable_path, NULL
    ) : 0U;
    if (characters == 0U || characters >= GDOX_WINDOWS_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu executable path is unavailable"
        );
        goto cleanup;
    }
    characters = GetFullPathNameW(
        configuration,
        GDOX_WINDOWS_PATH_CAPACITY,
        configuration_path,
        NULL
    );
    if (characters == 0U || characters >= GDOX_WINDOWS_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu configuration path is unavailable"
        );
        goto cleanup;
    }
    if (!wide_absolute_path(save_vault)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu save vault path must be absolute"
        );
        goto cleanup;
    }
    characters = GetFullPathNameW(
        save_vault,
        GDOX_WINDOWS_PATH_CAPACITY,
        save_vault_path,
        NULL
    );
    if (characters == 0U || characters >= GDOX_WINDOWS_PATH_CAPACITY
        || !wide_directory(save_vault_path)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "xemu save vault is unavailable or is not a directory"
        );
        goto cleanup;
    }
    if (!gdox_windows_command_add_wide(&command, executable_path, error)
        || !gdox_windows_command_add_wide(
            &command, L"--gdox-runtime", error
        )
        || !gdox_windows_command_add_wide(
            &command, L"--gdox-save-vault", error
        )
        || !gdox_windows_command_add_wide(
            &command, save_vault_path, error
        )) {
        goto cleanup;
    }
    if (!gdox_windows_command_add_wide(&command, L"-config_path", error)
        || !gdox_windows_command_add_wide(
            &command, configuration_path, error
        )
        || (options->fullscreen
            && !gdox_windows_command_add_wide(
                &command, L"-full-screen", error
            ))
        || !gdox_windows_command_add_wide(&command, L"-dvd_path", error)
        || !gdox_windows_command_add_utf8(&command, dvd_uri, error)) {
        goto cleanup;
    }
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate emulator process"
        );
        goto cleanup;
    }
    if (!gdox_xemu_runtime_session_open(&process->session, error)
        || !gdox_xemu_environment_create(
            process->session.root, &environment, error
        )) {
        goto cleanup;
    }
    session_directory = gdox_windows_wide_path(
        process->session.root, error
    );
    if (session_directory == NULL) {
        goto cleanup;
    }
    startup.cb = sizeof(startup);
    if (!options->console_output) {
        null_output = CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (null_output == INVALID_HANDLE_VALUE) {
            gdox_windows_io_error(error, "could not open null output for xemu", GetLastError());
            goto cleanup;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = null_output;
        startup.hStdError = null_output;
        creation_flags |= CREATE_NO_WINDOW;
    }
    if (!CreateProcessW(
            executable_path,
            command.text,
            NULL,
            NULL,
            options->console_output ? FALSE : TRUE,
            creation_flags,
            environment.block,
            session_directory,
            &startup,
            &information
        )) {
        const DWORD code = GetLastError();
        gdox_windows_io_error(error, "could not launch xemu", code);
        goto cleanup;
    }
    process->handle = information.hProcess;
    process->identifier = information.dwProcessId;
    success = true;
    *process_output = process;

cleanup:
    if (null_output != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(null_output);
    }
    if (information.hThread != NULL) {
        (void)CloseHandle(information.hThread);
    }
    free(executable);
    free(configuration);
    free(save_vault);
    free(session_directory);
    gdox_windows_command_destroy(&command);
    gdox_xemu_environment_destroy(&environment);
    if (!success && process != NULL) {
        gdox_error cleanup_error;

        if (!cleanup_process_session(process, &cleanup_error)) {
            char message[GDOX_ERROR_MESSAGE_CAPACITY];

            (void)snprintf(
                message,
                sizeof(message),
                "%.96s; xemu session cleanup failed: %.96s",
                gdox_error_is_set(error) ? error->message : "xemu launch failed",
                cleanup_error.message
            );
            gdox_error_set(error, GDOX_ERROR_IO, message);
        }
        free(process);
    }
    return success;
}

enum {
    GDOX_EMULATOR_STOP_POLL_MS = 50U,
    GDOX_EMULATOR_FORCED_REAP_MS = 5000U,
};

static void mark_reaped(gdox_emulator_process *process, int exit_code)
{
    process->reaped = true;
    process->exit_code = exit_code;
}

static bool query_process(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    DWORD wait_result;
    DWORD code;

    if (process->reaped) {
        if (!cleanup_process_session(process, error)) {
            return false;
        }
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    wait_result = WaitForSingleObject(process->handle, 0U);
    if (wait_result == WAIT_TIMEOUT) {
        *running = true;
        *exit_code = 0;
        return true;
    }
    if (wait_result == WAIT_FAILED) {
        gdox_windows_io_error(
            error,
            "could not query xemu process",
            GetLastError()
        );
        return false;
    }
    if (wait_result != WAIT_OBJECT_0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "received an unexpected xemu process wait result"
        );
        return false;
    }
    if (!GetExitCodeProcess(process->handle, &code)) {
        gdox_windows_io_error(error, "could not query xemu process", GetLastError());
        return false;
    }
    mark_reaped(process, (int)code);
    if (!cleanup_process_session(process, error)) {
        return false;
    }
    *running = false;
    *exit_code = process->exit_code;
    return true;
}

bool gdox_emulator_poll(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and status outputs are required");
        return false;
    }
    return query_process(process, running, exit_code, error);
}

static BOOL CALLBACK request_window_close(HWND window, LPARAM parameter)
{
    DWORD process_identifier = 0U;
    (void)GetWindowThreadProcessId(window, &process_identifier);
    if (process_identifier == (DWORD)parameter) {
        (void)PostMessageW(window, WM_CLOSE, 0U, 0);
    }
    return TRUE;
}

static void remember_error(gdox_error *saved, const gdox_error *candidate)
{
    if (!gdox_error_is_set(saved) && gdox_error_is_set(candidate)) {
        *saved = *candidate;
    }
}

static bool wait_for_exit(
    gdox_emulator_process *process,
    uint32_t timeout_ms,
    int *exit_code,
    gdox_error *saved_error
)
{
    uint32_t waited = 0U;

    for (;;) {
        gdox_error query_error;
        bool running = true;
        int observed_exit = -1;

        gdox_error_clear(&query_error);
        if (query_process(
                process, &running, &observed_exit, &query_error
            )) {
            if (!running) {
                *exit_code = observed_exit;
                return true;
            }
        } else {
            remember_error(saved_error, &query_error);
        }
        if (waited >= timeout_ms) {
            return false;
        }
        {
            const uint32_t remaining = timeout_ms - waited;
            const uint32_t delay = remaining < GDOX_EMULATOR_STOP_POLL_MS
                ? remaining
                : GDOX_EMULATOR_STOP_POLL_MS;
            const DWORD result = WaitForSingleObject(process->handle, delay);

            if (result == WAIT_OBJECT_0) {
                DWORD code = 1U;

                if (!GetExitCodeProcess(process->handle, &code)) {
                    gdox_windows_io_error(
                        &query_error,
                        "could not read the stopped xemu process status",
                        GetLastError()
                    );
                    remember_error(saved_error, &query_error);
                }
                mark_reaped(process, (int)code);
                *exit_code = process->exit_code;
                return true;
            }
            if (result == WAIT_FAILED) {
                gdox_windows_io_error(
                    &query_error,
                    "could not wait for xemu to stop",
                    GetLastError()
                );
                remember_error(saved_error, &query_error);
                gdox_sleep_ms(delay);
            }
            waited += delay;
        }
    }
}

static bool terminate_owned_process(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error saved_error;

    gdox_error_clear(&saved_error);
    if (wait_for_exit(process, 0U, exit_code, &saved_error)) {
        gdox_error_clear(error);
        return true;
    }
    (void)EnumWindows(request_window_close, (LPARAM)process->identifier);
    if (wait_for_exit(process, grace_ms, exit_code, &saved_error)) {
        gdox_error_clear(error);
        return true;
    }
    if (!TerminateProcess(process->handle, 1U)) {
        gdox_error terminate_error;

        gdox_windows_io_error(
            &terminate_error,
            "could not force xemu to stop",
            GetLastError()
        );
        remember_error(&saved_error, &terminate_error);
    }
    if (wait_for_exit(
            process,
            GDOX_EMULATOR_FORCED_REAP_MS,
            exit_code,
            &saved_error
        )) {
        gdox_error_clear(error);
        return true;
    }
    if (gdox_error_is_set(&saved_error)) {
        *error = saved_error;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "xemu did not exit after forced termination"
        );
    }
    *exit_code = -1;
    return false;
}

static void force_terminal_cleanup(gdox_emulator_process *process)
{
    while (!process->reaped) {
        gdox_error query_error;
        bool running = true;
        int exit_code = -1;

        gdox_error_clear(&query_error);
        if (query_process(
                process, &running, &exit_code, &query_error
            ) && !running) {
            return;
        }
        (void)TerminateProcess(process->handle, 1U);
        if (WaitForSingleObject(process->handle, INFINITE)
            == WAIT_OBJECT_0) {
            DWORD code = 1U;

            (void)GetExitCodeProcess(process->handle, &code);
            mark_reaped(process, (int)code);
            return;
        }
        gdox_sleep_ms(10U);
    }
}

bool gdox_emulator_stop(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and exit code are required");
        return false;
    }
    return terminate_owned_process(process, grace_ms, exit_code, error);
}

void gdox_emulator_process_destroy(gdox_emulator_process *process)
{
    if (process != NULL) {
        int exit_code;
        gdox_error ignored;
        if (!process->reaped
            && !terminate_owned_process(
                process, 1000U, &exit_code, &ignored
            )) {
            force_terminal_cleanup(process);
        }
        (void)cleanup_process_session(process, &ignored);
        (void)CloseHandle(process->handle);
        free(process);
    }
}
