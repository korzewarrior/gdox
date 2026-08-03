#define WIN32_LEAN_AND_MEAN

#include "platform/xemu_helper_process.h"

#include "platform/session_storage.h"
#include "platform/windows_command.h"
#include "platform/windows_support.h"
#include "platform/xemu_runtime_session.h"

#include <windows.h>

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_WINDOWS_PATH_CAPACITY 32768U

enum {
    GDOX_XEMU_HELPER_POLL_MS = 10U,
    GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS = 10U,
};

static bool wide_regular_file(const wchar_t *path)
{
    const DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

static bool capture_pipe_output(
    HANDLE pipe,
    char output[GDOX_XEMU_HELPER_CAPTURE_BYTES],
    size_t *bytes,
    bool *open,
    bool *overflow,
    gdox_error *error
)
{
    while (*open) {
        char chunk[256];
        DWORD available = 0U;
        DWORD read_bytes = 0U;
        size_t copied;
        size_t capacity;

        if (!PeekNamedPipe(pipe, NULL, 0U, NULL, &available, NULL)) {
            const DWORD code = GetLastError();

            if (code == ERROR_BROKEN_PIPE) {
                *open = false;
                return true;
            }
            gdox_windows_io_error(
                error, "could not inspect xemu helper output", code
            );
            return false;
        }
        if (available == 0U) {
            return true;
        }
        if (!ReadFile(
                pipe,
                chunk,
                available < sizeof(chunk) ? available : (DWORD)sizeof(chunk),
                &read_bytes,
                NULL
            )) {
            const DWORD code = GetLastError();

            if (code == ERROR_BROKEN_PIPE) {
                *open = false;
                return true;
            }
            gdox_windows_io_error(
                error, "could not read xemu helper output", code
            );
            return false;
        }
        capacity = GDOX_XEMU_HELPER_CAPTURE_BYTES - *bytes;
        copied = (size_t)read_bytes < capacity
            ? (size_t)read_bytes : capacity;
        memcpy(output + *bytes, chunk, copied);
        *bytes += copied;
        *overflow = *overflow || copied != (size_t)read_bytes;
    }
    return true;
}

bool gdox_xemu_helper_run(
    const char *path,
    const char *const *arguments,
    uint32_t timeout_ms,
    gdox_xemu_helper_result *output,
    gdox_error *error
)
{
    SECURITY_ATTRIBUTES security = {0};
    HANDLE output_read = NULL;
    HANDLE output_write = NULL;
    HANDLE error_read = NULL;
    HANDLE error_write = NULL;
    HANDLE null_input = INVALID_HANDLE_VALUE;
    HANDLE job = NULL;
    wchar_t *executable = NULL;
    wchar_t executable_path[GDOX_WINDOWS_PATH_CAPACITY];
    gdox_windows_command command = {0};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits = {0};
    bool output_open = true;
    bool diagnostic_open = true;
    bool started = false;
    bool assigned = false;
    bool finished = false;
    uint32_t elapsed = 0U;
    DWORD exit_code = 1U;
    gdox_session_storage temporary = {0};
    gdox_xemu_environment child_environment = {0};
    gdox_error cleanup_error;
    wchar_t *wide_temporary = NULL;
    bool success = false;
    DWORD characters;
    size_t argument_count = 0U;

    gdox_error_clear(error);
    if (output != NULL) {
        memset(output, 0, sizeof(*output));
        output->exit_code = -1;
    }
    if (path == NULL || path[0] == '\0' || arguments == NULL
        || output == NULL || timeout_ms == 0U
        || timeout_ms > GDOX_XEMU_HELPER_MAXIMUM_TIMEOUT_MS) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "runnable xemu path, helper arguments, timeout, and result are required"
        );
        return false;
    }
    while (arguments[argument_count] != NULL
        && argument_count <= GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS) {
        ++argument_count;
    }
    if (argument_count == 0U
        || argument_count > GDOX_XEMU_HELPER_MAXIMUM_ARGUMENTS) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "xemu helper argument count is invalid"
        );
        return false;
    }
    if (!gdox_session_storage_recover(error)
        || !gdox_session_storage_create(&temporary, error)) {
        goto cleanup;
    }
    wide_temporary = gdox_windows_wide_path(temporary.root, error);
    if (wide_temporary == NULL
        || !gdox_xemu_environment_create(
            temporary.root, &child_environment, error
        )) {
        goto cleanup;
    }
    executable = gdox_windows_wide_path(path, error);
    characters = executable != NULL ? GetFullPathNameW(
        executable,
        GDOX_WINDOWS_PATH_CAPACITY,
        executable_path,
        NULL
    ) : 0U;
    if (executable == NULL || characters == 0U
        || characters >= GDOX_WINDOWS_PATH_CAPACITY
        || !wide_regular_file(executable_path)
        || !gdox_windows_command_add_wide(
            &command, executable_path, error
        )) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "xemu helper executable is unavailable"
            );
        }
        goto cleanup;
    }
    for (size_t index = 0U; index < argument_count; ++index) {
        if (!gdox_windows_command_add_utf8(
                &command, arguments[index], error
            )) {
            goto cleanup;
        }
    }
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&output_read, &output_write, &security, 0U)
        || !CreatePipe(&error_read, &error_write, &security, 0U)
        || !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0U)
        || !SetHandleInformation(error_read, HANDLE_FLAG_INHERIT, 0U)) {
        gdox_windows_io_error(
            error, "could not create xemu helper pipes", GetLastError()
        );
        goto cleanup;
    }
    null_input = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    job = CreateJobObjectW(NULL, NULL);
    job_limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (null_input == INVALID_HANDLE_VALUE || job == NULL
        || !SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &job_limits,
            sizeof(job_limits)
        )) {
        gdox_windows_io_error(
            error, "could not isolate xemu helper", GetLastError()
        );
        goto cleanup;
    }
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = null_input;
    startup.hStdOutput = output_write;
    startup.hStdError = error_write;
    if (!CreateProcessW(
            executable_path,
            command.text,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            child_environment.block,
            wide_temporary,
            &startup,
            &process
        )) {
        gdox_windows_io_error(
            error, "could not start xemu helper", GetLastError()
        );
        goto cleanup;
    }
    started = true;
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        gdox_windows_io_error(
            error, "could not contain xemu helper", GetLastError()
        );
        goto cleanup;
    }
    assigned = true;
    if (ResumeThread(process.hThread) == (DWORD)-1) {
        gdox_windows_io_error(
            error, "could not resume xemu helper", GetLastError()
        );
        goto cleanup;
    }
    (void)CloseHandle(process.hThread);
    process.hThread = NULL;
    (void)CloseHandle(output_write);
    output_write = NULL;
    (void)CloseHandle(error_write);
    error_write = NULL;
    while (elapsed <= timeout_ms) {
        if (!capture_pipe_output(
                output_read, output->output, &output->output_bytes,
                &output_open, &output->overflow, error
            ) || !capture_pipe_output(
                error_read, output->diagnostics,
                &output->diagnostic_bytes, &diagnostic_open,
                &output->overflow, error
            )) {
            goto cleanup;
        }
        if (!finished
            && WaitForSingleObject(process.hProcess, 0U) == WAIT_OBJECT_0) {
            finished = true;
        }
        if (finished && !output_open && !diagnostic_open) {
            break;
        }
        Sleep(GDOX_XEMU_HELPER_POLL_MS);
        elapsed = timeout_ms - elapsed < GDOX_XEMU_HELPER_POLL_MS
            ? timeout_ms + 1U : elapsed + GDOX_XEMU_HELPER_POLL_MS;
    }
    if (!finished || output_open || diagnostic_open) {
        output->timed_out = true;
        success = true;
        goto cleanup;
    }
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        gdox_windows_io_error(
            error, "could not read xemu helper exit code", GetLastError()
        );
        goto cleanup;
    }
    output->exit_code = exit_code <= INT_MAX ? (int)exit_code : -1;
    success = true;

