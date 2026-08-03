#include "app/runtime_media.h"

#include "gdox/disc.h"
#include "gdox/live.h"
#include "gdox/source.h"
#include "gdox/x360.h"

#include <stdio.h>
#include <string.h>

static bool copy_image_path(
    char output[GDOX_EMULATOR_PATH_CAPACITY],
    const char *path,
    gdox_error *error
)
{
    const size_t bytes = path != NULL ? strlen(path) : 0U;

    if (bytes == 0U || bytes >= GDOX_EMULATOR_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "disc image path is invalid or too long"
        );
        return false;
    }
    memcpy(output, path, bytes + 1U);
    return true;
}

static void clear_open_result(gdox_runtime_media_open_result *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
}

static void identify_media(
    gdox_runtime_media_open_result *result,
    const gdox_runtime_media_info *info
)
{
    result->state = GDOX_RUNTIME_MEDIA_IDENTIFIED;
    result->info = *info;
}

bool gdox_runtime_media_is_owned(
    const gdox_runtime_media_session *session
)
{
    return session != NULL
        && (session->open || session->exported != NULL
            || gdox_disc_is_valid(&session->validated_disc)
            || gdox_source_is_valid(&session->retained_source));
}

bool gdox_runtime_media_retain_cleanup_source(
    gdox_runtime_media_session *session,
    gdox_sector_source *source,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (session == NULL || !gdox_source_is_valid(source)
        || gdox_source_is_valid(&session->retained_source)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open source and empty cleanup owner are required"
        );
        return false;
    }
    session->retained_source = *source;
    source->context = NULL;
    source->ops = NULL;
    session->open = false;
    return true;
}

static bool select_xenia_policy(
    gdox_random_disc *disc,
    gdox_runtime_media_info *info,
    gdox_error *error
)
{
    gdox_xenia_title_identity identity;
    const gdox_xenia_launch_policy *policy;
    gdox_x360_executable_kind module_kind;
    gdox_x360_execution_info module_execution;

    if (info->x360.execution.valid) {
        identity = (gdox_xenia_title_identity){
            info->x360.execution.title_id,
            info->x360.execution.media_id,
            info->x360.execution.disc_number,
            info->x360.execution.disc_count,
        };
        policy = gdox_xenia_select_policy(&identity);
    } else {
        policy = gdox_xenia_default_policy();
    }
    if (policy == NULL || policy->runtime == NULL
        || policy->launch_module == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "Xbox 360 compatibility policy is incomplete"
        );
        return false;
    }
    if (strcmp(policy->launch_module, info->x360.launch_executable) == 0) {
        module_kind = info->x360.executable;
        module_execution = info->x360.execution;
    } else {
        if (!gdox_x360_disc_find_executable(
                disc,
                &info->x360,
                policy->launch_module,
                &module_kind,
                &module_execution,
                error
            ) || (module_kind != GDOX_X360_EXECUTABLE_XEX1
                && module_kind != GDOX_X360_EXECUTABLE_XEX2)
            || !gdox_x360_execution_info_equal(
                &module_execution, &info->x360.execution
            )) {
            if (!gdox_error_is_set(error)) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_INVALID_VOLUME,
                    "reviewed Xbox 360 launch module identity does not match the disc"
                );
            }
            return false;
        }
    }
    info->xenia_policy = policy;
    info->xenia_module_kind = module_kind;
    info->xenia_module_execution = module_execution;
    return true;
}

static bool xenia_info_matches(
    const gdox_runtime_media_info *expected,
    const gdox_runtime_media_info *actual
)
{
    return expected->xenia_policy == actual->xenia_policy
        && expected->xenia_module_kind == actual->xenia_module_kind
        && gdox_x360_execution_info_equal(
            &expected->xenia_module_execution,
            &actual->xenia_module_execution
        )
        && gdox_x360_disc_info_equal(&expected->x360, &actual->x360);
}

static bool validate_xenia_disc(
    gdox_random_disc *disc,
    const gdox_runtime_media_info *expected,
    gdox_error *error
)
{
    gdox_runtime_media_info actual = {0};

    if (!gdox_disc_is_valid(disc) || expected == NULL
        || !gdox_x360_disc_probe(disc, &actual.x360, error)
        || !select_xenia_policy(disc, &actual, error)) {
        return false;
    }
    if (!xenia_info_matches(expected, &actual)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "Xbox 360 disc identity or reviewed launch module changed"
        );
        return false;
    }
    return true;
}

static bool preflight_xenia_image_target(
    const gdox_xenia_runtime *runtime,
    gdox_error *error
)
{
    if (gdox_xenia_runtime_target_supported(
            runtime, GDOX_XENIA_TARGET_PRIVATE_NBD
        )) {
        return gdox_xenia_target_preflight(
            GDOX_XENIA_TARGET_PRIVATE_NBD, error
        );
    }
    if (gdox_xenia_runtime_target_supported(
            runtime, GDOX_XENIA_TARGET_IMAGE
        )) {
        return gdox_xenia_target_preflight(GDOX_XENIA_TARGET_IMAGE, error);
    }
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "reviewed Xenia runtime cannot consume media on this platform"
    );
    return false;
}

