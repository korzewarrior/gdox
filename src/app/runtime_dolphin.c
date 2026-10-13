#include "app/runtime_dolphin.h"
#include "platform/user_storage.h"
#include "app/dolphin_profile.h"
#include <string.h>

bool gdox_runtime_dolphin_prepare(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot, gdox_error *error)
{
    gdox_error_clear(error);
    if (runtime == NULL || snapshot == NULL || !runtime->media.open
        || runtime->media.info.backend != GDOX_MEDIA_BACKEND_DOLPHIN) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "An open GameCube disc is required");
        return false;
    }
    snapshot->dolphin_ready = false;
    snapshot->bundled_dolphin = false;
    snapshot->dolphin_executable[0] = '\0';
    if (!gdox_dolphin_resolve(&runtime->dolphin_runtime, error)) {
        gdox_runtime_copy_text(snapshot->dolphin_setup, sizeof(snapshot->dolphin_setup), error->message);
        return false;
    }
    snapshot->dolphin_ready = true;
    snapshot->bundled_dolphin = true;
    gdox_runtime_copy_text(snapshot->dolphin_executable, sizeof(snapshot->dolphin_executable), runtime->dolphin_runtime.payload);
    gdox_runtime_copy_text(snapshot->dolphin_setup, sizeof(snapshot->dolphin_setup), "Dolphin 2609a is verified");
    return true;
}

bool gdox_runtime_dolphin_start(gdox_runtime *runtime, gdox_error *error)
{
    char user_directory[GDOX_STORAGE_PATH_CAPACITY];
    bool fullscreen;
    gdox_error_clear(error);
    if (runtime == NULL || runtime->dolphin != NULL || !runtime->media.open
        || runtime->media.info.backend != GDOX_MEDIA_BACKEND_DOLPHIN
        || runtime->media.exported == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "A ready live GameCube session is required");
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "Could not read Dolphin settings");
        return false;
    }
    fullscreen = runtime->snapshot.settings.fullscreen;
    gdox_mutex_unlock(&runtime->mutex);
    /* Resolve and recheck the reviewed payload at every launch. */
    if (!gdox_dolphin_resolve(&runtime->dolphin_runtime, error)
        || !gdox_user_data_path("dolphin", user_directory, error)
        || !gdox_storage_ensure_private_directory(user_directory, error)
        || !gdox_dolphin_profile_prepare(user_directory, error)) return false;
    return gdox_dolphin_launch(&runtime->dolphin_runtime, user_directory,
        gdox_nbd_uri(runtime->media.exported), gdox_nbd_length(runtime->media.exported),
        fullscreen, &runtime->dolphin, error);
}

bool gdox_runtime_dolphin_stop(gdox_runtime *runtime, gdox_error *error)
{
    gdox_error_clear(error);
    return runtime == NULL || gdox_dolphin_stop(&runtime->dolphin, error);
}

bool gdox_runtime_dolphin_poll(gdox_runtime *runtime, bool *running, int *exit_code, gdox_error *error)
{
    if (runtime == NULL || !gdox_dolphin_poll(runtime->dolphin, running, exit_code, error)) return false;
    if (!*running) return gdox_dolphin_stop(&runtime->dolphin, error);
    return true;
}
