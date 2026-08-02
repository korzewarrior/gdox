#include "app/runtime_internal.h"

#include "gdox/optical.h"
#include "gdox/xdvdfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool preservation_is_cancelled(void *context)
{
    const gdox_runtime *runtime = context;
    return atomic_load_explicit(
        &runtime->stopping,
        memory_order_acquire
    )
        || atomic_load_explicit(
            &runtime->preservation_cancelled,
            memory_order_acquire
        );
}

static void preservation_progress(
    void *context,
    const gdox_preservation_progress *progress
)
{
    gdox_runtime *runtime = context;
    if (gdox_mutex_lock(&runtime->mutex)) {
        runtime->snapshot.phase = GDOX_RUNTIME_PRESERVING;
        runtime->snapshot.can_start = false;
        runtime->snapshot.can_restart = false;
        runtime->snapshot.can_close = false;
        runtime->snapshot.can_eject = false;
        runtime->snapshot.can_preserve = false;
        runtime->snapshot.can_cancel_preservation = true;
        runtime->snapshot.preservation_phase = progress->phase;
        runtime->snapshot.preservation_completed_bytes =
            progress->completed_bytes;
        runtime->snapshot.preservation_total_bytes =
            progress->total_bytes;
        runtime->snapshot.preservation_bytes_per_second =
            progress->bytes_per_second;
        runtime->snapshot.preservation_unreadable_sectors =
            progress->unreadable_sectors;
        gdox_runtime_copy_text(
            runtime->snapshot.status,
            sizeof(runtime->snapshot.status),
            progress->phase == GDOX_PRESERVATION_VERIFYING
                ? "Verifying preservation image"
                : "Preserving disc"
        );
        gdox_mutex_unlock(&runtime->mutex);
    }
}

typedef struct preservation_session {
    gdox_sector_source whole;
    gdox_sector_source partition;
    gdox_sector_source patched;
    gdox_sector_source compact;
    gdox_sector_source *source;
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_metadata metadata;
    gdox_byte_patch *patches;
    size_t patch_count;
    gdox_xdvdfs_compact_stats compact_stats;
    gdox_preservation_result result;
    bool result_available;
} preservation_session;

static void preservation_session_initialize(preservation_session *session)
{
    memset(session, 0, sizeof(*session));
    session->source = &session->whole;
    session->metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
}

static bool read_preservation_request(
    const gdox_runtime_request_entry *queued,
    gdox_preservation_format *format,
    bool *verify,
    char output_path[GDOX_EMULATOR_PATH_CAPACITY],
    gdox_error *error
)
{
    if (queued == NULL || queued->kind != GDOX_RUNTIME_REQUEST_PRESERVE
        || queued->path[0] == '\0') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "preservation request is unavailable"
        );
        return false;
    }
    *format = queued->preservation_format;
    *verify = queued->preservation_verify;
    gdox_runtime_copy_text(
        output_path,
        GDOX_EMULATOR_PATH_CAPACITY,
        queued->path
    );
    return true;
}

static void publish_preservation_start(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *output_path
)
{
    snapshot->phase = GDOX_RUNTIME_PRESERVING;
    snapshot->preservation_complete = false;
    snapshot->can_start = false;
    snapshot->can_restart = false;
    snapshot->can_close = false;
    snapshot->can_eject = false;
    snapshot->can_preserve = false;
    snapshot->can_cancel_preservation = true;
    snapshot->preservation_completed_bytes = 0U;
    snapshot->preservation_total_bytes = 0U;
    snapshot->preservation_bytes_per_second = 0.0;
    snapshot->preservation_unreadable_sectors = 0U;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Preparing preservation"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Opening the physical disc"
    );
    gdox_runtime_copy_text(
        snapshot->preservation_output,
        sizeof(snapshot->preservation_output),
        output_path
    );
    gdox_runtime_publish(runtime, snapshot);
}

static bool inspect_preservation_source(
    gdox_runtime *runtime,
    preservation_session *session,
    gdox_error *error
)
{
    return gdox_optical_open(
            runtime->optical_drive,
            3U,
            30000U,
            &session->whole,
            error
        )
        && gdox_xdvdfs_find_volume(
            &session->whole,
            &session->volume,
            error
        )
        && gdox_xdvdfs_inspect(
            &session->whole,
            &session->volume,
            &session->metadata,
            error
        );
}