static void describe_x360(gdox_runtime_media_info *info)
{
    if (info->x360.execution.valid) {
        (void)snprintf(
            info->title,
            sizeof(info->title),
            "Xbox 360 title %08X",
            info->x360.execution.title_id
        );
    } else {
        (void)snprintf(info->title, sizeof(info->title), "Xbox 360 disc");
    }
}

static bool start_export(
    gdox_random_disc *disc,
    gdox_media_backend backend,
    gdox_runtime_media_session *session,
    gdox_error *error
)
{
    gdox_nbd_client_access client_access;

    if (backend == GDOX_MEDIA_BACKEND_XENIA) {
        client_access = GDOX_NBD_CLIENT_READ_ONLY;
    } else if (backend == GDOX_MEDIA_BACKEND_XEMU) {
        /* xemu 0.8.136 opens dvd_path writable before exposing the CD-ROM. */
        client_access = GDOX_NBD_CLIENT_WRITE_OPEN;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a supported emulator backend is required for the disc export"
        );
        return false;
    }
    return gdox_nbd_start(
        disc, client_access, &session->exported, error
    );
}

static bool inspect_xenia_disc(
    gdox_random_disc *disc,
    void *context,
    gdox_error *error
)
{
    return validate_xenia_disc(
        disc, (const gdox_runtime_media_info *)context, error
    );
}

static bool build_physical_xbox_360(
    const gdox_optical_media_info *optical,
    gdox_sector_source *whole,
    gdox_sector_source *partition,
    gdox_random_disc *disc,
    gdox_runtime_media_info *info,
    gdox_error *error
)
{
    const uint64_t source_sectors = gdox_source_sector_count(whole);

    info->source_sectors = source_sectors;
    if (optical->game_partition_lba >= source_sectors) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "Xbox 360 game partition begins outside the validated source"
        );
        return false;
    }
    if (!gdox_source_make_partition(
            whole,
            optical->game_partition_lba,
            partition,
            error
        ) || !gdox_x360_live_disc_build(
            partition, disc, &info->x360, error
        )) {
        return false;
    }
    if (info->x360.layout != GDOX_X360_IMAGE_LAYOUT_PARTITION
        || info->x360.game_offset_bytes != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "selected Xbox 360 game partition has an unexpected nested layout"
        );
        return false;
    }
    info->game_partition_lba = optical->game_partition_lba;
    if (!select_xenia_policy(disc, info, error)) {
        return false;
    }
    info->platform = GDOX_MEDIA_PLATFORM_XBOX_360;
    info->backend = GDOX_MEDIA_BACKEND_XENIA;
    describe_x360(info);
    return true;
}

