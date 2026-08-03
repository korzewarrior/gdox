#include "gdox/preserve.h"

#include "core/preservation_internal.h"
#include "core/ports/preservation_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRESERVATION_READ_BLOCKS 31U
#define PRESERVATION_VERIFY_BUFFER ((size_t)4U * 1024U * 1024U)

typedef struct writer_state {
    const gdox_preservation_request *request;
    const gdox_preservation_input *input;
    const gdox_preservation_map *map;
    gdox_preservation_cancelled_fn cancelled;
    gdox_preservation_progress_fn progress;
    void *callback_context;
    gdox_preservation_result *result;
    gdox_hash_stream *hashes;
    gdox_preservation_file *file;
    uint64_t completed_sectors;
    double started;
    double last_progress;
} writer_state;

static uint64_t output_sector_count(const gdox_preservation_input *input)
{
    return input->output_sectors != 0U
        ? input->output_sectors
        : gdox_source_sector_count(input->source);
}

static bool is_cancelled(const writer_state *state)
{
    return state->cancelled != NULL
        && state->cancelled(state->callback_context);
}

static void report_progress(
    writer_state *state,
    gdox_preservation_phase phase,
    uint64_t completed,
    uint64_t total,
    bool force
)
{
    gdox_preservation_progress progress;
    const double now = gdox_preservation_monotonic_seconds();
    double elapsed;
    if (state->progress == NULL) {
        return;
    }
    if (!force && now - state->last_progress < 0.2) {
        return;
    }
    state->last_progress = now;
    elapsed = now - state->started;
    progress.phase = phase;
    progress.completed_bytes = completed;
    progress.total_bytes = total;
    progress.bytes_per_second = elapsed > 0.001
        ? (double)completed / elapsed
        : 0.0;
    progress.unreadable_sectors = state->result->unreadable_sectors;
    state->progress(state->callback_context, &progress);
}

static char *appended_path(const char *path, const char *suffix)
{
    const size_t path_bytes = strlen(path);
    const size_t suffix_bytes = strlen(suffix);
    char *output;
    if (path_bytes > SIZE_MAX - suffix_bytes - 1U) {
        return NULL;
    }
    output = malloc(path_bytes + suffix_bytes + 1U);
    if (output != NULL) {
        memcpy(output, path, path_bytes);
        memcpy(output + path_bytes, suffix, suffix_bytes + 1U);
    }
    return output;
}

static bool hashes_equal(const gdox_hashes *left, const gdox_hashes *right)
{
    return left->crc32 == right->crc32
        && memcmp(left->md5, right->md5, sizeof(left->md5)) == 0
        && memcmp(left->sha1, right->sha1, sizeof(left->sha1)) == 0
        && memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0;
}

static bool expected_hashes_match(
    const gdox_preservation_map *map,
    const gdox_hashes *actual
)
{
    if ((map->expected_hash_mask & GDOX_EXPECTED_CRC32) != 0U
        && map->expected_hashes.crc32 != actual->crc32) {
        return false;
    }
    if ((map->expected_hash_mask & GDOX_EXPECTED_MD5) != 0U
        && memcmp(map->expected_hashes.md5, actual->md5, sizeof(actual->md5)) != 0) {
        return false;
    }
    if ((map->expected_hash_mask & GDOX_EXPECTED_SHA1) != 0U
        && memcmp(map->expected_hashes.sha1, actual->sha1, sizeof(actual->sha1)) != 0) {
        return false;
    }
    if ((map->expected_hash_mask & GDOX_EXPECTED_SHA256) != 0U
        && memcmp(map->expected_hashes.sha256, actual->sha256, sizeof(actual->sha256)) != 0) {
        return false;
    }
    return true;
}

static bool lba_is_normalized(
    const gdox_preservation_map *map,
    uint64_t lba
)
{
    size_t index;
    if (map == NULL) {
        return false;
    }
    for (index = 0U; index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
        if (lba >= map->ranges[index].start_lba
            && lba <= map->ranges[index].end_lba) {
            return true;
        }
        if (lba < map->ranges[index].start_lba) {
            return false;
        }
    }
    return false;
}

