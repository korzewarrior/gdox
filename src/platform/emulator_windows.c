#define WIN32_LEAN_AND_MEAN

#include "gdox/emulator.h"

#include "core/emulator_configuration.h"
#include "platform/portable_sync.h"
#include "platform/windows_support.h"

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
};

typedef struct wide_command {
    wchar_t *text;
    size_t length;
    size_t capacity;
} wide_command;

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

static bool managed_configuration_wide(
    wchar_t output[GDOX_WINDOWS_PATH_CAPACITY]
)
{
    wchar_t base[GDOX_WINDOWS_PATH_CAPACITY];
    const DWORD length = GetEnvironmentVariableW(
        L"APPDATA",
        base,
        GDOX_WINDOWS_PATH_CAPACITY
    );

    return length != 0U && length < GDOX_WINDOWS_PATH_CAPACITY
        && append_wide_path(
            base,
            L"gdox\\gdox\\data\\xemu\\xemu.toml",
            output
        );
}

static bool create_parent_directories_wide(const wchar_t *path)
{
    wchar_t partial[GDOX_WINDOWS_PATH_CAPACITY];
    size_t index;
    const size_t bytes = wcslen(path);

    if (bytes >= GDOX_WINDOWS_PATH_CAPACITY) {
        return false;
    }
    memcpy(partial, path, (bytes + 1U) * sizeof(wchar_t));
    for (index = 3U; index < bytes; ++index) {
        if (partial[index] != L'\\') {
            continue;
        }
        partial[index] = L'\0';
        if (!CreateDirectoryW(partial, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
        partial[index] = L'\\';
    }
    return true;
}

/*
 * GDOX never edits an external xemu installation's own configuration. The
 * first time one is discovered, its configuration is copied into GDOX's
 * managed location and all later updates apply to the copy.
 */
static bool adopt_external_configuration(
    const wchar_t *external,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    wchar_t managed[GDOX_WINDOWS_PATH_CAPACITY];

    return managed_configuration_wide(managed)
        && create_parent_directories_wide(managed)
        && CopyFileW(external, managed, TRUE)
        && wide_to_utf8(managed, output);
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
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    wchar_t base[GDOX_WINDOWS_PATH_CAPACITY];
    wchar_t external[GDOX_WINDOWS_PATH_CAPACITY];
    DWORD length;

    if (environment_path(L"GDOX_XEMU_CONFIG", output)) {
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
            L"gdox\\gdox\\data\\xemu\\xemu.toml",
            output
        )) {
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
        && adopt_external_configuration(external, output);
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

bool gdox_emulator_discover(gdox_emulator_paths *paths, gdox_error *error)
{
    gdox_error_clear(error);
    if (paths == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator path output is required");
        return false;
    }
    memset(paths, 0, sizeof(*paths));
    if (!gdox_emulator_discover_executable(paths->executable, error)) {
        return false;
    }
    if (!find_configuration(paths->executable, paths->configuration)) {
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
    bool success = false;

    gdox_error_clear(error);
    if (options == NULL || !regular_file(options->executable)
        || !regular_file(options->configuration)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid xemu paths are required");
        return false;
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

static bool command_reserve(
    wide_command *command,
    size_t additional,
    gdox_error *error
)
{
    size_t needed;
    size_t capacity;
    wchar_t *resized;
    if (additional > SIZE_MAX - command->length - 1U) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "xemu command line is too long");
        return false;
    }
    needed = command->length + additional + 1U;
    if (needed <= command->capacity) {
        return true;
    }
    capacity = command->capacity == 0U ? 1024U : command->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    resized = realloc(command->text, capacity * sizeof(*resized));
    if (resized == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate xemu command line");
        return false;
    }
    command->text = resized;
    command->capacity = capacity;
    return true;
}

static bool command_character(
    wide_command *command,
    wchar_t character,
    gdox_error *error
)
{
    if (!command_reserve(command, 1U, error)) {
        return false;
    }
    command->text[command->length++] = character;
    command->text[command->length] = L'\0';
    return true;
}

static bool command_repeat(
    wide_command *command,
    wchar_t character,
    size_t count,
    gdox_error *error
)
{
    if (!command_reserve(command, count, error)) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        command->text[command->length++] = character;
    }
    command->text[command->length] = L'\0';
    return true;
}

static bool command_backslash_run(
    wide_command *command,
    size_t count,
    bool before_quote,
    bool closes_argument,
    gdox_error *error
)
{
    size_t output_count = count;

    if (before_quote || closes_argument) {
        const size_t extra = before_quote ? 1U : 0U;
        if (count > (SIZE_MAX - extra) / 2U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "xemu command line is too long"
            );
            return false;
        }
        output_count = count * 2U + extra;
    }
    return command_repeat(command, L'\\', output_count, error);
}

static bool command_argument(
    wide_command *command,
    const wchar_t *argument,
    gdox_error *error
)
{
    const wchar_t *cursor = argument;
    size_t backslashes = 0U;

    if (command->length != 0U
        && !command_character(command, L' ', error)) {
        return false;
    }
    if (!command_character(command, L'"', error)) {
        return false;
    }
    while (*cursor != L'\0') {
        if (*cursor == L'\\') {
            ++backslashes;
            ++cursor;
            continue;
        }
        if (!command_backslash_run(
                command,
                backslashes,
                *cursor == L'"',
                false,
                error
            )) {
            return false;
        }
        backslashes = 0U;
        if (!command_character(command, *cursor++, error)) {
            return false;
        }
    }
    return command_backslash_run(
        command,
        backslashes,
        false,
        true,
        error
    )
        && command_character(command, L'"', error);
}

static bool append_utf8_argument(
    wide_command *command,
    const char *argument,
    gdox_error *error
)
{
    wchar_t *wide = gdox_windows_wide_path(argument, error);
    bool success;
    if (wide == NULL) {
        return false;
    }
    success = command_argument(command, wide, error);
    free(wide);
    return success;
}

bool gdox_emulator_launch(
    const gdox_emulator_options *options,
    const char *dvd_uri,
    gdox_emulator_process **process_output,
    gdox_error *error
)
{
    gdox_emulator_process *process;
    wchar_t *executable;
    wide_command command = {0};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION information = {0};
    HANDLE null_output = INVALID_HANDLE_VALUE;
    DWORD creation_flags = 0U;
    bool success;

    gdox_error_clear(error);
    if (options == NULL || dvd_uri == NULL || dvd_uri[0] == '\0'
        || process_output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "emulator options, disc URI, and process output are required");
        return false;
    }
    *process_output = NULL;
    if (!gdox_emulator_prepare(options, error)) {
        return false;
    }
    executable = gdox_windows_wide_path(options->executable, error);
    if (executable == NULL) {
        return false;
    }
    success = command_argument(&command, executable, error)
        && command_argument(&command, L"-config_path", error)
        && append_utf8_argument(&command, options->configuration, error)
        && (!options->fullscreen
            || command_argument(&command, L"-full-screen", error))
        && command_argument(&command, L"-dvd_path", error)
        && append_utf8_argument(&command, dvd_uri, error);
    if (!success) {
        free(executable);
        free(command.text);
        return false;
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
            free(executable);
            free(command.text);
            gdox_windows_io_error(error, "could not open null output for xemu", GetLastError());
            return false;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = null_output;
        startup.hStdError = null_output;
        creation_flags = CREATE_NO_WINDOW;
    }
    if (!CreateProcessW(
            executable,
            command.text,
            NULL,
            NULL,
            options->console_output ? FALSE : TRUE,
            creation_flags,
            NULL,
            NULL,
            &startup,
            &information
        )) {
        const DWORD code = GetLastError();
        if (null_output != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(null_output);
        }
        free(executable);
        free(command.text);
        gdox_windows_io_error(error, "could not launch xemu", code);
        return false;
    }
    if (null_output != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(null_output);
    }
    (void)CloseHandle(information.hThread);
    free(executable);
    free(command.text);
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        (void)TerminateProcess(information.hProcess, 1U);
        (void)CloseHandle(information.hProcess);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate emulator process");
        return false;
    }
    process->handle = information.hProcess;
    process->identifier = information.dwProcessId;
    *process_output = process;
    return true;
}