static bool build_compact_source(
    preservation_session *session,
    gdox_error *error
)
{
    gdox_xdvdfs_volume partition_volume;

    if (!gdox_xdvdfs_collect_media_patches(
            &session->whole,
            &session->metadata,
            &session->patches,
            &session->patch_count,
            error
        ) || !gdox_source_make_partition(
            &session->whole,
            session->volume.base_lba,
            &session->partition,
            error
        ) || !gdox_source_make_patched(
            &session->partition,
            session->patches,
            session->patch_count,
            &session->patched,
            error
        )) {
        return false;
    }
    partition_volume = session->volume;
    partition_volume.base_lba = 0U;
    if (!gdox_source_make_compact_xiso(
            &session->patched,
            &partition_volume,
            &session->compact,
            &session->compact_stats,
            error
        )) {
        return false;
    }
    session->source = &session->compact;
    return true;
}

static bool run_preservation(
    gdox_runtime *runtime,
    preservation_session *session,
    gdox_preservation_format format,
    const char *output_path,
    bool verify,
    gdox_error *error
)
{
    const gdox_preservation_request request = {
        format,
        output_path,
        verify,
        false,
        NULL,
    };
    const gdox_preservation_input input = {
        session->source,
        format == GDOX_PRESERVATION_REDUMP
            ? 0U
            : session->volume.base_lba,
        session->metadata.title != NULL
            ? session->metadata.title
            : "Original Xbox disc",
        session->metadata.title_id_present,
        session->metadata.title_id,
        "validated physical optical source",
        session->patch_count,
        0U,
        format == GDOX_PRESERVATION_XISO_COMPACT,
    };

    if (!gdox_preservation_run(
            &request,
            &input,
            preservation_is_cancelled,
            preservation_progress,
            runtime,
            &session->result,
            error
        )) {
        return false;
    }
    session->result_available = true;
    return true;
}

static bool close_preservation_source(
    preservation_session *session,
    gdox_error *error
)
{
    if (gdox_source_is_valid(&session->compact)) {
        return gdox_source_close(&session->compact, error);
    }
    if (gdox_source_is_valid(&session->patched)) {
        return gdox_source_close(&session->patched, error);
    }
    if (gdox_source_is_valid(&session->partition)) {
        return gdox_source_close(&session->partition, error);
    }
    if (gdox_source_is_valid(&session->whole)) {
        return gdox_source_close(&session->whole, error);
    }
    return true;
}

static bool preservation_session_finish(
    preservation_session *session,
    bool success,
    gdox_error *error
)
{
    gdox_error cleanup_error;

    gdox_error_clear(&cleanup_error);
    free(session->patches);
    gdox_xdvdfs_metadata_destroy(&session->metadata);
    if (!close_preservation_source(session, &cleanup_error)) {
        if (success) {
            *error = cleanup_error;
        }
        success = false;
    }
    if (!success && session->result_available) {
        gdox_preservation_result_destroy(&session->result);
        session->result_available = false;
    }
    return success;
}

static void publish_preservation_complete(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_preservation_result *result
)
{
    gdox_runtime_copy_snapshot(runtime, snapshot);
    snapshot->phase = GDOX_RUNTIME_PRESERVED;
    snapshot->preservation_complete = true;
    snapshot->can_start = true;
    snapshot->can_restart = false;
    snapshot->can_close = false;
    snapshot->can_eject =
        gdox_optical_drive_can_eject(runtime->optical_drive);
    snapshot->can_preserve = true;
    snapshot->can_cancel_preservation = false;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Preservation complete"
    );
    if (result->expected_hashes_match == 1) {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Verified image matches every available catalog hash"
        );
    } else if (result->unreadable_sectors == 0U) {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Verified image completed without unexpected unreadable sectors"
        );
    } else {
        (void)snprintf(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Image completed with %llu unexpected unreadable sectors",
            (unsigned long long)result->unreadable_sectors
        );
    }
    gdox_runtime_publish(runtime, snapshot);
}

bool gdox_runtime_run_preservation(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_request_entry *queued_request,
    gdox_error *error
)
{
    preservation_session session;
    gdox_preservation_format format;
    bool verify;
    char output_path[GDOX_EMULATOR_PATH_CAPACITY];
    bool success;

    preservation_session_initialize(&session);
    if (!read_preservation_request(
            queued_request,
            &format,
            &verify,
            output_path,
            error
        )) {
        return false;
    }
    publish_preservation_start(runtime, snapshot, output_path);
    success = inspect_preservation_source(runtime, &session, error);
    if (success && format == GDOX_PRESERVATION_XISO_COMPACT) {
        success = build_compact_source(&session, error);
    }
    if (success) {
        success = run_preservation(
            runtime,
            &session,
            format,
            output_path,
            verify,
            error
        );
    }
    if (!preservation_session_finish(&session, success, error)) {
        return false;
    }
    publish_preservation_complete(runtime, snapshot, &session.result);
    gdox_preservation_result_destroy(&session.result);
    runtime->preservation_hold = true;
    return true;
}
