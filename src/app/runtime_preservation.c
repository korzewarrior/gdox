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

bool gdox_runtime_run_preservation(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    gdox_sector_source whole = {0};
    gdox_sector_source partition = {0};
    gdox_sector_source patched = {0};
    gdox_sector_source compact = {0};
    gdox_sector_source *source = &whole;
    gdox_xdvdfs_volume volume;
    gdox_xdvdfs_volume partition_volume;
    gdox_xdvdfs_metadata metadata;
    gdox_byte_patch *patches = NULL;
    size_t patch_count = 0U;
    gdox_xdvdfs_compact_stats compact_stats = {0};
    gdox_preservation_request request;
    gdox_preservation_input input;
    gdox_preservation_result result = {0};
    gdox_error cleanup_error;
    gdox_preservation_format format;
    bool verify;
    char output_path[sizeof(runtime->pending_preservation_path)];
    bool success = false;
    bool result_available = false;
    bool closed = true;

    memset(&metadata, 0, sizeof(metadata));
    metadata.default_xbe_index = GDOX_XDVDFS_NO_ENTRY;
    gdox_error_clear(&cleanup_error);
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not read preservation request"
        );
        return false;
    }
    format = runtime->pending_preservation_format;
    verify = runtime->pending_preservation_verify;
    gdox_runtime_copy_text(
        output_path,
        sizeof(output_path),
        runtime->pending_preservation_path
    );
    gdox_mutex_unlock(&runtime->mutex);

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

    if (!gdox_optical_open(
            runtime->optical_drive,
            3U,
            30000U,
            &whole,
            error
        )
        || !gdox_xdvdfs_find_volume(&whole, &volume, error)
        || !gdox_xdvdfs_inspect(&whole, &volume, &metadata, error)) {
        goto cleanup;
    }
    if (format == GDOX_PRESERVATION_XISO_COMPACT) {
        if (!gdox_xdvdfs_collect_media_patches(
                &whole,
                &metadata,
                &patches,
                &patch_count,
                error
            )
            || !gdox_source_make_partition(
                &whole,
                volume.base_lba,
                &partition,
                error
            )
            || !gdox_source_make_patched(
                &partition,
                patches,
                patch_count,
                &patched,
                error
            )) {
            goto cleanup;
        }
        partition_volume = volume;
        partition_volume.base_lba = 0U;
        if (!gdox_source_make_compact_xiso(
                &patched,
                &partition_volume,
                &compact,
                &compact_stats,
                error
            )) {
            goto cleanup;
        }
        source = &compact;
    }
    request = (gdox_preservation_request){
        format,
        output_path,
        verify,
        false,
        NULL,
    };
    input = (gdox_preservation_input){
        source,
        format == GDOX_PRESERVATION_REDUMP ? 0U : volume.base_lba,
        metadata.title != NULL ? metadata.title : "Original Xbox disc",
        metadata.title_id_present,
        metadata.title_id,
        "validated physical optical source",
        patch_count,
        0U,
        format == GDOX_PRESERVATION_XISO_COMPACT,
    };
    if (!gdox_preservation_run(
            &request,
            &input,
            preservation_is_cancelled,
            preservation_progress,
            runtime,
            &result,
            error
        )) {
        goto cleanup;
    }
    success = true;
    result_available = true;

cleanup:
    free(patches);
    gdox_xdvdfs_metadata_destroy(&metadata);
    if (gdox_source_is_valid(&compact)) {
        closed = gdox_source_close(&compact, &cleanup_error);
    } else if (gdox_source_is_valid(&patched)) {
        closed = gdox_source_close(&patched, &cleanup_error);
    } else if (gdox_source_is_valid(&partition)) {
        closed = gdox_source_close(&partition, &cleanup_error);
    } else if (gdox_source_is_valid(&whole)) {
        closed = gdox_source_close(&whole, &cleanup_error);
    }
    if (!closed) {
        if (success) {
            *error = cleanup_error;
        }
        success = false;
    }
    if (!success) {
        if (result_available) {
            gdox_preservation_result_destroy(&result);
        }
        return false;
    }

    gdox_runtime_copy_snapshot(runtime, snapshot);
    snapshot->phase = GDOX_RUNTIME_PRESERVED;
    snapshot->preservation_complete = true;
    snapshot->can_start = true;
    snapshot->can_restart = false;
    snapshot->can_close = false;
    snapshot->can_eject = true;
    snapshot->can_preserve = true;
    snapshot->can_cancel_preservation = false;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Preservation complete"
    );
    if (result.expected_hashes_match == 1) {
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Verified image matches every available catalog hash"
        );
    } else if (result.unreadable_sectors == 0U) {
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
            (unsigned long long)result.unreadable_sectors
        );
    }
    gdox_runtime_publish(runtime, snapshot);
    gdox_preservation_result_destroy(&result);
    runtime->preservation_hold = true;
    return true;
}