bool gdox_runtime_media_open_physical(
    gdox_optical_drive drive,
    gdox_runtime_media_session *session,
    gdox_runtime_media_open_result *result,
    gdox_error *error
)
{
    gdox_sector_source whole = {0};
    gdox_sector_source partition = {0};
    gdox_random_disc disc = {0};
    gdox_live_disc_info live_info = {0};
    gdox_live_disc_options live_options = {0};
    gdox_runtime_media_info info = {0};
    gdox_optical_media_info optical = {0};
    gdox_error cleanup_error;
    bool xbox_360;
    bool success = false;

    gdox_error_clear(error);
    clear_open_result(result);
    if (session == NULL || result == NULL || session->open
        || gdox_runtime_media_is_owned(session)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty media session is required"
        );
        return false;
    }
    if (!gdox_optical_open_media(
            drive, 3U, 20000U, &whole, &optical, error
        )) {
        if (gdox_source_is_valid(&whole)) {
            gdox_error operation_error = *error;

            if (!gdox_runtime_media_retain_cleanup_source(
                    session, &whole, &cleanup_error
                )) {
                *error = cleanup_error;
            } else {
                *error = operation_error;
            }
        }
        return false;
    }
    xbox_360 = optical.profile == GDOX_OPTICAL_MEDIA_XGD2
        || optical.profile == GDOX_OPTICAL_MEDIA_XGD3;
    info.source = GDOX_MEDIA_PHYSICAL_DISC;
    info.source_sectors = gdox_source_sector_count(&whole);
    if (optical.profile == GDOX_OPTICAL_MEDIA_XGD1) {
        live_options.sequential_read_blocks =
            optical.sequential_read_blocks;
        if (!gdox_live_disc_build_configured(
                &whole,
                &live_options,
                &disc,
                &live_info,
                error
            )) {
            goto cleanup;
        }
        info.platform = GDOX_MEDIA_PLATFORM_XBOX;
        info.backend = GDOX_MEDIA_BACKEND_XEMU;
        info.source_sectors = live_info.input_sectors;
        (void)snprintf(info.title, sizeof(info.title), "%s", live_info.title);
    } else if (!xbox_360) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "opened optical media has no validated XGD profile"
            );
        }
        goto cleanup;
    }
    if (xbox_360 && !build_physical_xbox_360(
            &optical, &whole, &partition, &disc, &info, error
        )) {
        goto cleanup;
    }
    identify_media(result, &info);
    if (xbox_360) {
        if (!gdox_xenia_runtime_target_supported(
                info.xenia_policy->runtime,
                GDOX_XENIA_TARGET_PRIVATE_NBD
            )) {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "reviewed Xenia runtime cannot consume a live disc on this platform"
            );
            goto cleanup;
        }
        if (!gdox_xenia_target_preflight(
                GDOX_XENIA_TARGET_PRIVATE_NBD, error
            )) {
            goto cleanup;
        }
    }
    if (!start_export(&disc, info.backend, session, error)) {
        goto cleanup;
    }
    session->info = info;
    session->open = true;
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        if (!gdox_disc_is_valid(&session->validated_disc)) {
            session->validated_disc = disc;
            disc.context = NULL;
            disc.ops = NULL;
            session->open = false;
        }
        *error = cleanup_error;
        success = false;
    }
    if (gdox_source_is_valid(&whole)
        && !gdox_source_close(&whole, &cleanup_error)) {
        gdox_error retention_error;

        if (!gdox_runtime_media_retain_cleanup_source(
                session, &whole, &retention_error
            )) {
            *error = retention_error;
        } else {
            *error = cleanup_error;
        }
        success = false;
    }
    if (gdox_source_is_valid(&partition)
        && !gdox_source_close(&partition, &cleanup_error)) {
        gdox_error retention_error;

        if (!gdox_runtime_media_retain_cleanup_source(
                session, &partition, &retention_error
            )) {
            *error = retention_error;
        } else {
            *error = cleanup_error;
        }
        success = false;
    }
    if (!success && gdox_runtime_media_is_owned(session)) {
        if (!gdox_runtime_media_close(session, &cleanup_error)) {
            *error = cleanup_error;
        }
    }
    if (success) {
        result->state = GDOX_RUNTIME_MEDIA_READY;
    }
    return success;
}

bool gdox_runtime_media_open_image(
    const char *path,
    gdox_runtime_media_session *session,
    gdox_runtime_media_open_result *result,
    gdox_error *error
)
{
    gdox_random_disc disc = {0};
    gdox_media_image_info image = {0};
    gdox_runtime_media_info info = {0};
    char image_path[GDOX_EMULATOR_PATH_CAPACITY];
    gdox_error cleanup_error;
    bool success = false;

    gdox_error_clear(error);
    clear_open_result(result);
    if (path == NULL || path[0] == '\0' || session == NULL || result == NULL
        || session->open || gdox_runtime_media_is_owned(session)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an image path and empty media session are required"
        );
        return false;
    }
    if (!copy_image_path(image_path, path, error)
        || !gdox_media_open_image(path, &disc, &image, error)) {
        return false;
    }
    info.source = GDOX_MEDIA_DISC_IMAGE;
    info.platform = image.platform;
    info.backend = image.backend;
    info.image_layout = image.layout;
    info.source_sectors = image.source_sectors;
    info.game_partition_lba = image.game_partition_lba;
    info.x360 = image.x360;
    if (image.platform == GDOX_MEDIA_PLATFORM_XBOX) {
        (void)snprintf(
            info.title, sizeof(info.title), "%s", image.disc.title
        );
        identify_media(result, &info);
        if (!start_export(&disc, info.backend, session, error)) {
            goto cleanup;
        }
    } else if (image.platform == GDOX_MEDIA_PLATFORM_XBOX_360) {
        if (!select_xenia_policy(&disc, &info, error)) {
            goto cleanup;
        }
        describe_x360(&info);
        identify_media(result, &info);
        if (!preflight_xenia_image_target(info.xenia_policy->runtime, error)) {
            goto cleanup;
        }
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_VOLUME,
            "disc image platform was not identified"
        );
        goto cleanup;
    }
    if (image.platform == GDOX_MEDIA_PLATFORM_XBOX_360) {
        session->validated_disc = disc;
        disc.context = NULL;
        disc.ops = NULL;
        memcpy(session->image_path, image_path, sizeof(session->image_path));
    }
    session->info = info;
    session->open = true;
    success = true;

