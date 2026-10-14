#define _POSIX_C_SOURCE 200809L
#include "gdox/dolphin.h"
#include "platform/disc_bridge_linux.h"
#include "platform/portable_sync.h"

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

struct gdox_dolphin_process {
    pid_t child;
    int exit_code;
    bool shutdown_requested;
    gdox_disc_bridge *bridge;
};

static bool reap(gdox_dolphin_process *process, bool *running, gdox_error *error)
{
    int status = 0;
    pid_t result;
    *running = false;
    if (process->child <= 0) return true;
    do {
        result = waitpid(process->child, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        *running = true;
        return true;
    }
    if (result == process->child) {
        process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
            : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
        process->child = 0;
        return true;
    }
    if (result < 0 && errno == ECHILD) {
        process->child = 0;
        process->exit_code = 1;
        return true;
    }
    gdox_error_set(error, GDOX_ERROR_IO, "Could not monitor Dolphin");
    return false;
}

bool gdox_dolphin_launch(const gdox_dolphin_runtime *runtime,
    const char *user_directory, const char *disc_uri, uint64_t length,
    bool fullscreen, gdox_dolphin_process **output, gdox_error *error)
{
    gdox_dolphin_process *process;
    int result;
    gdox_error_clear(error);
    if (runtime == NULL || runtime->launcher[0] == '\0'
        || user_directory == NULL || user_directory[0] != '/'
        || output == NULL || *output != NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "A verified Dolphin runtime and empty process owner are required");
        return false;
    }
    process = calloc(1U, sizeof(*process));
    if (process == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "Could not allocate Dolphin process");
        return false;
    }
    *output = process;
    if (!gdox_disc_bridge_start(user_directory, false, disc_uri, length,
            &process->bridge, error)) {
        /* The bridge may retain an incomplete mount for cleanup. */
        if (process->bridge == NULL) {
            free(process);
            *output = NULL;
        }
        return false;
    }
    char *arguments[] = {
        (char *)runtime->launcher, "--user", (char *)user_directory,
        "--exec", (char *)gdox_disc_bridge_path(process->bridge),
        "--config", "Dolphin.Core.LoadGameIntoMemory=False",
        "--config", "Dolphin.Interface.ConfirmStop=False",
        "--config", "Dolphin.Interface.PauseOnFocusLost=False",
        /* GDOX reports stream faults and owns teardown. A modal read-error
         * dialog can otherwise block Dolphin's orderly stop path. */
        "--config", "Dolphin.Interface.UsePanicHandlers=False",
        "--config", "Dolphin.Display.RenderToMain=True",
        "--config", "Dolphin.DSP.DumpAudio=False",
        "--config", "Dolphin.DSP.DumpUCode=False",
        "--config", "Dolphin.Movie.DumpFrames=False",
        "--config", "GFX.Settings.DumpTextures=False",
        "--config", fullscreen ? "Dolphin.Display.Fullscreen=True"
                                : "Dolphin.Display.Fullscreen=False",
        NULL,
    };
    result = posix_spawn(&process->child, runtime->launcher, NULL, NULL,
        arguments, environ);
    if (result != 0) {
        process->child = 0;
        gdox_error_set(error, GDOX_ERROR_IO, "Could not start bundled Dolphin");
        return false;
    }
    return true;
}

bool gdox_dolphin_poll(gdox_dolphin_process *process,
    bool *running, int *exit_code, gdox_error *error)
{
    bool bridge_alive;
    gdox_error_clear(error);
    if (process == NULL || running == NULL || exit_code == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "Dolphin process is required");
        return false;
    }
    if (!reap(process, running, error)) return false;
    *exit_code = process->exit_code;
    if (*running && (!gdox_disc_bridge_alive(process->bridge, &bridge_alive, error)
            || !bridge_alive)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_IO, "Live GameCube or Wii disc stream closed unexpectedly");
        }
        return false;
    }
    return true;
}

bool gdox_dolphin_stop(gdox_dolphin_process **owner, gdox_error *error)
{
    gdox_dolphin_process *process;
    bool running = false;
    gdox_error_clear(error);
    if (owner == NULL || *owner == NULL) return true;
    process = *owner;
    if (!reap(process, &running, error)) return false;
    if (running) {
        /* Dolphin handles SIGTERM through its event loop and flushes saves. */
        if (!process->shutdown_requested) {
            if (kill(process->child, SIGTERM) != 0 && errno != ESRCH) {
                gdox_error_set(error, GDOX_ERROR_IO, "Could not request Dolphin shutdown");
                return false;
            }
            /* Dolphin resets its signal handler after the first request.
             * Sending SIGTERM again would bypass its save-flushing path. */
            process->shutdown_requested = true;
        }
        for (unsigned int attempt = 0U; attempt < 100U && running; ++attempt) {
            gdox_sleep_ms(50U);
            if (!reap(process, &running, error)) return false;
        }
        if (running) {
            /* Retain ownership: do not silently kill a save-writing process. */
            gdox_error_set(error, GDOX_ERROR_IO,
                "Dolphin has not closed; finish any save operation and close its window");
            return false;
        }
    }
    if (!gdox_disc_bridge_close(process->bridge, error)) return false;
    process->bridge = NULL;
    free(process);
    *owner = NULL;
    return true;
}
