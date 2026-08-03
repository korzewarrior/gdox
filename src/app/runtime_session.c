#include "app/runtime_session.h"

#include "app/physical_media_monitor.h"

#include "app/runtime_media.h"
#include "app/runtime_playback.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool current_auto_start(gdox_runtime *runtime)
{
    bool enabled = true;

    if (gdox_mutex_lock(&runtime->mutex)) {
        enabled = runtime->snapshot.settings.auto_start;
        gdox_mutex_unlock(&runtime->mutex);
    }
    return enabled;
}

static void close_invalid_media_session(
    gdox_runtime *runtime,
    const char *message,
    gdox_error *error
)
{
    gdox_error invalid_error;
    gdox_error cleanup_error;

    gdox_error_set(&invalid_error, GDOX_ERROR_INTERNAL, message);
    if (!gdox_runtime_media_close(&runtime->media, &cleanup_error)) {
        *error = cleanup_error;
    } else {
        *error = invalid_error;
    }
}

bool gdox_runtime_session_refresh_read_stats(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    bool publish
)
{
    gdox_physical_read_stats stats;
    gdox_nbd_read_stats read_stats;
    bool physical_changed;
    bool changed;

    if (runtime->media.exported == NULL
        || !gdox_nbd_physical_read_stats(runtime->media.exported, &stats)
        || !gdox_nbd_get_read_stats(
            runtime->media.exported, &read_stats
        )) {
        return false;
    }
    physical_changed = stats.commands != snapshot->physical_read_commands
        || stats.sectors != snapshot->physical_read_sectors
        || stats.bytes != snapshot->physical_read_bytes
        || stats.last_lba != snapshot->physical_last_lba;
    changed = physical_changed
        || memcmp(
            &read_stats,
            &snapshot->nbd_read_stats,
            sizeof(read_stats)
        ) != 0;
    snapshot->physical_read_commands = stats.commands;
    snapshot->physical_read_sectors = stats.sectors;
    snapshot->physical_read_bytes = stats.bytes;
    snapshot->physical_last_lba = stats.last_lba;
    snapshot->nbd_read_stats = read_stats;
    if (physical_changed) {
        (void)fprintf(
            stderr,
            "GDOX: physical optical reads: commands=%" PRIu64
            " sectors=%" PRIu64 " bytes=%" PRIu64 " last_lba=%" PRIu64 "\n",
            stats.commands,
            stats.sectors,
            stats.bytes,
            stats.last_lba
        );
        (void)fflush(stderr);
    }
    if (changed && publish) {
        gdox_runtime_publish(runtime, snapshot);
    }
    return true;
}

bool gdox_runtime_session_close(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    bool closed;

    if (!gdox_runtime_media_is_owned(&runtime->media)) {
        return true;
    }
    if (runtime->media.open) {
        (void)gdox_runtime_session_refresh_read_stats(
            runtime, snapshot, false
        );
    }
    closed = gdox_runtime_media_close(&runtime->media, error);
    if (!gdox_runtime_media_is_owned(&runtime->media)) {
        gdox_runtime_playback_reset_xenia(runtime, snapshot);
    }
    if (!closed) {
        return false;
    }
    gdox_runtime_reset_media_snapshot(snapshot, snapshot->media_source);
    snapshot->phase = GDOX_RUNTIME_EMPTY;
    if (snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC) {
        gdox_runtime_copy_text(
            snapshot->disc, sizeof(snapshot->disc), "No Xbox disc"
        );
        gdox_runtime_copy_text(
            snapshot->status,
            sizeof(snapshot->status),
            "Waiting for an Xbox disc"
        );
        gdox_runtime_copy_text(
            snapshot->notice, sizeof(snapshot->notice), "Drive is ready"
        );
    } else {
        gdox_runtime_copy_text(
            snapshot->status, sizeof(snapshot->status), "Disc image closed"
        );
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Select the image again to reopen it"
        );
    }
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, false, false);
    gdox_runtime_publish(runtime, snapshot);
    return true;
}

