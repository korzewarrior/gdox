#include "qemu/osdep.h"

#include "gdox_qemu_disc.h"

#include "block/block-io.h"
#include "block/block_int.h"
#include "gdox/android_disc.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qobject/qdict.h"
#include "system/runstate.h"

#include <errno.h>
#include <jni.h>
#include <pthread.h>

#define GDOX_READ_AHEAD_BYTES ((size_t)256U * 1024U)

typedef struct gdox_qemu_disc_state {
    gdox_android_disc *disc;
    uint8_t *read_ahead;
    uint64_t read_ahead_offset;
    size_t read_ahead_bytes;
    uint64_t requests;
    uint64_t bytes;
    uint64_t request_size_buckets[7];
    uint64_t sequential_requests;
    uint64_t discontinuous_requests;
    uint64_t read_ahead_fills;
    uint64_t read_ahead_hits;
    uint64_t direct_reads;
    uint64_t previous_end;
    uint64_t physical_commands_at_open;
    uint64_t physical_bytes_at_open;
    uint64_t media_generation;
    bool has_previous_end;
    bool media_generation_known;
    bool read_ahead_valid;
    bool physical_read_reported;
    bool removal_reported;
} gdox_qemu_disc_state;

static pthread_mutex_t staged_mutex = PTHREAD_MUTEX_INITIALIZER;
static gdox_android_disc *staged_disc;
static gdox_android_disc *active_disc;
static bool global_media_end_reported;
static uint64_t session_media_generation;
static bool session_media_generation_known;

static gdox_removable_session_status gdox_observe_disc(
    const gdox_android_disc *disc,
    bool generation_known,
    uint64_t expected_generation,
    gdox_media_observation *observation
)
{
    *observation = (gdox_media_observation){0};
    if (disc == NULL
        || !gdox_android_disc_observe_media(disc, observation)) {
        return GDOX_REMOVABLE_SESSION_UNAVAILABLE;
    }
    return gdox_removable_session_classify(
        observation, generation_known, expected_generation
    );
}

static const char *gdox_media_end_description(
    gdox_removable_session_status status
)
{
    switch (status) {
        case GDOX_REMOVABLE_SESSION_EJECT_REQUESTED:
            return "physical-disc eject requested; ending session";
        case GDOX_REMOVABLE_SESSION_CHANGED:
            return "physical disc changed; ending session";
        case GDOX_REMOVABLE_SESSION_UNAVAILABLE:
            return "physical disc was removed; ending session";
        case GDOX_REMOVABLE_SESSION_PRESENT:
            break;
    }
    return "physical-disc session ended";
}

static gdox_removable_session_status gdox_observe_state(
    gdox_qemu_disc_state *state
)
{
    gdox_media_observation observation;
    const gdox_removable_session_status status = gdox_observe_disc(
        state->disc,
        state->media_generation_known,
        state->media_generation,
        &observation
    );

    if (status == GDOX_REMOVABLE_SESSION_PRESENT
        && !state->media_generation_known) {
        state->media_generation = observation.generation;
        state->media_generation_known = true;
    }
    return status;
}

static void gdox_end_state_session(
    gdox_qemu_disc_state *state,
    gdox_removable_session_status status
)
{
    if (state->removal_reported
        || status == GDOX_REMOVABLE_SESSION_PRESENT) {
        return;
    }
    state->removal_reported = true;
    info_report("GDOX %s", gdox_media_end_description(status));
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
}

