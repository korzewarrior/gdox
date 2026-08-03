#include "gdox/source.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct gdox_partition_context {
    gdox_sector_source inner;
    uint64_t base_lba;
    uint64_t sectors;
} gdox_partition_context;

typedef struct gdox_indexed_patch {
    gdox_byte_patch patch;
    size_t declaration_index;
} gdox_indexed_patch;

typedef struct gdox_patch_context {
    gdox_sector_source inner;
    gdox_byte_patch *patches;
    size_t patch_count;
} gdox_patch_context;

gdox_removable_session_status gdox_removable_session_classify(
    const gdox_media_observation *observation,
    bool generation_known,
    uint64_t expected_generation
)
{
    if (observation == NULL) {
        return GDOX_REMOVABLE_SESSION_UNAVAILABLE;
    }
    if ((generation_known
            && observation->generation != expected_generation)
        || observation->event == GDOX_MEDIA_EVENT_NEW_MEDIA
        || observation->event == GDOX_MEDIA_EVENT_REMOVAL
        || observation->event == GDOX_MEDIA_EVENT_CHANGED) {
        return GDOX_REMOVABLE_SESSION_CHANGED;
    }
    if (observation->event == GDOX_MEDIA_EVENT_EJECT_REQUEST) {
        return GDOX_REMOVABLE_SESSION_EJECT_REQUESTED;
    }
    return observation->readiness == GDOX_MEDIA_READINESS_PRESENT
        ? GDOX_REMOVABLE_SESSION_PRESENT
        : GDOX_REMOVABLE_SESSION_UNAVAILABLE;
}

void gdox_disc_evidence_clear(gdox_disc_evidence *evidence)
{
    if (evidence != NULL) {
        memset(evidence, 0, sizeof(*evidence));
    }
}

bool gdox_source_is_valid(const gdox_sector_source *source)
{
    return source != NULL && source->context != NULL && source->ops != NULL
        && source->ops->sector_count != NULL && source->ops->read != NULL
        && source->ops->close != NULL;
}

uint64_t gdox_source_sector_count(const gdox_sector_source *source)
{
    if (!gdox_source_is_valid(source)) {
        return 0U;
    }
    return source->ops->sector_count(source->context);
}

bool gdox_source_validate_read(
    uint64_t sectors,
    uint64_t lba,
    uint32_t blocks,
    size_t output_bytes,
    gdox_error *error
)
{
    const uint64_t expected = (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;

    gdox_error_clear(error);
    if (blocks == 0U) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "sector read must contain at least one block");
        return false;
    }
    if (expected > SIZE_MAX || output_bytes != (size_t)expected) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "sector read buffer has the wrong size");
        return false;
    }
    if (lba > sectors || (uint64_t)blocks > sectors - lba) {
        gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "sector read is outside the source");
        return false;
    }
    return true;
}

bool gdox_source_read(
    gdox_sector_source *source,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (!gdox_source_is_valid(source) || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid source and output are required");
        return false;
    }
    if (!gdox_source_validate_read(
            source->ops->sector_count(source->context),
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    return source->ops->read(source->context, lba, blocks, output, output_bytes, error);
}

bool gdox_source_observe_media(
    const gdox_sector_source *source,
    gdox_media_observation *output
)
{
    if (output == NULL) {
        return false;
    }
    output->readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    output->generation = 0U;
    output->event = GDOX_MEDIA_EVENT_NONE;
    if (!gdox_source_is_valid(source)) {
        return false;
    }
    if (source->ops->observe_media != NULL) {
        source->ops->observe_media(source->context, output);
        if (output->readiness < GDOX_MEDIA_READINESS_UNKNOWN
            || output->readiness > GDOX_MEDIA_READINESS_PRESENT) {
            output->readiness = GDOX_MEDIA_READINESS_UNKNOWN;
        }
        if (output->event < GDOX_MEDIA_EVENT_NONE
            || output->event > GDOX_MEDIA_EVENT_CHANGED) {
            output->event = GDOX_MEDIA_EVENT_NONE;
        }
        return true;
    }
    output->readiness = source->ops->media_present == NULL
        || source->ops->media_present(source->context)
        ? GDOX_MEDIA_READINESS_PRESENT
        : GDOX_MEDIA_READINESS_ABSENT;
    return true;
}

bool gdox_source_media_present(const gdox_sector_source *source)
{
    gdox_media_observation observation;
    return gdox_source_observe_media(source, &observation)
        && observation.readiness == GDOX_MEDIA_READINESS_PRESENT;
}

bool gdox_source_evidence(
    const gdox_sector_source *source,
    gdox_disc_evidence *output
)
{
    if (output == NULL) {
        return false;
    }
    gdox_disc_evidence_clear(output);
    if (!gdox_source_is_valid(source) || source->ops->evidence == NULL) {
        return false;
    }
    return source->ops->evidence(source->context, output);
}

bool gdox_source_physical_read_stats(
    const gdox_sector_source *source,
    gdox_physical_read_stats *output
)
{
    if (output == NULL) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    if (!gdox_source_is_valid(source)
        || source->ops->physical_read_stats == NULL) {
        return false;
    }
    return source->ops->physical_read_stats(source->context, output);
}

void gdox_source_abort(gdox_sector_source *source)
{
    if (gdox_source_is_valid(source) && source->ops->abort != NULL) {
        source->ops->abort(source->context);
    }
}

bool gdox_source_prepare_close(gdox_sector_source *source, gdox_error *error)
{
    gdox_error_clear(error);
    if (!gdox_source_is_valid(source)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "source is not open");
        return false;
    }
    if (source->ops->prepare_close == NULL) {
        return true;
    }
    return source->ops->prepare_close(source->context, error);
}

bool gdox_source_close(gdox_sector_source *source, gdox_error *error)
{
    void *context;
    const gdox_sector_source_ops *ops;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(source)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "source is not open");
        return false;
    }
    if (!gdox_source_prepare_close(source, error)) {
        return false;
    }
    context = source->context;
    ops = source->ops;
    source->context = NULL;
    source->ops = NULL;
    return ops->close(context, error);
}