gdox_runtime_live_prepare_result gdox_runtime_session_prepare_live(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    bool force_launch,
    gdox_error *error
)
{
    gdox_runtime_media_open_result result;

    gdox_runtime_playback_reset_xenia(runtime, snapshot);
    gdox_runtime_reset_media_snapshot(
        snapshot, GDOX_MEDIA_PHYSICAL_DISC
    );
    snapshot->phase = GDOX_RUNTIME_PREPARING;
    gdox_runtime_copy_text(
        snapshot->disc, sizeof(snapshot->disc), "Reading Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status, sizeof(snapshot->status), "Preparing live disc"
    );
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, false, false);
    gdox_runtime_publish(runtime, snapshot);
    if (!gdox_runtime_media_open_physical(
            runtime->optical_drive, &runtime->media, &result, error
        )) {
        (void)gdox_runtime_apply_media_open_result(snapshot, &result);
        gdox_runtime_attention(
            runtime, snapshot, "Could not prepare disc", error, false, false
        );
        snapshot->can_start = gdox_runtime_media_open_can_retry(
            error->code,
            gdox_runtime_media_is_owned(&runtime->media),
            true
        );
        gdox_runtime_publish(runtime, snapshot);
        return result.state == GDOX_RUNTIME_MEDIA_UNIDENTIFIED
                && (error->code == GDOX_ERROR_NOT_FOUND
                    || error->code == GDOX_ERROR_TRANSPORT)
            ? GDOX_RUNTIME_LIVE_RETRYABLE_FAILURE
            : GDOX_RUNTIME_LIVE_TERMINAL_FAILURE;
    }
    if (!gdox_runtime_apply_media_open_result(snapshot, &result)) {
        close_invalid_media_session(
            runtime,
            "opened physical media has no valid platform identity",
            error
        );
        gdox_runtime_attention(
            runtime, snapshot, "Could not prepare disc", error, false, false
        );
        return GDOX_RUNTIME_LIVE_TERMINAL_FAILURE;
    }
    gdox_runtime_playback_prepare(runtime, snapshot);
    snapshot->phase = GDOX_RUNTIME_READY;
    (void)gdox_runtime_session_refresh_read_stats(runtime, snapshot, false);
    gdox_runtime_copy_text(
        snapshot->status, sizeof(snapshot->status), "Disc ready"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Live physical-disc session is active"
    );
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, true, false);
    gdox_runtime_publish(runtime, snapshot);
    if ((force_launch || current_auto_start(runtime))
        && gdox_runtime_playback_ready(snapshot)
        && !gdox_runtime_playback_start(runtime, snapshot, error)) {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not start playback",
            error,
            true,
            gdox_runtime_playback_running(runtime)
        );
    }
    return GDOX_RUNTIME_LIVE_PREPARED;
}

bool gdox_runtime_session_prepare_image(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *path,
    bool launch,
    gdox_error *error
)
{
    gdox_runtime_media_open_result result;

    gdox_runtime_playback_reset_xenia(runtime, snapshot);
    gdox_runtime_reset_media_snapshot(snapshot, GDOX_MEDIA_DISC_IMAGE);
    gdox_runtime_copy_text(
        snapshot->disc_image_path, sizeof(snapshot->disc_image_path), path
    );
    snapshot->phase = GDOX_RUNTIME_PREPARING;
    gdox_runtime_copy_text(
        snapshot->drive, sizeof(snapshot->drive), "Read-only disc image"
    );
    gdox_runtime_copy_text(
        snapshot->disc, sizeof(snapshot->disc), "Reading Xbox disc image"
    );
    gdox_runtime_copy_text(
        snapshot->status, sizeof(snapshot->status), "Preparing disc image"
    );
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, false, false);
    gdox_runtime_publish(runtime, snapshot);
    if (!gdox_runtime_media_open_image(
            path, &runtime->media, &result, error
        )) {
        (void)gdox_runtime_apply_media_open_result(snapshot, &result);
        gdox_runtime_attention(
            runtime, snapshot, "Could not open disc image", error, false, false
        );
        snapshot->can_start = gdox_runtime_media_open_can_retry(
            error->code,
            gdox_runtime_media_is_owned(&runtime->media),
            snapshot->disc_image_path[0] != '\0'
        );
        gdox_runtime_publish(runtime, snapshot);
        return false;
    }
    if (!gdox_runtime_apply_media_open_result(snapshot, &result)) {
        close_invalid_media_session(
            runtime,
            "opened disc image has no valid platform identity",
            error
        );
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not open disc image",
            error,
            false,
            false
        );
        return false;
    }
    (void)snprintf(
        snapshot->drive,
        sizeof(snapshot->drive),
        "Disc image · %s",
        gdox_runtime_media_layout_name(&runtime->media.info)
    );
    gdox_runtime_playback_prepare(runtime, snapshot);
    snapshot->phase = GDOX_RUNTIME_READY;
    gdox_runtime_copy_text(
        snapshot->status, sizeof(snapshot->status), "Disc image ready"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Read-only disc-image session is active"
    );
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, true, false);
    gdox_runtime_publish(runtime, snapshot);
    if (launch && gdox_runtime_playback_ready(snapshot)
        && !gdox_runtime_playback_start(runtime, snapshot, error)) {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Could not start playback",
            error,
            true,
            gdox_runtime_playback_running(runtime)
        );
    }
    return true;
}