bool gdox_qemu_disc_prepare(
    int file_descriptor,
    gdox_qemu_disc_info *output_info,
    gdox_error *error
)
{
    gdox_android_disc *disc = NULL;
    gdox_android_disc_info info;
    gdox_media_observation observation = {0};
    bool generation_known;

    if (!gdox_android_disc_open(
            file_descriptor,
            3U,
            UINT32_C(20000),
            &disc,
            &info,
            error
        )) {
        return false;
    }
    generation_known = gdox_android_disc_observe_media(disc, &observation);
    pthread_mutex_lock(&staged_mutex);
    if (staged_disc != NULL) {
        pthread_mutex_unlock(&staged_mutex);
        gdox_android_disc_close(disc, error);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a physical disc is already prepared"
        );
        return false;
    }
    staged_disc = disc;
    global_media_end_reported = false;
    session_media_generation = observation.generation;
    session_media_generation_known = generation_known;
    pthread_mutex_unlock(&staged_mutex);
    if (output_info != NULL) {
        memset(output_info, 0, sizeof(*output_info));
        memcpy(
            output_info->title,
            info.title,
            sizeof(output_info->title)
        );
        output_info->title_id_present = info.title_id_present;
        output_info->title_id = info.title_id;
    }
    return true;
}

bool gdox_qemu_disc_media_present(void)
{
    gdox_android_disc *disc;
    gdox_media_observation observation;
    gdox_removable_session_status status;
    uint64_t expected_generation;
    bool generation_known;
    bool report_end = false;

    pthread_mutex_lock(&staged_mutex);
    disc = active_disc != NULL ? active_disc : staged_disc;
    expected_generation = session_media_generation;
    generation_known = session_media_generation_known;
    status = gdox_observe_disc(
        disc, generation_known, expected_generation, &observation
    );
    if (disc != NULL && status != GDOX_REMOVABLE_SESSION_PRESENT
        && !global_media_end_reported) {
        global_media_end_reported = true;
        report_end = true;
    }
    pthread_mutex_unlock(&staged_mutex);
    if (report_end) {
        info_report("GDOX %s", gdox_media_end_description(status));
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
    }
    return status == GDOX_REMOVABLE_SESSION_PRESENT;
}