cleanup:
    if (started && !finished) {
        if (job != NULL && assigned) {
            (void)TerminateJobObject(job, 1U);
        }
        if (process.hProcess != NULL) {
            (void)TerminateProcess(process.hProcess, 1U);
        }
        if (process.hProcess != NULL) {
            (void)WaitForSingleObject(process.hProcess, 5000U);
        }
    }
    if (process.hThread != NULL) {
        (void)CloseHandle(process.hThread);
    }
    if (process.hProcess != NULL) {
        (void)CloseHandle(process.hProcess);
    }
    if (job != NULL) {
        (void)CloseHandle(job);
    }
    if (null_input != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(null_input);
    }
    if (output_read != NULL) {
        (void)CloseHandle(output_read);
    }
    if (output_write != NULL) {
        (void)CloseHandle(output_write);
    }
    if (error_read != NULL) {
        (void)CloseHandle(error_read);
    }
    if (error_write != NULL) {
        (void)CloseHandle(error_write);
    }
    gdox_windows_command_destroy(&command);
    gdox_xemu_environment_destroy(&child_environment);
    free(wide_temporary);
    free(executable);
    if (!gdox_session_storage_cleanup(&temporary, &cleanup_error)) {
        if (success || !gdox_error_is_set(error)) {
            *error = cleanup_error;
        }
        success = false;
    }
    return success;
}