static bool append_bad_sector(
    gdox_preservation_result *result,
    uint64_t lba,
    gdox_error *error
)
{
    gdox_bad_sector_range *ranges;
    if (result->unreadable_range_count != 0U) {
        gdox_bad_sector_range *last =
            &result->unreadable_ranges[result->unreadable_range_count - 1U];
        if (last->end_lba != UINT64_MAX && last->end_lba + 1U == lba) {
            last->end_lba = lba;
            ++result->unreadable_sectors;
            return true;
        }
    }
    if (result->unreadable_range_count >= SIZE_MAX / sizeof(*ranges) - 1U) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "unreadable-sector range list is too large");
        return false;
    }
    ranges = realloc(
        result->unreadable_ranges,
        (result->unreadable_range_count + 1U) * sizeof(*ranges)
    );
    if (ranges == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not record an unreadable sector");
        return false;
    }
    result->unreadable_ranges = ranges;
    ranges[result->unreadable_range_count].start_lba = lba;
    ranges[result->unreadable_range_count].end_lba = lba;
    ++result->unreadable_range_count;
    ++result->unreadable_sectors;
    return true;
}

static bool recover_block(
    writer_state *state,
    uint64_t source_lba,
    uint32_t blocks,
    uint8_t *output,
    gdox_error *error
)
{
    uint32_t index;
    for (index = 0U; index < blocks; ++index) {
        const uint64_t relative_lba = source_lba + index;
        const uint64_t absolute_lba =
            state->input->source_lba_offset + relative_lba;
        uint8_t *sector = output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES;
        gdox_error read_error;
        if (is_cancelled(state)) {
            gdox_error_set(error, GDOX_ERROR_CANCELLED, "preservation was cancelled");
            return false;
        }
        if (lba_is_normalized(state->map, absolute_lba)) {
            memset(sector, 0, GDOX_LOGICAL_SECTOR_BYTES);
            continue;
        }
        if (!gdox_source_read(
                state->input->source,
                relative_lba,
                1U,
                sector,
                GDOX_LOGICAL_SECTOR_BYTES,
                &read_error
            )) {
            if (read_error.code == GDOX_ERROR_NOT_FOUND
                || read_error.code == GDOX_ERROR_CANCELLED) {
                *error = read_error;
                return false;
            }
            memset(sector, 0, GDOX_LOGICAL_SECTOR_BYTES);
            if (!append_bad_sector(state->result, absolute_lba, error)) {
                return false;
            }
        }
    }
    return true;
}

static bool range_intersects_map(
    const gdox_preservation_map *map,
    uint64_t absolute_lba,
    uint32_t blocks
)
{
    uint32_t index;
    for (index = 0U; index < blocks; ++index) {
        if (lba_is_normalized(map, absolute_lba + index)) {
            return true;
        }
    }
    return false;
}

static bool write_image(writer_state *state, gdox_error *error)
{
    const uint64_t sectors = output_sector_count(state->input);
    const uint64_t total_bytes = sectors * GDOX_LOGICAL_SECTOR_BYTES;
    uint8_t *buffer = malloc(
        (size_t)PRESERVATION_READ_BLOCKS * GDOX_LOGICAL_SECTOR_BYTES
    );
    if (buffer == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate preservation read buffer");
        return false;
    }
    while (state->completed_sectors < sectors) {
        const uint64_t remaining = sectors - state->completed_sectors;
        const uint32_t blocks = remaining < PRESERVATION_READ_BLOCKS
            ? (uint32_t)remaining
            : PRESERVATION_READ_BLOCKS;
        const size_t bytes = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
        const uint64_t absolute_lba =
            state->input->source_lba_offset + state->completed_sectors;
        gdox_error read_error;
        bool read_ok;

        if (is_cancelled(state)) {
            free(buffer);
            gdox_error_set(error, GDOX_ERROR_CANCELLED, "preservation was cancelled");
            return false;
        }
        read_ok = !range_intersects_map(state->map, absolute_lba, blocks)
            && gdox_source_read(
                state->input->source,
                state->completed_sectors,
                blocks,
                buffer,
                bytes,
                &read_error
            );
        if (!read_ok && !recover_block(
                state,
                state->completed_sectors,
                blocks,
                buffer,
                error
            )) {
            free(buffer);
            return false;
        }
        if (!gdox_preservation_file_write(state->file, buffer, bytes, error)
            || !gdox_hash_stream_update(state->hashes, buffer, bytes, error)) {
            free(buffer);
            return false;
        }
        state->completed_sectors += blocks;
        report_progress(
            state,
            GDOX_PRESERVATION_READING,
            state->completed_sectors * GDOX_LOGICAL_SECTOR_BYTES,
            total_bytes,
            false
        );
    }
    free(buffer);
    return gdox_hash_stream_finish(state->hashes, &state->result->hashes, error);
}

