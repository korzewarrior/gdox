#define WIN32_LEAN_AND_MEAN

#include "gdox/xenia.h"

#include "core/xenia_launch.h"
#include "platform/portable_sync.h"
#include "platform/windows_command.h"
#include "platform/windows_support.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct gdox_xenia_process {
    HANDLE handle;
    DWORD identifier;
    bool reaped;
    int exit_code;
};

static void close_valid_handle(HANDLE handle)
{
    if (handle != NULL && handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(handle);
    }
}

bool gdox_xenia_launch(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_process **output,
    gdox_error *error
)
{
    gdox_xenia_process *process;
    gdox_xenia_launch_plan plan;
    gdox_windows_command command = {0};
    wchar_t *executable;
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION information = {0};
    SECURITY_ATTRIBUTES security = {0};
    HANDLE null_input = INVALID_HANDLE_VALUE;
    HANDLE null_output = INVALID_HANDLE_VALUE;
    DWORD creation_flags = 0U;
    size_t index;

    gdox_error_clear(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (options == NULL || options->runtime == NULL
        || options->runtime->definition == NULL || target == NULL
        || target->location == NULL || target->location[0] == '\0'
        || output == NULL) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "verified Xenia launch options and target are required"
            );
        }
        return false;
    }
    if (!gdox_xenia_runtime_target_supported(
            options->runtime->definition, target->kind
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "reviewed Xenia runtime cannot consume the selected media target"
        );
        return false;
    }
    if (!gdox_xenia_target_preflight(target->kind, error)) {
        return false;
    }
    if (!gdox_xenia_verify_payload(
            options->runtime->payload,
            options->runtime->definition,
            error
        ) || !gdox_xenia_build_target_launch_plan(
            options,
            target,
            &plan,
            error
        )) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "Xenia image target is invalid"
            );
        }
        return false;
    }
    executable = gdox_windows_wide_path(options->runtime->launcher, error);
    if (executable == NULL) {
        return false;
    }
    for (index = 0U; index < plan.count; ++index) {
        if (!gdox_windows_command_add_utf8(
                &command,
                plan.arguments[index],
                error
            )) {
            free(executable);
            gdox_windows_command_destroy(&command);
            return false;
        }
    }
    startup.cb = sizeof(startup);
    if (!options->console_output) {
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        null_input = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        null_output = CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (null_input == INVALID_HANDLE_VALUE
            || null_output == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();

            close_valid_handle(null_input);
            close_valid_handle(null_output);
            free(executable);
            gdox_windows_command_destroy(&command);
            gdox_windows_io_error(
                error,
                "could not open null input and output for Xenia",
                code
            );
            return false;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_input;
        startup.hStdOutput = null_output;
        startup.hStdError = null_output;
        creation_flags = CREATE_NO_WINDOW;
    }
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        close_valid_handle(null_input);
        close_valid_handle(null_output);
        free(executable);
        gdox_windows_command_destroy(&command);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Xenia process"
        );
        return false;
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

        close_valid_handle(null_input);
        close_valid_handle(null_output);
        free(process);
        free(executable);
        gdox_windows_command_destroy(&command);
        gdox_windows_io_error(error, "could not launch Xenia", code);
        return false;
    }
    close_valid_handle(null_input);
    close_valid_handle(null_output);
    (void)CloseHandle(information.hThread);
    free(executable);
    gdox_windows_command_destroy(&command);
    process->handle = information.hProcess;
    process->identifier = information.dwProcessId;
    *output = process;
    return true;
}

enum {
    GDOX_XENIA_STOP_POLL_MS = 50U,
    GDOX_XENIA_FORCED_REAP_MS = 5000U,
};

static void mark_reaped(gdox_xenia_process *process, int exit_code)
{
    process->reaped = true;
    process->exit_code = exit_code;
}

static bool query_process(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    DWORD wait_result;
    DWORD code;

    if (process->reaped) {
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
            "could not query Xenia process",
            GetLastError()
        );
        return false;
    }
    if (wait_result != WAIT_OBJECT_0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "received an unexpected Xenia process wait result"
        );
        return false;
    }
    if (!GetExitCodeProcess(process->handle, &code)) {
        gdox_windows_io_error(
            error,
            "could not query Xenia process",
            GetLastError()
        );
        return false;
    }
    mark_reaped(process, (int)code);
    *running = false;
    *exit_code = process->exit_code;
    return true;
}

bool gdox_xenia_poll(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia process and status outputs are required"
        );
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
    gdox_xenia_process *process,
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
            const uint32_t delay = remaining < GDOX_XENIA_STOP_POLL_MS
                ? remaining
                : GDOX_XENIA_STOP_POLL_MS;
            const DWORD result = WaitForSingleObject(process->handle, delay);

            if (result == WAIT_OBJECT_0) {
                DWORD code = 1U;

                if (!GetExitCodeProcess(process->handle, &code)) {
                    gdox_windows_io_error(
                        &query_error,
                        "could not read the stopped Xenia process status",
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
                    "could not wait for Xenia to stop",
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
    gdox_xenia_process *process,
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
            "could not force Xenia to stop",
            GetLastError()
        );
        remember_error(&saved_error, &terminate_error);
    }
    if (wait_for_exit(
            process,
            GDOX_XENIA_FORCED_REAP_MS,
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
            "Xenia did not exit after forced termination"
        );
    }
    *exit_code = -1;
    return false;
}

static void force_terminal_cleanup(gdox_xenia_process *process)
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

bool gdox_xenia_stop(
    gdox_xenia_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (process == NULL || exit_code == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia process and exit status are required"
        );
        return false;
    }
    return terminate_owned_process(process, grace_ms, exit_code, error);
}

void gdox_xenia_process_destroy(gdox_xenia_process *process)
{
    if (process != NULL) {
        int exit_code;
        gdox_error error;

        if (!process->reaped) {
            if (!terminate_owned_process(
                    process, 1000U, &exit_code, &error
                )) {
                force_terminal_cleanup(process);
            }
        }
        (void)CloseHandle(process->handle);
        free(process);
    }
}
