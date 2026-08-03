#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/nbd_telemetry.h"

#include <stdint.h>
#include <string.h>

static uint64_t saturating_add_u64(uint64_t left, uint64_t right)
{
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

static bool physical_delta(
    const gdox_physical_read_stats *before,
    const gdox_physical_read_stats *after,
    gdox_physical_read_stats *delta
)
{
    if (before == NULL || after == NULL
        || after->commands < before->commands
        || after->sectors < before->sectors
        || after->bytes < before->bytes) {
        return false;
    }
    delta->commands = after->commands - before->commands;
    delta->sectors = after->sectors - before->sectors;
    delta->bytes = after->bytes - before->bytes;
    delta->last_lba = after->last_lba;
    return true;
}

void gdox_nbd_telemetry_record(
    gdox_nbd_telemetry *telemetry,
    gdox_mutex *mutex,
    uint64_t offset,
    uint32_t length,
    bool sequence_valid,
    bool succeeded,
    const gdox_physical_read_stats *physical_before,
    const gdox_physical_read_stats *physical_after,
    uint64_t elapsed_ms
)
{
    gdox_physical_read_stats delta;
    const bool physical_known = physical_delta(
        physical_before,
        physical_after,
        &delta
    );
    gdox_nbd_read_stats *stats;

    if (!gdox_mutex_lock(mutex)) {
        return;
    }
    stats = &telemetry->reads;
    stats->requests = saturating_add_u64(stats->requests, 1U);
    stats->requested_bytes = saturating_add_u64(
        stats->requested_bytes,
        length
    );
    stats->service_milliseconds = saturating_add_u64(
        stats->service_milliseconds,
        elapsed_ms
    );
    if (elapsed_ms > stats->maximum_service_milliseconds) {
        stats->maximum_service_milliseconds = elapsed_ms;
    }
    if (sequence_valid) {
        if (telemetry->previous_read_valid) {
            uint64_t *counter = offset == telemetry->previous_read_end
                ? &stats->sequential_requests
                : &stats->discontinuous_requests;

            *counter = saturating_add_u64(*counter, 1U);
        }
        telemetry->previous_read_end = offset + length;
        telemetry->previous_read_valid = true;
    }
    if (succeeded) {
        stats->successful_requests = saturating_add_u64(
            stats->successful_requests,
            1U
        );
        stats->successful_bytes = saturating_add_u64(
            stats->successful_bytes,
            length
        );
    } else {
        stats->failed_requests = saturating_add_u64(
            stats->failed_requests,
            1U
        );
    }
    if (physical_known) {
        stats->physical_commands = saturating_add_u64(
            stats->physical_commands,
            delta.commands
        );
        stats->physical_sectors = saturating_add_u64(
            stats->physical_sectors,
            delta.sectors
        );
        stats->physical_bytes = saturating_add_u64(
            stats->physical_bytes,
            delta.bytes
        );
        if (delta.commands != 0U) {
            stats->requests_with_drive_io = saturating_add_u64(
                stats->requests_with_drive_io,
                1U
            );
        } else if (succeeded) {
            stats->served_without_drive_io_requests = saturating_add_u64(
                stats->served_without_drive_io_requests,
                1U
            );
            stats->served_without_drive_io_bytes = saturating_add_u64(
                stats->served_without_drive_io_bytes,
                length
            );
        }
    }
    gdox_mutex_unlock(mutex);
}

bool gdox_nbd_telemetry_snapshot(
    const gdox_nbd_telemetry *telemetry,
    gdox_mutex *mutex,
    gdox_nbd_read_stats *output
)
{
    if (output == NULL) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    if (telemetry == NULL || !gdox_mutex_lock(mutex)) {
        return false;
    }
    *output = telemetry->reads;
    gdox_mutex_unlock(mutex);
    return true;
}