static bool hash_file_contents(
    gdox_preservation_file *file,
    uint64_t expected_bytes,
    writer_state *state,
    gdox_hash_stream *hashes,
    uint8_t *buffer,
    gdox_hashes *output,
    gdox_error *error
)
{
    uint64_t completed = 0U;

    for (;;) {
        size_t read_bytes = 0U;
        if (is_cancelled(state)) {
            gdox_error_set(
                error,
                GDOX_ERROR_CANCELLED,
                "preservation verification was cancelled"
            );
            return false;
        }
        if (!gdox_preservation_file_read(
            file,
            buffer,
            PRESERVATION_VERIFY_BUFFER,
            &read_bytes,
            error
        )) {
            return false;
        }
        if (read_bytes == 0U) {
            break;
        }
        if (!gdox_hash_stream_update(hashes, buffer, read_bytes, error)) {
            return false;
        }
        completed += read_bytes;
        report_progress(
            state,
            GDOX_PRESERVATION_VERIFYING,
            completed,
            expected_bytes,
            false
        );
    }
    if (completed == expected_bytes
        && gdox_hash_stream_finish(hashes, output, error)) {
        return true;
    }
    if (!gdox_error_is_set(error)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "preservation verification ended at the wrong length"
        );
    }
    return false;
}

static bool hash_file(
    const char *path,
    uint64_t expected_bytes,
    writer_state *state,
    gdox_hashes *output,
    gdox_error *error
)
{
    gdox_preservation_file *file = NULL;
    gdox_hash_stream *hashes = NULL;
    uint8_t *buffer = NULL;
    uint64_t length = 0U;
    bool success = false;
    gdox_error close_error;

    if (!gdox_preservation_file_open_read(path, &file, &length, error)
        || length != expected_bytes) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "preservation output length changed before verification");
        }
        goto cleanup;
    }
    if (!gdox_hash_stream_create(&hashes, error)) {
        goto cleanup;
    }
    buffer = malloc(PRESERVATION_VERIFY_BUFFER);
    if (buffer == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate verification buffer");
        goto cleanup;
    }
    if (!hash_file_contents(
        file,
        expected_bytes,
        state,
        hashes,
        buffer,
        output,
        error
    )) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(buffer);
    gdox_hash_stream_destroy(hashes);
    if (file != NULL && !gdox_preservation_file_close(file, &close_error) && success) {
        *error = close_error;
        success = false;
    }
    return success;
}

static bool ranges_equal(
    const gdox_security_range *left,
    const gdox_security_range *right
)
{
    return memcmp(
        left,
        right,
        sizeof(gdox_security_range) * GDOX_XGD1_SECURITY_RANGE_COUNT
    )
        == 0;
}

static bool resolve_map(
    const gdox_preservation_request *request,
    const gdox_disc_evidence *evidence,
    uint64_t sectors,
    gdox_preservation_map *storage,
    const gdox_preservation_map **output,
    gdox_error *error
)
{
    gdox_security_sector_report report;
    gdox_error inspect_error;
    const gdox_preservation_map *external = request->security_map;
    bool authenticated = false;

    *output = NULL;
    if (request->format != GDOX_PRESERVATION_REDUMP) {
        if (external != NULL) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "security maps apply only to full-disc preservation");
            return false;
        }
        return true;
    }
    if (evidence->security_sector_present
        && gdox_security_sector_inspect(
            evidence->security_sector,
            sizeof(evidence->security_sector),
            &report,
            &inspect_error
        )) {
        memset(storage, 0, sizeof(*storage));
        storage->source = GDOX_SECURITY_MAP_AUTHENTICATED_SS;
        memcpy(storage->ranges, report.ranges, sizeof(storage->ranges));
        authenticated = true;
    }
    if (external != NULL) {
        if (!gdox_security_ranges_validate(external->ranges, error)) {
            return false;
        }
        if (authenticated && !ranges_equal(storage->ranges, external->ranges)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "external security ranges disagree with authenticated SS evidence");
            return false;
        }
        if (authenticated) {
            storage->expected_hashes = external->expected_hashes;
            storage->expected_hash_mask = external->expected_hash_mask;
            (void)snprintf(storage->title, sizeof(storage->title), "%s", external->title);
            (void)snprintf(
                storage->mastering_id,
                sizeof(storage->mastering_id),
                "%s",
                external->mastering_id
            );
            *output = storage;
        } else {
            *output = external;
        }
        return true;
    }
    if (authenticated) {
        *output = storage;
        return true;
    }
    if (gdox_preservation_catalog_match(evidence, sectors, storage, error)) {
        *output = storage;
        return true;
    }
    return !gdox_error_is_set(error);
}