cleanup:
    if (gdox_disc_is_valid(&disc)
        && !gdox_disc_close(&disc, &cleanup_error)) {
        if (!gdox_disc_is_valid(&session->validated_disc)) {
            session->validated_disc = disc;
            disc.context = NULL;
            disc.ops = NULL;
            session->open = false;
        }
        *error = cleanup_error;
        success = false;
    }
    if (!success && gdox_runtime_media_is_owned(session)) {
        if (!gdox_runtime_media_close(session, &cleanup_error)) {
            *error = cleanup_error;
        }
    }
    if (success) {
        result->state = GDOX_RUNTIME_MEDIA_READY;
    }
    return success;
}

bool gdox_runtime_media_prepare_xenia_target(
    gdox_runtime_media_session *session,
    gdox_xenia_target *target,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (target != NULL) {
        memset(target, 0, sizeof(*target));
    }
    if (session == NULL || target == NULL || !session->open
        || session->info.backend != GDOX_MEDIA_BACKEND_XENIA
        || session->info.xenia_policy == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a validated Xbox 360 media session is required"
        );
        return false;
    }
    if (gdox_disc_is_valid(&session->validated_disc)) {
        if (!validate_xenia_disc(
                &session->validated_disc, &session->info, error
            )) {
            return false;
        }
        if (gdox_xenia_runtime_target_supported(
                session->info.xenia_policy->runtime,
                GDOX_XENIA_TARGET_PRIVATE_NBD
            )) {
            if (session->exported != NULL) {
                gdox_error_set(
                    error,
                    GDOX_ERROR_INTERNAL,
                    "Xbox 360 media session owns two launch sources"
                );
                return false;
            }
            if (!gdox_xenia_target_preflight(
                    GDOX_XENIA_TARGET_PRIVATE_NBD, error
                ) || !start_export(
                    &session->validated_disc,
                    session->info.backend,
                    session,
                    error
                )) {
                return false;
            }
        } else if (gdox_xenia_runtime_target_supported(
                session->info.xenia_policy->runtime,
                GDOX_XENIA_TARGET_IMAGE
            ) && session->info.source == GDOX_MEDIA_DISC_IMAGE
            && session->image_path[0] != '\0') {
            if (!gdox_xenia_target_preflight(
                    GDOX_XENIA_TARGET_IMAGE, error
                )) {
                return false;
            }
            target->kind = GDOX_XENIA_TARGET_IMAGE;
            target->location = session->image_path;
            return true;
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "Xbox 360 media target is unavailable on this system"
            );
            return false;
        }
    }
    if (!gdox_xenia_runtime_target_supported(
            session->info.xenia_policy->runtime,
            GDOX_XENIA_TARGET_PRIVATE_NBD
        ) || session->exported == NULL
        || !gdox_xenia_target_preflight(
            GDOX_XENIA_TARGET_PRIVATE_NBD, error
        ) || !gdox_nbd_inspect_disc(
            session->exported,
            inspect_xenia_disc,
            &session->info,
            error
        )) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "validated Xbox 360 disc export is unavailable"
            );
        }
        return false;
    }
    target->kind = GDOX_XENIA_TARGET_PRIVATE_NBD;
    target->location = gdox_nbd_uri(session->exported);
    target->length = gdox_nbd_length(session->exported);
    return true;
}

bool gdox_runtime_media_close(
    gdox_runtime_media_session *session,
    gdox_error *error
)
{
    gdox_error cleanup_error;
    bool success = true;

    gdox_error_clear(error);
    if (session == NULL) {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "media session is required"
        );
        return false;
    }
    session->open = false;
    if (session->exported != NULL) {
        success = gdox_nbd_close(session->exported, error);
        if (success) {
            session->exported = NULL;
        }
    }
    if (gdox_disc_is_valid(&session->validated_disc)
        && !gdox_disc_close(&session->validated_disc, &cleanup_error)) {
        if (success && error != NULL) {
            *error = cleanup_error;
        }
        success = false;
    }
    if (gdox_source_is_valid(&session->retained_source)
        && !gdox_source_close(&session->retained_source, &cleanup_error)) {
        if (success) {
            *error = cleanup_error;
        }
        success = false;
    }
    if (!gdox_runtime_media_is_owned(session)) {
        memset(session, 0, sizeof(*session));
    }
    return success;
}

const char *gdox_runtime_media_layout_name(
    const gdox_runtime_media_info *info
)
{
    if (info == NULL) {
        return "Disc image";
    }
    if (info->platform == GDOX_MEDIA_PLATFORM_XBOX_360) {
        return gdox_x360_image_layout_name(info->x360.layout);
    }
    switch (info->image_layout) {
        case GDOX_MEDIA_IMAGE_PLAYABLE_XISO:
            return "Playable XISO";
        case GDOX_MEDIA_IMAGE_WHOLE_DISC:
            return "Full-disc image";
        case GDOX_MEDIA_IMAGE_NONE:
            break;
    }
    return "Disc image";
}