void gdox_source_destroy(gdox_sector_source *source)
{
    gdox_error ignored;
    if (gdox_source_is_valid(source)) {
        (void)gdox_source_close(source, &ignored);
    }
}

static uint64_t partition_sector_count(const void *context)
{
    const gdox_partition_context *partition = context;
    return partition->sectors;
}

static bool partition_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_partition_context *partition = context;

    if (!gdox_source_validate_read(
            partition->sectors,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    return gdox_source_read(
        &partition->inner,
        partition->base_lba + lba,
        blocks,
        output,
        output_bytes,
        error
    );
}

static bool partition_media_present(const void *context)
{
    const gdox_partition_context *partition = context;
    return gdox_source_media_present(&partition->inner);
}

static void partition_observe_media(
    const void *context,
    gdox_media_observation *output
)
{
    const gdox_partition_context *partition = context;
    (void)gdox_source_observe_media(&partition->inner, output);
}

static bool partition_evidence(
    const void *context,
    gdox_disc_evidence *output
)
{
    const gdox_partition_context *partition = context;
    return gdox_source_evidence(&partition->inner, output);
}

static bool partition_physical_read_stats(
    const void *context,
    gdox_physical_read_stats *output
)
{
    const gdox_partition_context *partition = context;
    return gdox_source_physical_read_stats(&partition->inner, output);
}

static bool partition_close(void *context, gdox_error *error)
{
    gdox_partition_context *partition = context;
    const bool closed = gdox_source_close(&partition->inner, error);
    free(partition);
    return closed;
}

static bool partition_prepare_close(void *context, gdox_error *error)
{
    gdox_partition_context *partition = context;
    return gdox_source_prepare_close(&partition->inner, error);
}

static void partition_abort(void *context)
{
    gdox_partition_context *partition = context;
    gdox_source_abort(&partition->inner);
}

static const gdox_sector_source_ops partition_ops = {
    partition_sector_count,
    partition_read,
    partition_media_present,
    partition_close,
    partition_evidence,
    partition_physical_read_stats,
    partition_abort,
    partition_prepare_close,
    partition_observe_media,
};

bool gdox_source_make_partition(
    gdox_sector_source *inner,
    uint64_t base_lba,
    gdox_sector_source *output,
    gdox_error *error
)
{
    gdox_partition_context *context;
    uint64_t sectors;

    gdox_error_clear(error);
    if (inner == NULL || output == NULL || inner == output
        || !gdox_source_is_valid(inner) || gdox_source_is_valid(output)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "partition source requires a valid input and empty, distinct output"
        );
        return false;
    }
    sectors = gdox_source_sector_count(inner);
    if (base_lba >= sectors) {
        gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "partition begins outside the source");
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate partition source");
        return false;
    }

    context->inner = *inner;
    context->base_lba = base_lba;
    context->sectors = sectors - base_lba;
    inner->context = NULL;
    inner->ops = NULL;
    output->context = context;
    output->ops = &partition_ops;
    return true;
}

static int compare_indexed_patches(const void *left_value, const void *right_value)
{
    const gdox_indexed_patch *left = left_value;
    const gdox_indexed_patch *right = right_value;

    if (left->patch.offset < right->patch.offset) {
        return -1;
    }
    if (left->patch.offset > right->patch.offset) {
        return 1;
    }
    if (left->declaration_index < right->declaration_index) {
        return -1;
    }
    if (left->declaration_index > right->declaration_index) {
        return 1;
    }
    return 0;
}

static uint64_t patch_sector_count(const void *context)
{
    const gdox_patch_context *patched = context;
    return gdox_source_sector_count(&patched->inner);
}

