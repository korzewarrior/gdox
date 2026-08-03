#include "app/runtime_xenia.h"
#include "app/xenia_process_stop.h"

#include <stdio.h>
#include <string.h>

static bool cleanup_failed_start(
    gdox_runtime *runtime,
    const gdox_error *failure,
    gdox_error *error
)
{
    gdox_error cleanup_error;

    if (gdox_xenia_storage_close(&runtime->xenia_storage, &cleanup_error)) {
        *error = *failure;
        return false;
    }
    (void)snprintf(
        error->message,
        sizeof(error->message),
        "%.96s; session cleanup failed: %.96s",
        failure->message,
        cleanup_error.message
    );
    error->code = GDOX_ERROR_IO;
    return false;
}

bool gdox_runtime_xenia_prepare(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    const gdox_xenia_launch_policy *policy;

    gdox_error_clear(error);
    if (runtime == NULL || snapshot == NULL || !runtime->media.open
        || runtime->media.info.backend != GDOX_MEDIA_BACKEND_XENIA) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an Xbox 360 media session is required"
        );
        return false;
    }
    policy = runtime->media.info.xenia_policy;
    memset(&runtime->xenia_runtime, 0, sizeof(runtime->xenia_runtime));
    snapshot->xenia_ready = false;
    snapshot->bundled_xenia = false;
    snapshot->xenia_executable[0] = '\0';
    if (policy == NULL || policy->runtime == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Xbox 360 media has no supported Xenia runtime policy"
        );
        gdox_runtime_copy_text(
            snapshot->xenia_setup,
            sizeof(snapshot->xenia_setup),
            error->message
        );
        return false;
    } else if (!policy->runtime->supports_storage_isolation) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Xbox 360 support requires an unpublished rebuilt Xenia runtime with ephemeral-content isolation"
        );
        gdox_runtime_copy_text(
            snapshot->xenia_setup,
            sizeof(snapshot->xenia_setup),
            error->message
        );
        return false;
    }
    if (!gdox_xenia_resolve_runtime(
            policy->runtime, NULL, &runtime->xenia_runtime, error
        )) {
        gdox_runtime_copy_text(
            snapshot->xenia_setup,
            sizeof(snapshot->xenia_setup),
            gdox_error_is_set(error)
                ? error->message
                : "Verified Xenia runtime is unavailable"
        );
        return false;
    }
    snapshot->xenia_ready = true;
    snapshot->bundled_xenia =
        runtime->xenia_runtime.origin == GDOX_XENIA_RUNTIME_BUNDLED;
    gdox_runtime_copy_text(
        snapshot->xenia_executable,
        sizeof(snapshot->xenia_executable),
        runtime->xenia_runtime.payload
    );
    (void)snprintf(
        snapshot->xenia_setup,
        sizeof(snapshot->xenia_setup),
        "Xenia %s is verified",
        policy->runtime->revision
    );
    return true;
}

bool gdox_runtime_xenia_start(gdox_runtime *runtime, gdox_error *error)
{
    const gdox_xenia_launch_policy *policy;
    gdox_xenia_options options;
    gdox_xenia_target target;
    gdox_app_settings settings;

    gdox_error_clear(error);
    if (runtime == NULL || runtime->xenia != NULL || !runtime->media.open
        || runtime->media.info.backend != GDOX_MEDIA_BACKEND_XENIA
        || runtime->media.info.xenia_policy == NULL
        || runtime->xenia_runtime.definition
            != runtime->media.info.xenia_policy->runtime) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a ready verified Xbox 360 media session is required"
        );
        return false;
    }
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(
            error, GDOX_ERROR_INTERNAL, "could not read Xenia settings"
        );
        return false;
    }
    settings = runtime->snapshot.settings;
    gdox_mutex_unlock(&runtime->mutex);
    policy = runtime->media.info.xenia_policy;
    if (!gdox_xenia_storage_open(
            policy, &runtime->xenia_storage, error
        )) {
        return false;
    }
    options = (gdox_xenia_options){
        .runtime = &runtime->xenia_runtime,
        .policy = policy,
        .performance_profile = runtime->host_profile
                == GDOX_HOST_PROFILE_HANDHELD
            ? GDOX_XENIA_PERFORMANCE_HANDHELD
            : GDOX_XENIA_PERFORMANCE_DESKTOP,
        .storage_root = runtime->xenia_storage.storage,
        .content_root = runtime->xenia_storage.content,
        .cache_root = runtime->xenia_storage.cache,
        .log_file = runtime->xenia_storage.log_file,
        .console_output = false,
        .fullscreen = settings.fullscreen,
    };
    if (!gdox_runtime_media_prepare_xenia_target(
            &runtime->media, &target, error
        )) {
        const gdox_error failure = *error;

        return cleanup_failed_start(runtime, &failure, error);
    }
    if (!gdox_xenia_launch(&options, &target, &runtime->xenia, error)) {
        const gdox_error failure = *error;

        return cleanup_failed_start(runtime, &failure, error);
    }
    return true;
}

bool gdox_runtime_xenia_stop(gdox_runtime *runtime, gdox_error *error)
{
    gdox_error stop_error;
    bool stopped;

    gdox_error_clear(error);
    if (runtime == NULL || runtime->xenia == NULL) {
        return true;
    }
    stopped = gdox_xenia_process_stop_orderly(
        &runtime->xenia, &stop_error
    );
    if (runtime->xenia != NULL) {
        *error = stop_error;
        return false;
    }
    if (!stopped) {
        runtime->terminal_shutdown_failed = true;
        runtime->terminal_shutdown_error = stop_error;
    }
    if (!gdox_xenia_storage_close(&runtime->xenia_storage, error)) {
        return false;
    }
    if (!stopped) {
        *error = stop_error;
        return false;
    }
    return true;
}

bool gdox_runtime_xenia_cleanup(gdox_runtime *runtime, gdox_error *error)
{
    gdox_error_clear(error);
    if (runtime == NULL || runtime->xenia != NULL) {
        if (runtime != NULL && runtime->xenia != NULL) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "Xenia must stop before session storage cleanup"
            );
            return false;
        }
        return true;
    }
    return gdox_xenia_storage_close(&runtime->xenia_storage, error);
}

bool gdox_runtime_xenia_poll(
    gdox_runtime *runtime,
    bool *running,
    int *exit_code,
    gdox_error *error
)
{
    if (runtime == NULL || runtime->xenia == NULL) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "Xenia process is not running"
        );
        return false;
    }
    if (!gdox_xenia_poll(runtime->xenia, running, exit_code, error)) {
        return false;
    }
    if (!*running) {
        gdox_xenia_process_destroy(runtime->xenia);
        runtime->xenia = NULL;
        if (!gdox_xenia_storage_close(&runtime->xenia_storage, error)) {
            return false;
        }
    }
    return true;
}
