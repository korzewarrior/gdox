#include "app/xenia_storage.h"

#include "app/xenia_content_migration.h"
#include "app/xenia_patches.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <string.h>

static bool remove_legacy_derived_storage(gdox_error *error)
{
    char xenia_root[GDOX_STORAGE_PATH_CAPACITY];

    if (!gdox_user_data_path("xenia", xenia_root, error)) {
        return false;
    }
    return !gdox_storage_directory_exists(xenia_root)
        || (gdox_session_storage_remove_relative(
                xenia_root, "storage", error
            )
            && gdox_session_storage_remove_relative(
                xenia_root, "proton", error
            )
            && gdox_session_storage_remove_relative(
                xenia_root, "logs", error
            ));
}

static bool migrate_legacy_content(gdox_error *error)
{
    char content_root[GDOX_STORAGE_PATH_CAPACITY];

    return gdox_user_data_path("xenia/content", content_root, error)
        && gdox_xenia_content_migrate(content_root, error);
}

static bool prepare_ephemeral_session(
    gdox_session_storage *session,
    gdox_error *error
)
{
#if defined(_WIN32)
    /*
     * Windows has no driverless memory filesystem. Keep derived Xenia state
     * in GDOX's owned temporary-session tree and remove it on close or the
     * next recovery pass. Linux uses a verified memory filesystem.
     */
    return gdox_session_storage_recover(error)
        && gdox_session_storage_create(session, error);
#else
    return gdox_session_storage_recover_memory(error)
        && gdox_session_storage_create_memory(session, error);
#endif
}

bool gdox_xenia_storage_recover(gdox_error *error)
{
    gdox_error_clear(error);
    return gdox_session_storage_recover(error)
        && remove_legacy_derived_storage(error)
        && migrate_legacy_content(error);
}

static bool prepare_session_directory(
    gdox_session_storage *session,
    const char *relative,
    char output[GDOX_SESSION_PATH_CAPACITY],
    gdox_error *error
)
{
    return gdox_session_storage_path(session, relative, output, error)
        && gdox_storage_ensure_directory(output, error);
}

bool gdox_xenia_storage_open(
    const gdox_xenia_launch_policy *policy,
    gdox_xenia_storage *storage,
    gdox_error *error
)
{
    gdox_error cleanup_error;
    int written;

    gdox_error_clear(error);
    if (policy == NULL || policy->runtime == NULL || storage == NULL
        || storage->session.active) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia policy and inactive session storage are required"
        );
        return false;
    }
    if (!policy->runtime->supports_storage_isolation) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Xenia runtime does not provide ephemeral-content isolation"
        );
        return false;
    }
    memset(storage, 0, sizeof(*storage));
    if (!gdox_xenia_storage_recover(error)
        || !prepare_ephemeral_session(&storage->session, error)
        || !prepare_session_directory(
            &storage->session, "storage", storage->storage, error
        )
        || !prepare_session_directory(
            &storage->session, "cache", storage->cache, error
        )) {
        goto fail;
    }
    if (!gdox_user_data_path("xenia/content", storage->content, error)
        || !gdox_storage_ensure_directory(storage->content, error)) {
        goto fail;
    }
    written = snprintf(
        storage->log_file,
        sizeof(storage->log_file),
        "%s/xenia.log",
        storage->session.root
    );
    if (written < 0 || (size_t)written >= sizeof(storage->log_file)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia session log path is too long"
        );
        goto fail;
    }
    if (!gdox_xenia_provision_patches(
            storage->storage, policy->patch_set, error
        )) {
        goto fail;
    }
    return true;

fail:
    if (storage->session.active
        && !gdox_session_storage_cleanup(
            &storage->session, &cleanup_error
        )) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];

        (void)snprintf(
            message,
            sizeof(message),
            "%.96s; session cleanup failed: %.96s",
            gdox_error_is_set(error) ? error->message : "Xenia setup failed",
            cleanup_error.message
        );
        gdox_error_set(error, GDOX_ERROR_IO, message);
    }
    memset(storage, 0, sizeof(*storage));
    return false;
}

bool gdox_xenia_storage_close(
    gdox_xenia_storage *storage,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (storage == NULL) {
        return true;
    }
    if (!gdox_session_storage_cleanup(&storage->session, error)) {
        return false;
    }
    memset(storage, 0, sizeof(*storage));
    return true;
}