void gdox_runtime_session_select_physical(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor
)
{
    gdox_runtime_playback_reset_xenia(runtime, snapshot);
    gdox_runtime_reset_media_snapshot(
        snapshot, GDOX_MEDIA_PHYSICAL_DISC
    );
    snapshot->phase = GDOX_RUNTIME_DISCOVERING;
    runtime->optical_drive = GDOX_OPTICAL_DRIVE_NONE;
    gdox_runtime_copy_text(
        snapshot->drive, sizeof(snapshot->drive), "Checking optical drive"
    );
    gdox_runtime_copy_text(
        snapshot->disc, sizeof(snapshot->disc), "No Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Looking for a physical disc"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Physical discs are the default source"
    );
    gdox_runtime_set_controls(snapshot, runtime->optical_drive, false, false);
    gdox_optical_monitor_retry(monitor);
    gdox_runtime_publish(runtime, snapshot);
}

bool gdox_runtime_session_end_physical(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_runtime_physical_end_reason reason,
    uint64_t eject_generation,
    bool *eject_authorized,
    const gdox_error *observation_error,
    gdox_optical_monitor *optical_monitor
)
{
    gdox_error playback_error;
    gdox_error media_error;
    gdox_error notice;
    bool playback_clean = true;
    bool media_clean = true;
    bool cleanup_pending;

    if (eject_authorized != NULL) {
        *eject_authorized = false;
    }

    if (gdox_runtime_playback_running(runtime)) {
        playback_clean = gdox_runtime_playback_stop(
            runtime, snapshot, &playback_error
        );
    }
    if (reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED
        && !gdox_runtime_playback_running(runtime)
        && runtime->media.exported != NULL) {
        gdox_media_observation observation = {0};
        const bool current = gdox_nbd_observe_media(
            runtime->media.exported, &observation
        ) && gdox_physical_media_eject_request_matches(
            &observation, eject_generation
        );

        if (eject_authorized != NULL) {
            *eject_authorized = current;
        }
        if (!current) {
            reason = GDOX_RUNTIME_PHYSICAL_MEDIA_CHANGED;
        }
    }
    if (!gdox_runtime_playback_running(runtime)
        && gdox_runtime_media_is_owned(&runtime->media)) {
        media_clean = gdox_runtime_session_close(
            runtime, snapshot, &media_error
        );
    }
    if (!playback_clean) {
        (void)snprintf(
            notice.message,
            sizeof(notice.message),
            "physical media changed; playback cleanup reported: %.307s",
            playback_error.message
        );
        notice.code = playback_error.code;
    } else if (!media_clean) {
        (void)snprintf(
            notice.message,
            sizeof(notice.message),
            "physical media changed; media cleanup reported: %.313s",
            media_error.message
        );
        notice.code = media_error.code;
    } else if (gdox_runtime_playback_running(runtime)) {
        gdox_error_set(
            &notice,
            GDOX_ERROR_IO,
            "playback is still active while the physical session is ending"
        );
    } else if (observation_error != NULL
        && gdox_error_is_set(observation_error)) {
        (void)snprintf(
            notice.message,
            sizeof(notice.message),
            "physical media status failed: %.346s",
            observation_error->message
        );
        notice.code = GDOX_ERROR_TRANSPORT;
    } else if (reason == GDOX_RUNTIME_PHYSICAL_MEDIA_CHANGED) {
        gdox_error_set(
            &notice,
            GDOX_ERROR_NOT_FOUND,
            "the disc changed; playback was stopped and the drive was released"
        );
    } else if (reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED) {
        gdox_error_set(
            &notice,
            GDOX_ERROR_NOT_FOUND,
            "the drive eject button was pressed; playback was stopped"
        );
    } else if (reason == GDOX_RUNTIME_PHYSICAL_SESSION_FAILED) {
        gdox_error_set(
            &notice,
            GDOX_ERROR_IO,
            "the live disc session failed and was safely closed"
        );
    } else {
        gdox_error_set(
            &notice,
            GDOX_ERROR_NOT_FOUND,
            "the physical optical drive was disconnected; playback was stopped"
        );
    }
    cleanup_pending = gdox_runtime_media_is_owned(&runtime->media);
    if (cleanup_pending
        || reason == GDOX_RUNTIME_PHYSICAL_EJECT_REQUESTED) {
        gdox_optical_monitor_block(optical_monitor);
    } else if (reason == GDOX_RUNTIME_PHYSICAL_SESSION_FAILED) {
        gdox_optical_monitor_fail(
            optical_monitor, GDOX_OPTICAL_MONITOR_FAILURE_TERMINAL
        );
    } else {
        gdox_optical_monitor_session_ended(optical_monitor);
    }
    if (!cleanup_pending && playback_clean && media_clean
        && reason == GDOX_RUNTIME_PHYSICAL_MEDIA_CHANGED) {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Disc change detected; waiting for the replacement disc"
        );
        gdox_runtime_publish(runtime, snapshot);
    } else {
        gdox_runtime_attention(
            runtime,
            snapshot,
            "Live session ended",
            &notice,
            runtime->media.open,
            gdox_runtime_playback_running(runtime)
        );
    }
    return cleanup_pending;
}