static size_t lower_bound_patch(
    const gdox_byte_patch *patches,
    size_t count,
    uint64_t offset
)
{
    size_t first = 0U;
    size_t remaining = count;

    while (remaining != 0U) {
        const size_t step = remaining / 2U;
        const size_t middle = first + step;
        if (patches[middle].offset < offset) {
            first = middle + 1U;
            remaining -= step + 1U;
        } else {
            remaining = step;
        }
    }
    return first;
}

static bool patch_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_patch_context *patched = context;
    const uint64_t start = lba * GDOX_LOGICAL_SECTOR_BYTES;
    const uint64_t end = start + (uint64_t)output_bytes;
    size_t index;

    if (!gdox_source_read(
            &patched->inner,
            lba,
            blocks,
            output,
            output_bytes,
            error
        )) {
        return false;
    }

    index = lower_bound_patch(patched->patches, patched->patch_count, start);
    while (index < patched->patch_count && patched->patches[index].offset < end) {
        output[(size_t)(patched->patches[index].offset - start)] =
            patched->patches[index].value;
        ++index;
    }
    return true;
}

static bool patch_media_present(const void *context)
{
    const gdox_patch_context *patched = context;
    return gdox_source_media_present(&patched->inner);
}

static void patch_observe_media(
    const void *context,
    gdox_media_observation *output
)
{
    const gdox_patch_context *patched = context;
    (void)gdox_source_observe_media(&patched->inner, output);
}

static bool patch_evidence(
    const void *context,
    gdox_disc_evidence *output
)
{
    const gdox_patch_context *patched = context;
    return gdox_source_evidence(&patched->inner, output);
}

static bool patch_physical_read_stats(
    const void *context,
    gdox_physical_read_stats *output
)
{
    const gdox_patch_context *patched = context;
    return gdox_source_physical_read_stats(&patched->inner, output);
}

static bool patch_close(void *context, gdox_error *error)
{
    gdox_patch_context *patched = context;
    const bool closed = gdox_source_close(&patched->inner, error);
    free(patched->patches);
    free(patched);
    return closed;
}

static bool patch_prepare_close(void *context, gdox_error *error)
{
    gdox_patch_context *patched = context;
    return gdox_source_prepare_close(&patched->inner, error);
}

static void patch_abort(void *context)
{
    gdox_patch_context *patched = context;
    gdox_source_abort(&patched->inner);
}

static const gdox_sector_source_ops patch_ops = {
    patch_sector_count,
    patch_read,
    patch_media_present,
    patch_close,
    patch_evidence,
    patch_physical_read_stats,
    patch_abort,
    patch_prepare_close,
    patch_observe_media,
};

bool gdox_source_make_patched(
    gdox_sector_source *inner,
    const gdox_byte_patch *patches,
    size_t patch_count,
    gdox_sector_source *output,
    gdox_error *error
)
{
    gdox_patch_context *context;
    gdox_indexed_patch *indexed;
    uint64_t source_bytes;
    size_t index;
    size_t unique_count;

    gdox_error_clear(error);
    if (inner == NULL || output == NULL || inner == output
        || !gdox_source_is_valid(inner) || gdox_source_is_valid(output)
        || (patch_count != 0U && patches == NULL)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "patched source requires a valid input, empty distinct output, and a patch array"
        );
        return false;
    }
    if (patch_count == 0U) {
        *output = *inner;
        inner->context = NULL;
        inner->ops = NULL;
        return true;
    }
    if (patch_count > SIZE_MAX / sizeof(*indexed)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "patch array is too large");
        return false;
    }
    if (gdox_source_sector_count(inner) > UINT64_MAX / GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "source byte length overflows");
        return false;
    }
    source_bytes = gdox_source_sector_count(inner) * GDOX_LOGICAL_SECTOR_BYTES;
    indexed = malloc(patch_count * sizeof(*indexed));
    if (indexed == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate patch index");
        return false;
    }
    for (index = 0U; index < patch_count; ++index) {
        if (patches[index].offset >= source_bytes) {
            free(indexed);
            gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "patch is outside the source");
            return false;
        }
        indexed[index].patch = patches[index];
        indexed[index].declaration_index = index;
    }
    qsort(indexed, patch_count, sizeof(*indexed), compare_indexed_patches);

    unique_count = 0U;
    for (index = 0U; index < patch_count; ++index) {
        if (index + 1U < patch_count
            && indexed[index].patch.offset == indexed[index + 1U].patch.offset) {
            continue;
        }
        indexed[unique_count] = indexed[index];
        ++unique_count;
    }

    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        free(indexed);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate patched source");
        return false;
    }
    context->patches = malloc(unique_count * sizeof(*context->patches));
    if (context->patches == NULL) {
        free(indexed);
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate patches");
        return false;
    }
    for (index = 0U; index < unique_count; ++index) {
        context->patches[index] = indexed[index].patch;
    }
    free(indexed);

    context->patch_count = unique_count;
    context->inner = *inner;
    inner->context = NULL;
    inner->ops = NULL;
    output->context = context;
    output->ops = &patch_ops;
    return true;
}