void gdox_qemu_disc_shutdown(void)
{
    gdox_android_disc *prepared;
    gdox_android_disc *active;
    gdox_error ignored;

    pthread_mutex_lock(&staged_mutex);
    prepared = staged_disc;
    active = active_disc;
    staged_disc = NULL;
    active_disc = NULL;
    global_media_end_reported = false;
    session_media_generation = 0U;
    session_media_generation_known = false;
    pthread_mutex_unlock(&staged_mutex);
    if (prepared != NULL) {
        (void)gdox_android_disc_close(prepared, &ignored);
    }
    if (active != NULL && active != prepared) {
        (void)gdox_android_disc_close(active, &ignored);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_korze_gdox_android_emulator_GdoxEmulatorActivity_nativePhysicalDiscPresent(
    JNIEnv *environment,
    jobject instance
)
{
    (void)environment;
    (void)instance;
    return gdox_qemu_disc_media_present() ? JNI_TRUE : JNI_FALSE;
}

static void gdox_parse_filename(
    const char *filename,
    QDict *options,
    Error **error
)
{
    (void)options;
    if (strcmp(filename, GDOX_QEMU_DISC_URL) != 0) {
        error_setg(error, "GDOX physical-disc URL is invalid");
    }
}

static int gdox_open(
    BlockDriverState *block,
    QDict *options,
    int flags,
    Error **error
)
{
    gdox_qemu_disc_state *state = block->opaque;
    gdox_physical_read_stats stats;
    int result;

    (void)flags;
    GLOBAL_STATE_CODE();
    bdrv_graph_rdlock_main_loop();
    result = bdrv_apply_auto_read_only(
        block,
        "the GDOX physical disc is read-only",
        error
    );
    bdrv_graph_rdunlock_main_loop();
    if (result < 0) {
        return result;
    }
    qdict_del(options, "filename");
    pthread_mutex_lock(&staged_mutex);
    state->disc = staged_disc;
    staged_disc = NULL;
    active_disc = state->disc;
    state->media_generation = session_media_generation;
    state->media_generation_known = session_media_generation_known;
    pthread_mutex_unlock(&staged_mutex);
    if (state->disc == NULL) {
        error_setg(error, "no GDOX physical disc is prepared");
        return -ENOMEDIUM;
    }
    block->bl.request_alignment = GDOX_LOGICAL_SECTOR_BYTES;
    if (gdox_android_disc_physical_read_stats(state->disc, &stats)) {
        state->physical_commands_at_open = stats.commands;
        state->physical_bytes_at_open = stats.bytes;
    }
    {
        const gdox_removable_session_status status =
            gdox_observe_state(state);

        gdox_end_state_session(state, status);
    }
    state->read_ahead = g_try_malloc(GDOX_READ_AHEAD_BYTES);
    info_report(
        "GDOX physical-disc block device opened (%llu bytes, "
        "%zu KiB volatile read-ahead)",
        (unsigned long long)gdox_android_disc_length(state->disc),
        state->read_ahead != NULL
            ? GDOX_READ_AHEAD_BYTES / 1024U
            : 0U
    );
    return 0;
}

static int64_t coroutine_fn gdox_get_length(BlockDriverState *block)
{
    gdox_qemu_disc_state *state = block->opaque;
    const uint64_t length = gdox_android_disc_length(state->disc);

    if (length > INT64_MAX) {
        return -EFBIG;
    }
    return (int64_t)length;
}

static bool gdox_range_contains(
    uint64_t container_offset,
    size_t container_bytes,
    uint64_t offset,
    size_t bytes
)
{
    uint64_t within;

    if (offset < container_offset) {
        return false;
    }
    within = offset - container_offset;
    return within <= container_bytes
        && bytes <= container_bytes - (size_t)within;
}

static bool gdox_read_with_ahead(
    gdox_qemu_disc_state *state,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    const uint64_t length = gdox_android_disc_length(state->disc);
    uint64_t window_offset;
    uint64_t remaining;
    size_t window_bytes;
    size_t filled;

    if (offset >= length
        || state->read_ahead == NULL
        || output_bytes > GDOX_READ_AHEAD_BYTES) {
        state->direct_reads++;
        return gdox_android_disc_read_at(
            state->disc,
            offset,
            output,
            output_bytes,
            read_bytes,
            error
        );
    }
    if (state->read_ahead_valid
        && gdox_range_contains(
            state->read_ahead_offset,
            state->read_ahead_bytes,
            offset,
            output_bytes
        )) {
        memcpy(
            output,
            state->read_ahead + (size_t)(offset - state->read_ahead_offset),
            output_bytes
        );
        state->read_ahead_hits++;
        *read_bytes = output_bytes;
        return true;
    }

    /*
     * QEMU issues sector-aligned requests. Begin the volatile window at the
     * requested byte instead of a host-sized boundary: aligning backwards
     * made every random miss read unrelated sectors before the requested
     * data, which is especially expensive on optical media.
     */
    window_offset = offset;
    remaining = length - window_offset;
    window_bytes = remaining < GDOX_READ_AHEAD_BYTES
        ? (size_t)remaining
        : GDOX_READ_AHEAD_BYTES;
    if (!gdox_range_contains(
            window_offset,
            window_bytes,
            offset,
            output_bytes
        )) {
        state->direct_reads++;
        return gdox_android_disc_read_at(
            state->disc,
            offset,
            output,
            output_bytes,
            read_bytes,
            error
        );
    }

    state->read_ahead_valid = false;
    if (!gdox_android_disc_read_at(
            state->disc,
            window_offset,
            state->read_ahead,
            window_bytes,
            &filled,
            error
        )
        || filled != window_bytes) {
        /*
         * Read-ahead must never make a requested sector fail because an
         * unrequested sector later in the window is damaged.
         */
        state->direct_reads++;
        return gdox_android_disc_read_at(
            state->disc,
            offset,
            output,
            output_bytes,
            read_bytes,
            error
        );
    }
    state->read_ahead_offset = window_offset;
    state->read_ahead_bytes = window_bytes;
    state->read_ahead_valid = true;
    state->read_ahead_fills++;
    memcpy(
        output,
        state->read_ahead + (size_t)(offset - window_offset),
        output_bytes
    );
    *read_bytes = output_bytes;
    return true;
}

static int coroutine_fn gdox_read(
    BlockDriverState *block,
    int64_t offset,
    int64_t bytes,
    QEMUIOVector *vectors,
    BdrvRequestFlags flags
)
{
    gdox_qemu_disc_state *state = block->opaque;
    gdox_error error;
    uint8_t *buffer;
    uint8_t *destination;
    size_t read_bytes;
    gdox_removable_session_status media_status;
    bool borrowed;

    (void)flags;
    if (offset < 0 || bytes < 0 || (uint64_t)bytes > SIZE_MAX) {
        return -EINVAL;
    }
    if (bytes == 0) {
        return 0;
    }
    state->requests++;
    state->bytes += (uint64_t)bytes;
    if (bytes <= GDOX_LOGICAL_SECTOR_BYTES) {
        state->request_size_buckets[0]++;
    } else if (bytes <= 2 * GDOX_LOGICAL_SECTOR_BYTES) {
        state->request_size_buckets[1]++;
    } else if (bytes <= 4 * GDOX_LOGICAL_SECTOR_BYTES) {
        state->request_size_buckets[2]++;
    } else if (bytes <= 8 * GDOX_LOGICAL_SECTOR_BYTES) {
        state->request_size_buckets[3]++;
    } else if (bytes <= 16 * GDOX_LOGICAL_SECTOR_BYTES) {
        state->request_size_buckets[4]++;
    } else if (bytes <= 32 * GDOX_LOGICAL_SECTOR_BYTES) {
        state->request_size_buckets[5]++;
    } else {
        state->request_size_buckets[6]++;
    }
    if (state->has_previous_end) {
        if ((uint64_t)offset == state->previous_end) {
            state->sequential_requests++;
        } else {
            state->discontinuous_requests++;
        }
    }
    state->previous_end = (uint64_t)offset + (uint64_t)bytes;
    state->has_previous_end = true;
    if (state->requests == 1U) {
        info_report(
            "GDOX physical-disc block reads started "
            "(offset %lld, %lld bytes)",
            (long long)offset,
            (long long)bytes
        );
    }
    borrowed = vectors->niov == 1
        && vectors->iov[0].iov_len == (size_t)bytes;
    if (borrowed) {
        destination = vectors->iov[0].iov_base;
        buffer = NULL;
    } else {
        buffer = g_try_malloc((size_t)bytes);
        if (buffer == NULL) {
            return -ENOMEM;
        }
        destination = buffer;
    }
    if (!gdox_read_with_ahead(
            state,
            (uint64_t)offset,
            destination,
            (size_t)bytes,
            &read_bytes,
            &error
        )
        || read_bytes != (size_t)bytes) {
        media_status = gdox_observe_state(state);
        error_report("GDOX physical-disc read failed: %s", error.message);
        gdox_end_state_session(state, media_status);
        g_free(buffer);
        return media_status == GDOX_REMOVABLE_SESSION_PRESENT
            ? -EIO
            : -ENOMEDIUM;
    }
    if (!borrowed
        && qemu_iovec_from_buf(vectors, 0U, buffer, read_bytes) != read_bytes) {
        g_free(buffer);
        return -EIO;
    }
    if (!state->physical_read_reported
        || state->requests % UINT64_C(256) == 0U) {
        gdox_physical_read_stats stats;

        if (gdox_android_disc_physical_read_stats(state->disc, &stats)
            && stats.commands >= state->physical_commands_at_open
            && stats.bytes >= state->physical_bytes_at_open) {
            const uint64_t physical_commands =
                stats.commands - state->physical_commands_at_open;
            const uint64_t physical_bytes =
                stats.bytes - state->physical_bytes_at_open;

            if (!state->physical_read_reported && physical_commands > 0U) {
                state->physical_read_reported = true;
                info_report(
                    "GDOX live optical reads started "
                    "(%llu command%s, %llu bytes)",
                    (unsigned long long)physical_commands,
                    physical_commands == 1U ? "" : "s",
                    (unsigned long long)physical_bytes
                );
            } else if (state->physical_read_reported
                       && state->requests % UINT64_C(256) == 0U) {
                info_report(
                    "GDOX physical-disc streaming "
                    "(%llu requests, %llu block bytes; "
                    "%llu optical commands, %llu optical bytes; "
                    "sizes 2K:%llu 4K:%llu 8K:%llu 16K:%llu "
                    "32K:%llu 64K:%llu larger:%llu; "
                    "sequential:%llu discontinuous:%llu; "
                    "read-ahead fills:%llu hits:%llu direct:%llu)",
                    (unsigned long long)state->requests,
                    (unsigned long long)state->bytes,
                    (unsigned long long)physical_commands,
                    (unsigned long long)physical_bytes,
                    (unsigned long long)state->request_size_buckets[0],
                    (unsigned long long)state->request_size_buckets[1],
                    (unsigned long long)state->request_size_buckets[2],
                    (unsigned long long)state->request_size_buckets[3],
                    (unsigned long long)state->request_size_buckets[4],
                    (unsigned long long)state->request_size_buckets[5],
                    (unsigned long long)state->request_size_buckets[6],
                    (unsigned long long)state->sequential_requests,
                    (unsigned long long)state->discontinuous_requests,
                    (unsigned long long)state->read_ahead_fills,
                    (unsigned long long)state->read_ahead_hits,
                    (unsigned long long)state->direct_reads
                );
            }
        }
    }
    g_free(buffer);
    return 0;
}

static bool coroutine_fn gdox_is_inserted(BlockDriverState *block)
{
    gdox_qemu_disc_state *state = block->opaque;
    const gdox_removable_session_status status = gdox_observe_state(state);

    gdox_end_state_session(state, status);
    return status == GDOX_REMOVABLE_SESSION_PRESENT;
}

static void gdox_close(BlockDriverState *block)
{
    gdox_qemu_disc_state *state = block->opaque;
    gdox_error error;

    if (state->disc == NULL) {
        g_clear_pointer(&state->read_ahead, g_free);
        return;
    }
    info_report(
        "GDOX physical-disc block device closed "
        "(%llu request%s, %llu bytes; read-ahead fills:%llu "
        "hits:%llu direct:%llu)",
        (unsigned long long)state->requests,
        state->requests == 1U ? "" : "s",
        (unsigned long long)state->bytes,
        (unsigned long long)state->read_ahead_fills,
        (unsigned long long)state->read_ahead_hits,
        (unsigned long long)state->direct_reads
    );
    pthread_mutex_lock(&staged_mutex);
    if (active_disc == state->disc) {
        active_disc = NULL;
        session_media_generation = 0U;
        session_media_generation_known = false;
        global_media_end_reported = false;
    }
    pthread_mutex_unlock(&staged_mutex);
    if (!gdox_android_disc_close(state->disc, &error)) {
        error_report("GDOX could not close the physical disc: %s", error.message);
    }
    state->disc = NULL;
    g_clear_pointer(&state->read_ahead, g_free);
}

static void gdox_refresh_filename(BlockDriverState *block)
{
    pstrcpy(
        block->exact_filename,
        sizeof(block->exact_filename),
        GDOX_QEMU_DISC_URL
    );
}

static BlockDriver gdox_driver = {
    .format_name = "gdox",
    .protocol_name = "gdox",
    .instance_size = sizeof(gdox_qemu_disc_state),
    .bdrv_open = gdox_open,
    .bdrv_close = gdox_close,
    .bdrv_parse_filename = gdox_parse_filename,
    .bdrv_co_getlength = gdox_get_length,
    .bdrv_co_preadv = gdox_read,
    .bdrv_co_is_inserted = gdox_is_inserted,
    .bdrv_refresh_filename = gdox_refresh_filename,
};

static void gdox_driver_init(void)
{
    bdrv_register(&gdox_driver);
}

block_init(gdox_driver_init);
