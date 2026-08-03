#include "gdox/disc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct direct_disc_context {
    gdox_sector_source source;
    uint64_t length;
} direct_disc_context;

bool gdox_disc_is_valid(const gdox_random_disc *disc)
{
    return disc != NULL && disc->context != NULL && disc->ops != NULL
        && disc->ops->length != NULL && disc->ops->read_at != NULL
        && disc->ops->close != NULL;
}

uint64_t gdox_disc_length(const gdox_random_disc *disc)
{
    return gdox_disc_is_valid(disc) ? disc->ops->length(disc->context) : 0U;
}

bool gdox_disc_read_at(
    gdox_random_disc *disc,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc) || read_bytes == NULL
        || (output_bytes != 0U && output == NULL)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "valid disc, output, and read count are required");
        return false;
    }
    *read_bytes = 0U;
    return disc->ops->read_at(
        disc->context,
        offset,
        output,
        output_bytes,
        read_bytes,
        error
    );
}

bool gdox_disc_observe_media(
    const gdox_random_disc *disc,
    gdox_media_observation *output
)
{
    if (output == NULL) {
        return false;
    }
    output->readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    output->generation = 0U;
    output->event = GDOX_MEDIA_EVENT_NONE;
    if (!gdox_disc_is_valid(disc)) {
        return false;
    }
    if (disc->ops->observe_media != NULL) {
        disc->ops->observe_media(disc->context, output);
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
    output->readiness = disc->ops->media_present == NULL
        || disc->ops->media_present(disc->context)
        ? GDOX_MEDIA_READINESS_PRESENT
        : GDOX_MEDIA_READINESS_ABSENT;
    return true;
}

bool gdox_disc_media_present(const gdox_random_disc *disc)
{
    gdox_media_observation observation;
    return gdox_disc_observe_media(disc, &observation)
        && observation.readiness == GDOX_MEDIA_READINESS_PRESENT;
}

bool gdox_disc_physical_read_stats(
    const gdox_random_disc *disc,
    gdox_physical_read_stats *output
)
{
    if (output == NULL) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    if (!gdox_disc_is_valid(disc)
        || disc->ops->physical_read_stats == NULL) {
        return false;
    }
    return disc->ops->physical_read_stats(disc->context, output);
}

void gdox_disc_abort(gdox_random_disc *disc)
{
    if (gdox_disc_is_valid(disc) && disc->ops->abort != NULL) {
        disc->ops->abort(disc->context);
    }
}

bool gdox_disc_prepare_close(gdox_random_disc *disc, gdox_error *error)
{
    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "disc is not open");
        return false;
    }
    if (disc->ops->prepare_close == NULL) {
        return true;
    }
    return disc->ops->prepare_close(disc->context, error);
}

bool gdox_disc_close(gdox_random_disc *disc, gdox_error *error)
{
    void *context;
    const gdox_random_disc_ops *ops;

    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "disc is not open");
        return false;
    }
    if (!gdox_disc_prepare_close(disc, error)) {
        return false;
    }
    context = disc->context;
    ops = disc->ops;
    disc->context = NULL;
    disc->ops = NULL;
    return ops->close(context, error);
}

static bool source_length(
    const gdox_sector_source *source,
    uint64_t *length,
    gdox_error *error
)
{
    const uint64_t sectors = gdox_source_sector_count(source);
    if (sectors > UINT64_MAX / GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "source byte length overflows");
        return false;
    }
    *length = sectors * GDOX_LOGICAL_SECTOR_BYTES;
    return true;
}

static uint64_t direct_length(const void *context)
{
    const direct_disc_context *direct = context;
    return direct->length;
}

static bool direct_read_at(
    void *context,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    direct_disc_context *direct = context;
    uint64_t remaining;
    size_t readable;
    uint64_t first_sector;
    size_t within;
    size_t span;
    uint64_t block_count;
    size_t aligned_bytes;
    uint8_t *aligned;

    if (output_bytes == 0U || offset >= direct->length) {
        *read_bytes = 0U;
        return true;
    }
    remaining = direct->length - offset;
    readable = remaining < output_bytes ? (size_t)remaining : output_bytes;
    first_sector = offset / GDOX_LOGICAL_SECTOR_BYTES;
    within = (size_t)(offset % GDOX_LOGICAL_SECTOR_BYTES);
    if (readable > SIZE_MAX - within) {
        gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "byte read span overflows");
        return false;
    }
    if (within == 0U
        && readable % GDOX_LOGICAL_SECTOR_BYTES == 0U
        && readable / GDOX_LOGICAL_SECTOR_BYTES <= UINT32_MAX) {
        if (!gdox_source_read(
                &direct->source,
                first_sector,
                (uint32_t)(readable / GDOX_LOGICAL_SECTOR_BYTES),
                output,
                readable,
                error
            )) {
            return false;
        }
        *read_bytes = readable;
        return true;
    }
    span = within + readable;
    block_count =
        ((uint64_t)span + GDOX_LOGICAL_SECTOR_BYTES - 1U)
        / GDOX_LOGICAL_SECTOR_BYTES;
    if (block_count > UINT32_MAX
        || block_count > SIZE_MAX / GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "byte read is too large");
        return false;
    }
    aligned_bytes = (size_t)block_count * GDOX_LOGICAL_SECTOR_BYTES;
    aligned = malloc(aligned_bytes);
    if (aligned == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate aligned read buffer");
        return false;
    }
    if (!gdox_source_read(
            &direct->source,
            first_sector,
            (uint32_t)block_count,
            aligned,
            aligned_bytes,
            error
        )) {
        free(aligned);
        return false;
    }
    memcpy(output, aligned + within, readable);
    free(aligned);
    *read_bytes = readable;
    return true;
}

static bool direct_media_present(const void *context)
{
    const direct_disc_context *direct = context;
    return gdox_source_media_present(&direct->source);
}

static void direct_observe_media(
    const void *context,
    gdox_media_observation *output
)
{
    const direct_disc_context *direct = context;
    (void)gdox_source_observe_media(&direct->source, output);
}

static bool direct_physical_read_stats(
    const void *context,
    gdox_physical_read_stats *output
)
{
    const direct_disc_context *direct = context;
    return gdox_source_physical_read_stats(&direct->source, output);
}

static bool direct_close(void *context, gdox_error *error)
{
    direct_disc_context *direct = context;
    const bool closed = gdox_source_close(&direct->source, error);
    free(direct);
    return closed;
}

static bool direct_prepare_close(void *context, gdox_error *error)
{
    direct_disc_context *direct = context;
    return gdox_source_prepare_close(&direct->source, error);
}

static void direct_abort(void *context)
{
    direct_disc_context *direct = context;
    gdox_source_abort(&direct->source);
}

static const gdox_random_disc_ops direct_ops = {
    direct_length,
    direct_read_at,
    direct_media_present,
    direct_close,
    direct_physical_read_stats,
    direct_abort,
    direct_prepare_close,
    direct_observe_media,
};

bool gdox_disc_from_source(
    gdox_sector_source *source,
    gdox_random_disc *disc,
    gdox_error *error
)
{
    direct_disc_context *context;
    uint64_t length;

    gdox_error_clear(error);
    if (!gdox_source_is_valid(source) || disc == NULL
        || gdox_disc_is_valid(disc)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "an open source and empty disc output are required");
        return false;
    }
    if (!source_length(source, &length, error)) {
        return false;
    }
    context = malloc(sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate direct disc");
        return false;
    }
    context->source = *source;
    context->length = length;
    source->context = NULL;
    source->ops = NULL;
    disc->context = context;
    disc->ops = &direct_ops;
    return true;
}