bool gdox_emulator_poll(
    gdox_emulator_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    DWORD code;
    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and status outputs are required");
        return false;
    }
    if (process->reaped) {
        *running = false;
        *exit_code = process->exit_code;
        return true;
    }
    if (!GetExitCodeProcess(process->handle, &code)) {
        gdox_windows_io_error(error, "could not query xemu process", GetLastError());
        return false;
    }
    if (code == STILL_ACTIVE) {
        *running = true;
        *exit_code = 0;
        return true;
    }
    process->reaped = true;
    process->exit_code = (int)code;
    *running = false;
    *exit_code = process->exit_code;
    return true;
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

bool gdox_emulator_stop(
    gdox_emulator_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    uint32_t waited = 0U;
    bool running;

    gdox_error_clear(error);
    if (process == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "process and exit code are required");
        return false;
    }
    if (!gdox_emulator_poll(process, &running, exit_code, error) || !running) {
        return !gdox_error_is_set(error);
    }
    (void)EnumWindows(request_window_close, (LPARAM)process->identifier);
    while (waited < grace_ms) {
        gdox_sleep_ms(50U);
        waited += 50U;
        if (!gdox_emulator_poll(process, &running, exit_code, error)
            || !running) {
            return !gdox_error_is_set(error);
        }
    }
    if (!TerminateProcess(process->handle, 1U)) {
        gdox_windows_io_error(error, "could not stop xemu", GetLastError());
        return false;
    }
    if (WaitForSingleObject(process->handle, 5000U) != WAIT_OBJECT_0
        || !gdox_emulator_poll(process, &running, exit_code, error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_IO, "xemu did not stop");
        }
        return false;
    }
    return !running;
}

void gdox_emulator_process_destroy(gdox_emulator_process *process)
{
    if (process != NULL) {
        int exit_code;
        gdox_error ignored;
        if (!process->reaped) {
            (void)gdox_emulator_stop(process, 1000U, &exit_code, &ignored);
        }
        (void)CloseHandle(process->handle);
        free(process);
    }
}