static bool validate_request(
    const gdox_preservation_request *request,
    const gdox_preservation_input *input,
    const gdox_disc_evidence *evidence,
    bool has_map,
    gdox_error *error
)
{
    uint64_t available;
    uint64_t bytes;
    const uint64_t source_sectors = gdox_source_sector_count(input->source);
    const uint64_t sectors = output_sector_count(input);

    if (request->output_path == NULL || request->output_path[0] == '\0'
        || !gdox_source_is_valid(input->source)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "output path and source are required");
        return false;
    }
    if (sectors == 0U || sectors > source_sectors
        || sectors > UINT64_MAX / GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "preservation source has an invalid length");
        return false;
    }
    if (request->format == GDOX_PRESERVATION_REDUMP
        && sectors != GDOX_XGD1_REDUMP_SECTORS) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "full-disc preservation requires exactly 3820880 sectors");
        return false;
    }
    if (request->format != GDOX_PRESERVATION_REDUMP
        && request->format != GDOX_PRESERVATION_XISO_COMPACT) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "unknown preservation format");
        return false;
    }
    if (gdox_preservation_path_exists(request->output_path)
        || !gdox_preservation_sidecars_available(request, evidence, has_map, error)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "preservation output already exists");
        }
        return false;
    }
    bytes = sectors * GDOX_LOGICAL_SECTOR_BYTES;
    if (!gdox_preservation_available_space(request->output_path, &available, error)) {
        return false;
    }
    if (available < bytes) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "the output filesystem does not have enough free space");
        return false;
    }
    return true;
}

void gdox_preservation_result_destroy(gdox_preservation_result *result)
{
    if (result != NULL) {
        free(result->unreadable_ranges);
        memset(result, 0, sizeof(*result));
    }
}

static bool write_and_verify_preservation(
    const char *part_path,
    uint64_t total_bytes,
    writer_state *state,
    gdox_error *error
)
{
    gdox_hashes verified;

    if (!gdox_preservation_file_create(part_path, &state->file, error)
        || !gdox_hash_stream_create(&state->hashes, error)
        || !write_image(state, error)) {
        return false;
    }
    gdox_hash_stream_destroy(state->hashes);
    state->hashes = NULL;
    if (!gdox_preservation_file_sync_close(state->file, error)) {
        state->file = NULL;
        return false;
    }
    state->file = NULL;
    if (!state->request->verify) {
        return true;
    }
    state->started = gdox_preservation_monotonic_seconds();
    state->last_progress = 0.0;
    if (hash_file(part_path, total_bytes, state, &verified, error)
        && hashes_equal(&verified, &state->result->hashes)) {
        state->result->readback_verified = true;
        return true;
    }
    if (!gdox_error_is_set(error)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "read-back hashes do not match the image just written"
        );
    }
    return false;
}

static bool evidence_is_complete(
    const gdox_preservation_request *request,
    const gdox_disc_evidence *evidence,
    const gdox_preservation_map *map,
    const gdox_preservation_result *result
)
{
    return request->verify
        && result->unreadable_sectors == 0U
        && evidence->pfi_present
        && evidence->dmi_present
        && evidence->security_sector_present
        && map != NULL
        && map->source == GDOX_SECURITY_MAP_AUTHENTICATED_SS;
}

static void classify_preservation(
    const gdox_preservation_request *request,
    const gdox_disc_evidence *evidence,
    const gdox_preservation_map *map,
    gdox_preservation_result *result
)
{
    if (map != NULL && map->expected_hash_mask != 0U) {
        result->expected_hashes_match =
            expected_hashes_match(map, &result->hashes) ? 1 : 0;
    }
    if (request->format == GDOX_PRESERVATION_XISO_COMPACT) {
        result->status = GDOX_PRESERVATION_PLAYABLE_XISO;
    } else if (evidence_is_complete(request, evidence, map, result)) {
        result->status = GDOX_PRESERVATION_REDUMP_EVIDENCE_COMPLETE;
    } else {
        result->status = GDOX_PRESERVATION_REDUMP_CANDIDATE;
    }
}

static void cleanup_preservation(
    writer_state *state,
    const gdox_preservation_request *request,
    const char *part_path,
    gdox_preservation_result *result,
    bool success
)
{
    gdox_error cleanup_error;

    gdox_hash_stream_destroy(state->hashes);
    if (state->file != NULL) {
        (void)gdox_preservation_file_close(state->file, &cleanup_error);
    }
    if (success) {
        return;
    }
    if (!request->keep_partial) {
        (void)gdox_preservation_path_remove(part_path);
    }
    free(result->unreadable_ranges);
    result->unreadable_ranges = NULL;
    result->unreadable_range_count = 0U;
}

bool gdox_preservation_run(
    const gdox_preservation_request *request,
    const gdox_preservation_input *input,
    gdox_preservation_cancelled_fn cancelled,
    gdox_preservation_progress_fn progress,
    void *callback_context,
    gdox_preservation_result *result,
    gdox_error *error
)
{
    gdox_disc_evidence evidence;
    gdox_preservation_map map_storage;
    const gdox_preservation_map *map = NULL;
    char *part_path = NULL;
    writer_state state;
    uint64_t sectors;
    uint64_t total_bytes;
    bool success = false;

    memset(&state, 0, sizeof(state));
    gdox_error_clear(error);
    if (request == NULL || input == NULL || result == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "preservation request, input, and result are required");
        return false;
    }
    memset(result, 0, sizeof(*result));
    result->expected_hashes_match = -1;
    gdox_disc_evidence_clear(&evidence);
    (void)gdox_source_evidence(input->source, &evidence);
    if (!resolve_map(
            request,
            &evidence,
            gdox_source_sector_count(input->source),
            &map_storage,
            &map,
            error
        )
        || !validate_request(request, input, &evidence, map != NULL, error)) {
        return false;
    }
    part_path = appended_path(request->output_path, ".part");
    if (part_path == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate partial output path");
        return false;
    }
    if (gdox_preservation_path_exists(part_path)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "partial preservation output already exists");
        goto cleanup;
    }
    sectors = output_sector_count(input);
    total_bytes = sectors * GDOX_LOGICAL_SECTOR_BYTES;
    state.request = request;
    state.input = input;
    state.map = map;
    state.cancelled = cancelled;
    state.progress = progress;
    state.callback_context = callback_context;
    state.result = result;
    state.started = gdox_preservation_monotonic_seconds();
    state.last_progress = 0.0;
    result->format = request->format;
    result->bytes = total_bytes;
    result->evidence = evidence;
    result->normalized_security_sectors =
        map != NULL ? GDOX_XGD1_NORMALIZED_SECTORS : 0U;
    report_progress(
        &state,
        GDOX_PRESERVATION_PREPARING,
        0U,
        total_bytes,
        true
    );

    if (!write_and_verify_preservation(
        part_path,
        total_bytes,
        &state,
        error
    )) {
        goto cleanup;
    }
    classify_preservation(request, &evidence, map, result);
    report_progress(
        &state,
        GDOX_PRESERVATION_FINALIZING,
        total_bytes,
        total_bytes,
        true
    );
    if (!gdox_preservation_path_commit(part_path, request->output_path, error)
        || !gdox_preservation_write_bundle(
            request,
            input,
            map,
            result,
            error
        )) {
        goto cleanup;
    }
    success = true;

cleanup:
    cleanup_preservation(&state, request, part_path, result, success);
    free(part_path);
    return success;
}
