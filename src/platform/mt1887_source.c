#define _POSIX_C_SOURCE 200809L

#include "gdox/optical.h"

#include "platform/mmc_commands.h"
#include "platform/mt1887_media_profile.h"
#include "platform/mt1887_source.h"
#include "platform/mt1887_profile.h"
#include "platform/optical_driver.h"
#include "platform/portable_sync.h"
#include "platform/scsi_transport.h"
#include "platform/usb_bot.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_MT_DEFAULT_TIMEOUT_MS UINT32_C(5000)
#define GDOX_MT_PRESENCE_TIMEOUT_MS UINT32_C(1000)
#define GDOX_MT_READ_TIMEOUT_MS UINT32_C(30000)
#define GDOX_MT_MAXIMUM_READ_SPEED UINT16_C(0xffff)
#define GDOX_MT_DIAGNOSTIC_ATTEMPTS UINT32_C(3)
#define GDOX_MT_READY_ATTEMPTS UINT32_C(20)
#define GDOX_MT_INQUIRY_ATTEMPTS UINT32_C(2)
/*
 * Total time permitted within one source read. Every command and backoff in
 * the read and recovery path is capped to this deadline.
 */
#define GDOX_MT_RECOVERY_BUDGET_MS UINT32_C(20000)

typedef struct gdox_mt1887_context {
    gdox_scsi_transport transport;
    const gdox_mt1887_profile *profile;
    const gdox_mt1887_media_profile *media;
    gdox_mutex mutex;
    gdox_disc_evidence evidence;
    gdox_mmc_media_tracker media_tracker;
    atomic_uint_fast64_t read_commands;
    atomic_uint_fast64_t read_sectors;
    atomic_uint_fast64_t read_bytes;
    atomic_uint_fast64_t last_read_lba;
    atomic_bool abort;
    uint16_t read_speed_kbps;
    uint32_t max_read_blocks;
    uint8_t retries;
    bool restoration_required;
} gdox_mt1887_context;

static void set_recovery_deadline_error(gdox_error *error)
{
    gdox_error_set(
        error,
        GDOX_ERROR_IO,
        "optical read recovery deadline expired"
    );
}

static bool deadline_timeout(
    uint64_t deadline_ms,
    uint32_t maximum_ms,
    uint32_t *timeout_ms,
    gdox_error *error
)
{
    uint64_t remaining_ms;

    if (deadline_ms == 0U) {
        *timeout_ms = maximum_ms;
        return true;
    }
    const uint64_t now_ms = gdox_monotonic_ms();
    if (now_ms >= deadline_ms) {
        *timeout_ms = 0U;
        set_recovery_deadline_error(error);
        return false;
    }
    remaining_ms = deadline_ms - now_ms;
    if (remaining_ms > maximum_ms) {
        remaining_ms = maximum_ms;
    }
    *timeout_ms = (uint32_t)remaining_ms;
    return true;
}

static void put_be_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)(value & 0xffU);
}

static bool request_read_speed(
    gdox_scsi_transport *transport,
    uint16_t kilobytes_per_second,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};
    uint32_t timeout_ms;

    if (!deadline_timeout(
            deadline_ms,
            GDOX_MT_DEFAULT_TIMEOUT_MS,
            &timeout_ms,
            error
        )) {
        return false;
    }

    cdb[0] = 0xbbU;
    put_be_u16(cdb + 2U, kilobytes_per_second);
    return gdox_scsi_command_none(
        transport,
        "SET CD SPEED",
        cdb,
        sizeof(cdb),
        timeout_ms,
        error
    );
}

static bool ladder_aborted(const atomic_bool *abort, gdox_error *error)
{
    if (abort != NULL
        && atomic_load_explicit(abort, memory_order_acquire)) {
        gdox_error_set(error, GDOX_ERROR_CANCELLED, "optical session was cancelled");
        return true;
    }
    return false;
}

static bool read_xdata(
    gdox_scsi_transport *transport,
    const atomic_bool *abort,
    uint16_t address,
    uint8_t *value,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    uint8_t cdb[10] = {
        0xf1U, 0x02U, 0U, 0U, 0U, 0U, 0x01U, 0U, 0U, 0U,
    };
    uint8_t response[4];
    uint32_t attempt;
    uint32_t timeout_ms;

    cdb[4] = (uint8_t)(address >> 8U);
    cdb[5] = (uint8_t)(address & 0xffU);
    for (attempt = 0U; attempt < GDOX_MT_DIAGNOSTIC_ATTEMPTS; ++attempt) {
        size_t transferred;
        if (ladder_aborted(abort, error)) {
            return false;
        }
        if (!deadline_timeout(
                deadline_ms,
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                &timeout_ms,
                error
            )) {
            return false;
        }
        if (gdox_scsi_command_in(
                transport,
                "MT1887 F1 diagnostic XDATA read",
                cdb,
                sizeof(cdb),
                response,
                sizeof(response),
                timeout_ms,
                &transferred,
                error
            ) && transferred == sizeof(response)) {
            *value = response[3];
            return true;
        }
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "MT1887 diagnostic XDATA read returned a short response"
            );
        }
        if (error->code == GDOX_ERROR_NOT_FOUND) {
            return false;
        }
        if (attempt + 1U < GDOX_MT_DIAGNOSTIC_ATTEMPTS) {
            gdox_error ignored;
            const uint32_t backoff_ms =
                UINT32_C(100) * (attempt + 1U);

            if (!deadline_timeout(
                    deadline_ms,
                    backoff_ms,
                    &timeout_ms,
                    error
                )) {
                return false;
            }
            (void)gdox_scsi_transport_reset(transport, &ignored);
            if (!deadline_timeout(
                    deadline_ms,
                    backoff_ms,
                    &timeout_ms,
                    error
                )) {
                return false;
            }
            gdox_sleep_ms(timeout_ms);
        }
    }
    return false;
}

static bool write_xdata(
    gdox_scsi_transport *transport,
    uint16_t address,
    uint8_t value,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    uint8_t cdb[10] = {
        0xf1U, 0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
    uint32_t timeout_ms;

    if (!deadline_timeout(
            deadline_ms,
            GDOX_MT_DEFAULT_TIMEOUT_MS,
            &timeout_ms,
            error
        )) {
        return false;
    }
    cdb[4] = (uint8_t)(address >> 8U);
    cdb[5] = (uint8_t)(address & 0xffU);
    cdb[9] = value;
    return gdox_scsi_command_none(
        transport,
        "MT1887 F1 volatile XDATA write",
        cdb,
        sizeof(cdb),
        timeout_ms,
        error
    );
}

static bool read_triplet(
    gdox_scsi_transport *transport,
    const atomic_bool *abort,
    const uint16_t addresses[3],
    uint8_t values[3],
    uint64_t deadline_ms,
    gdox_error *error
)
{
    size_t index;
    for (index = 0U; index < 3U; ++index) {
        if (!read_xdata(
                transport,
                abort,
                addresses[index],
                &values[index],
                deadline_ms,
                error
            )) {
            return false;
        }
    }
    return true;
}

static bool write_triplet(
    gdox_scsi_transport *transport,
    const uint16_t addresses[3],
    const uint8_t values[3],
    bool attempt_all,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    gdox_error first_error;
    bool failed = false;
    size_t index;

    gdox_error_clear(&first_error);
    for (index = 0U; index < 3U; ++index) {
        gdox_error current;
        if (!write_xdata(
                transport,
                addresses[index],
                values[index],
                deadline_ms,
                &current
            )) {
            if (!failed) {
                first_error = current;
                failed = true;
            }
            if (!attempt_all) {
                break;
            }
        }
    }
    if (failed) {
        *error = first_error;
        return false;
    }
    return true;
}

static bool read_state(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const atomic_bool *abort,
    gdox_mt1887_state *state,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    uint32_t timeout_ms;

    memset(state, 0, sizeof(*state));
    return read_triplet(
            transport,
            abort,
            profile->capacity_addresses,
            state->capacity,
            deadline_ms,
            error
        )
        && read_triplet(
            transport,
            abort,
            profile->geometry_addresses,
            state->geometry,
            deadline_ms,
            error
        )
        && (!profile->auxiliary_present
            || read_triplet(
                transport,
                abort,
                profile->auxiliary_addresses,
                state->auxiliary,
                deadline_ms,
                error
            ))
        && deadline_timeout(
            deadline_ms,
            UINT32_C(10000),
            &timeout_ms,
            error
        )
        && gdox_mmc_read_capacity_10(
            transport,
            timeout_ms,
            &state->last_lba,
            &state->block_size,
            error
        );
}

static bool restore_stock(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_media_profile *media,
    const atomic_bool *abort,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    gdox_error first_error;
    gdox_error current;
    bool failed = false;
    gdox_mt1887_state state;

    gdox_error_clear(&first_error);
    if (!write_triplet(
            transport,
            profile->geometry_addresses,
            media->stock_geometry,
            true,
            deadline_ms,
            &current
        )) {
        first_error = current;
        failed = true;
    }
    if (!write_triplet(
            transport,
            profile->capacity_addresses,
            media->stock_capacity,
            true,
            deadline_ms,
            &current
        ) && !failed) {
        first_error = current;
        failed = true;
    }
    if (profile->auxiliary_present
        && !write_triplet(
            transport,
            profile->auxiliary_addresses,
            profile->auxiliary,
            true,
            deadline_ms,
            &current
        ) && !failed) {
        first_error = current;
        failed = true;
    }
    if (failed) {
        *error = first_error;
        return false;
    }
    if (!read_state(
            transport, profile, abort, &state, deadline_ms, error
        )) {
        return false;
    }
    if (gdox_mt1887_media_state_classify(media, profile, &state)
        != GDOX_MT1887_MEDIA_STATE_STOCK) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "stock MT1887 SRAM state did not verify");
        return false;
    }
    return true;
}

static bool restore_stock_after_streaming(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_media_profile *media,
    gdox_error *error
)
{
    gdox_error last;
    uint32_t attempt;

    if (restore_stock(transport, profile, media, NULL, 0U, &last)) {
        return true;
    }
    for (attempt = 0U; attempt < 2U; ++attempt) {
        gdox_error ignored;

        (void)gdox_scsi_transport_reset(transport, &ignored);
        gdox_sleep_ms(UINT32_C(100) * (attempt + 1U));
        if (restore_stock(transport, profile, media, NULL, 0U, &last)) {
            return true;
        }
    }
    gdox_error_set(
        error,
        GDOX_ERROR_TRANSPORT,
        "could not restore optical SRAM; power-cycle the drive"
    );
    return false;
}

static bool recover_known_state(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_media_profile *media,
    const atomic_bool *abort,
    const gdox_mt1887_state *state,
    bool *restoration_required,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    gdox_mt1887_media_state_class state_class;

    state_class = gdox_mt1887_media_state_classify(media, profile, state);
    if (state_class == GDOX_MT1887_MEDIA_STATE_UNKNOWN) {
        const gdox_mt1887_media_profile *known_media =
            gdox_mt1887_media_profile_select_known(profile, state);
        char message[GDOX_ERROR_MESSAGE_CAPACITY];

        if (known_media != NULL && known_media != media) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "volatile SRAM state belongs to a different validated XGD profile"
            );
            return false;
        }

        (void)snprintf(
            message,
            sizeof(message),
            "refusing MT1887 session because volatile SRAM has an unknown "
            "state (capacity=%02x%02x%02x geometry=%02x%02x%02x "
            "auxiliary=%02x%02x%02x last_lba=%08x block_size=%u)",
            state->capacity[0],
            state->capacity[1],
            state->capacity[2],
            state->geometry[0],
            state->geometry[1],
            state->geometry[2],
            state->auxiliary[0],
            state->auxiliary[1],
            state->auxiliary[2],
            state->last_lba,
            state->block_size
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
        return false;
    }
    if (state->block_size != GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "refusing MT1887 recovery because the logical block size is not 2048 bytes"
        );
        return false;
    }
    if (state_class != GDOX_MT1887_MEDIA_STATE_STOCK) {
        if (restoration_required != NULL) {
            *restoration_required = true;
        }
        return restore_stock(
            transport, profile, media, abort, deadline_ms, error
        );
    }
    return true;
}

static bool recover_known_partial(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_media_profile *media,
    const atomic_bool *abort,
    bool *restoration_required,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    gdox_mt1887_state state;

    return read_state(
            transport, profile, abort, &state, deadline_ms, error
        )
        && recover_known_state(
            transport,
            profile,
            media,
            abort,
            &state,
            restoration_required,
            deadline_ms,
            error
        );
}

static bool apply_xgd(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_media_profile *media,
    const atomic_bool *abort,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    gdox_mt1887_state state;

    if (!write_triplet(
            transport,
            profile->capacity_addresses,
            media->live_capacity,
            false,
            deadline_ms,
            error
        )
        || !write_triplet(
            transport,
            profile->geometry_addresses,
            media->live_geometry,
            false,
            deadline_ms,
            error
        )
        || !read_state(
            transport, profile, abort, &state, deadline_ms, error
        )) {
        return false;
    }
    if (gdox_mt1887_media_state_classify(media, profile, &state)
        != GDOX_MT1887_MEDIA_STATE_LIVE) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "live MT1887 SRAM state did not verify");
        return false;
    }
    return true;
}

static bool ensure_xgd(
    gdox_scsi_transport *transport,
    const gdox_mt1887_profile *profile,
    const gdox_mt1887_media_profile *media,
    const atomic_bool *abort,
    uint64_t deadline_ms,
    gdox_error *error
)
{
    gdox_mt1887_state state;
    const bool state_read =
        read_state(
            transport, profile, abort, &state, deadline_ms, error
        );

    if (state_read) {
        if (gdox_mt1887_media_state_classify(media, profile, &state)
            == GDOX_MT1887_MEDIA_STATE_LIVE) {
            return true;
        }
    } else {
        if (error->code == GDOX_ERROR_CANCELLED
            || error->code == GDOX_ERROR_NOT_FOUND) {
            return false;
        }
    }
    if (!recover_known_partial(
            transport,
            profile,
            media,
            abort,
            NULL,
            deadline_ms,
            error
        )) {
        return false;
    }
    return apply_xgd(
        transport, profile, media, abort, deadline_ms, error
    );
}

static bool media_remains_current(
    gdox_scsi_transport *transport,
    gdox_mmc_media_tracker *media_tracker,
    uint64_t expected_generation,
    uint32_t timeout_ms,
    bool poll_event,
    gdox_error *error
)
{
    if (poll_event) {
        (void)gdox_mmc_poll_media_event(
            transport, timeout_ms, media_tracker
        );
    }
    if (!gdox_mmc_media_tracker_transitioned(
            media_tracker, expected_generation
        )) {
        return true;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        media_tracker->pending_event == GDOX_MEDIA_EVENT_EJECT_REQUEST
            ? "physical eject requested"
            : "physical media changed during optical read"
    );
    return false;
}

static bool recover_optical(
    gdox_scsi_transport *transport,
    gdox_mmc_media_tracker *media_tracker,
    uint64_t expected_generation,
    const atomic_bool *abort,
    uint64_t recovery_deadline_ms,
    gdox_error *error
)
{
    static const uint8_t load[6] = {0x1bU, 0U, 0U, 0U, 0x03U, 0U};
    uint32_t attempt;
    uint32_t timeout_ms;
    gdox_error last;

    gdox_error_clear(error);
    gdox_error_clear(&last);
    if (ladder_aborted(abort, error)) {
        return false;
    }
    if (!deadline_timeout(
            recovery_deadline_ms,
            GDOX_MT_DEFAULT_TIMEOUT_MS,
            &timeout_ms,
            error
        )) {
        return false;
    }
    (void)gdox_mmc_media_tracker_capture_transport_sense(
        transport,
        timeout_ms,
        media_tracker
    );
    if (!media_remains_current(
            transport,
            media_tracker,
            expected_generation,
            timeout_ms,
            true,
            error
        )) {
        return false;
    }
    (void)gdox_scsi_transport_reset(transport, &last);
    {
        uint8_t sense[18];
        size_t transferred;
        if (deadline_timeout(
                recovery_deadline_ms,
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                &timeout_ms,
                &last
            )) {
            if (gdox_mmc_request_sense(
                    transport,
                    timeout_ms,
                    sense,
                    &transferred,
                    &last
                )) {
                gdox_mmc_media_tracker_note_sense(
                    media_tracker,
                    sense,
                    transferred
                );
            }
        }
    }
    if (!media_remains_current(
            transport,
            media_tracker,
            expected_generation,
            timeout_ms,
            true,
            error
        )) {
        return false;
    }
    if (!deadline_timeout(
            recovery_deadline_ms,
            GDOX_MT_DEFAULT_TIMEOUT_MS,
            &timeout_ms,
            error
        )) {
        return false;
    }
    if (!gdox_scsi_command_none(
            transport,
            "START STOP UNIT (load/start)",
            load,
            sizeof(load),
            timeout_ms,
            &last
        )) {
        (void)gdox_mmc_media_tracker_capture_transport_sense(
            transport, timeout_ms, media_tracker
        );
    }
    if (!media_remains_current(
            transport,
            media_tracker,
            expected_generation,
            timeout_ms,
            true,
            error
        )) {
        return false;
    }
    for (attempt = 0U; attempt < GDOX_MT_READY_ATTEMPTS; ++attempt) {
        if (ladder_aborted(abort, error)) {
            return false;
        }
        if (!deadline_timeout(
                recovery_deadline_ms,
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                &timeout_ms,
                error
            )) {
            return false;
        }
        if (!media_remains_current(
                transport,
                media_tracker,
                expected_generation,
                timeout_ms,
                true,
                error
            )) {
            return false;
        }
        const bool ready = gdox_mmc_test_unit_ready(
            transport,
            timeout_ms,
            &last
        );
        if (!ready) {
            (void)gdox_mmc_media_tracker_capture_transport_sense(
                transport, timeout_ms, media_tracker
            );
        }
        if (!media_remains_current(
                transport,
                media_tracker,
                expected_generation,
                timeout_ms,
                true,
                error
            )) {
            return false;
        }
        if (ready) {
            return true;
        }
        if (last.code == GDOX_ERROR_NOT_FOUND) {
            break;
        }
        if (attempt == 7U || attempt == 15U) {
            if (!deadline_timeout(
                    recovery_deadline_ms,
                    GDOX_MT_DEFAULT_TIMEOUT_MS,
                    &timeout_ms,
                    error
                )) {
                return false;
            }
            if (!media_remains_current(
                    transport,
                    media_tracker,
                    expected_generation,
                    timeout_ms,
                    true,
                    error
                )) {
                return false;
            }
            if (!gdox_scsi_command_none(
                    transport,
                    "START STOP UNIT (load/start)",
                    load,
                    sizeof(load),
                    timeout_ms,
                    &last
                )) {
                (void)gdox_mmc_media_tracker_capture_transport_sense(
                    transport, timeout_ms, media_tracker
                );
            }
            if (!media_remains_current(
                    transport,
                    media_tracker,
                    expected_generation,
                    timeout_ms,
                    true,
                    error
                )) {
                return false;
            }
        }
        if (!deadline_timeout(
                recovery_deadline_ms,
                UINT32_C(250),
                &timeout_ms,
                error
            )) {
            return false;
        }
        gdox_sleep_ms(timeout_ms);
    }
    *error = last;
    return false;
}

static bool read_range_with_recovery(
    gdox_mt1887_context *context,
    uint64_t recovery_deadline_ms,
    uint32_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    const uint64_t expected_generation = context->media_tracker.generation;
    uint32_t attempt;
    uint32_t timeout_ms;
    gdox_error last;

    gdox_error_clear(&last);
    for (attempt = 0U; attempt <= context->retries; ++attempt) {
        if (ladder_aborted(&context->abort, error)) {
            return false;
        }
        if (!deadline_timeout(
                recovery_deadline_ms,
                GDOX_MT_READ_TIMEOUT_MS,
                &timeout_ms,
                &last
            )) {
            break;
        }
        if (attempt != 0U && !media_remains_current(
                &context->transport,
                &context->media_tracker,
                expected_generation,
                timeout_ms,
                true,
                &last
            )) {
            break;
        }
        if (gdox_mmc_read_12(
                &context->transport,
                lba,
                blocks,
                context->max_read_blocks,
                GDOX_LOGICAL_SECTOR_BYTES,
                output,
                output_bytes,
                timeout_ms,
                &last
            )) {
            (void)atomic_fetch_add_explicit(
                &context->read_commands,
                UINT64_C(1),
                memory_order_relaxed
            );
            (void)atomic_fetch_add_explicit(
                &context->read_sectors,
                (uint64_t)blocks,
                memory_order_relaxed
            );
            (void)atomic_fetch_add_explicit(
                &context->read_bytes,
                (uint64_t)blocks * GDOX_LOGICAL_SECTOR_BYTES,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &context->last_read_lba,
                (uint64_t)lba + (uint64_t)blocks - 1U,
                memory_order_relaxed
            );
            if (attempt != 0U) {
                (void)fprintf(
                    stderr,
                    "GDOX: recovered optical read at physical sector %u"
                    " after %u retry\n",
                    lba,
                    attempt
                );
                (void)fflush(stderr);
            }
            return true;
        }
        if (last.code == GDOX_ERROR_NOT_FOUND
            || last.code == GDOX_ERROR_CANCELLED) {
            break;
        }
        if (gdox_monotonic_ms() >= recovery_deadline_ms) {
            break;
        }
        if (attempt < context->retries) {
            (void)fprintf(
                stderr,
                "GDOX: retrying optical read at physical sector %u"
                " (%u blocks): %s\n",
                lba,
                blocks,
                last.message
            );
            (void)fflush(stderr);
            gdox_error recovery;
            bool recovered;

            gdox_error_clear(&recovery);
            recovered = recover_optical(
                &context->transport,
                &context->media_tracker,
                expected_generation,
                &context->abort,
                recovery_deadline_ms,
                &recovery
            );
            if (recovered) {
                recovered = ensure_xgd(
                    &context->transport,
                    context->profile,
                    context->media,
                    &context->abort,
                    recovery_deadline_ms,
                    &recovery
                );
            }
            if (!recovered) {
                last = recovery;
                if (recovery.code == GDOX_ERROR_NOT_FOUND
                    || recovery.code == GDOX_ERROR_CANCELLED) {
                    break;
                }
            } else {
                if (gdox_monotonic_ms() < recovery_deadline_ms) {
                    (void)request_read_speed(
                        &context->transport,
                        context->read_speed_kbps,
                        recovery_deadline_ms,
                        &recovery
                    );
                }
            }
            if (!deadline_timeout(
                    recovery_deadline_ms,
                    UINT32_C(150) * (attempt + 1U),
                    &timeout_ms,
                    &last
                )) {
                break;
            }
            gdox_sleep_ms(timeout_ms);
        }
    }
    *error = last;
    return false;
}

static void add_sector_context(gdox_error *error, uint32_t lba)
{
    const gdox_error cause = *error;
    char message[GDOX_ERROR_MESSAGE_CAPACITY];

    (void)snprintf(
        message,
        sizeof(message),
        "could not read Xbox disc sector %u: %.280s",
        lba,
        cause.message
    );
    gdox_error_set(error, cause.code, message);
}

static bool read_chunk(
    gdox_mt1887_context *context,
    uint64_t recovery_deadline_ms,
    uint32_t lba,
    uint32_t blocks,
    uint8_t *output,
    gdox_error *error
)
{
    const size_t bytes = (size_t)blocks * GDOX_LOGICAL_SECTOR_BYTES;
    uint32_t first_blocks;
    uint32_t second_blocks;

    if (read_range_with_recovery(
            context,
            recovery_deadline_ms,
            lba,
            blocks,
            output,
            bytes,
            error
        )) {
        return true;
    }
    if (blocks == 1U
        || error->code == GDOX_ERROR_NOT_FOUND
        || error->code == GDOX_ERROR_CANCELLED
        || gdox_monotonic_ms() >= recovery_deadline_ms) {
        add_sector_context(error, lba);
        return false;
    }
    first_blocks = blocks / 2U;
    second_blocks = blocks - first_blocks;
    if (!read_chunk(
            context,
            recovery_deadline_ms,
            lba,
            first_blocks,
            output,
            error
        )
        || !read_chunk(
            context,
            recovery_deadline_ms,
            lba + first_blocks,
            second_blocks,
            output + (size_t)first_blocks * GDOX_LOGICAL_SECTOR_BYTES,
            error
        )) {
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static uint64_t mt_sector_count(const void *context)
{
    const gdox_mt1887_context *source = context;
    return source->media->live_sectors;
}

static bool mt_read(
    void *raw_context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    gdox_mt1887_context *context = raw_context;
    uint32_t remaining = blocks;
    uint32_t current_lba = (uint32_t)lba;
    size_t output_offset = 0U;
    uint64_t recovery_deadline_ms;
    bool success = true;

    (void)output_bytes;
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not lock optical transport");
        return false;
    }
    recovery_deadline_ms = gdox_monotonic_ms() + GDOX_MT_RECOVERY_BUDGET_MS;
    while (remaining != 0U) {
        const uint32_t chunk =
            remaining < context->max_read_blocks
                ? remaining
                : context->max_read_blocks;
        if (!read_chunk(
                context,
                recovery_deadline_ms,
                current_lba,
                chunk,
                output + output_offset,
                error
            )) {
            success = false;
            break;
        }
        current_lba += chunk;
        remaining -= chunk;
        output_offset += (size_t)chunk * GDOX_LOGICAL_SECTOR_BYTES;
    }
    gdox_mutex_unlock(&context->mutex);
    return success;
}

static void mt_observe_media(
    const void *raw_context,
    gdox_media_observation *output
)
{
    gdox_mt1887_context *context = (gdox_mt1887_context *)raw_context;

    output->readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    output->generation = 0U;
    output->event = GDOX_MEDIA_EVENT_NONE;
    if (!gdox_mutex_lock(&context->mutex)) {
        return;
    }
    gdox_mmc_observe_media(
        &context->transport,
        GDOX_MT_PRESENCE_TIMEOUT_MS,
        &context->media_tracker,
        output
    );
    gdox_mutex_unlock(&context->mutex);
}

static bool mt_media_present(const void *raw_context)
{
    gdox_media_observation observation;
    mt_observe_media(raw_context, &observation);
    return observation.readiness == GDOX_MEDIA_READINESS_PRESENT;
}

static bool mt_evidence(
    const void *raw_context,
    gdox_disc_evidence *output
)
{
    const gdox_mt1887_context *context = raw_context;
    if (output == NULL) {
        return false;
    }
    *output = context->evidence;
    return context->evidence.pfi_present
        || context->evidence.dmi_present
        || context->evidence.security_sector_present;
}

static bool mt_physical_read_stats(
    const void *raw_context,
    gdox_physical_read_stats *output
)
{
    const gdox_mt1887_context *context = raw_context;
    if (output == NULL) {
        return false;
    }
    output->commands = atomic_load_explicit(
        &context->read_commands,
        memory_order_relaxed
    );
    output->sectors = atomic_load_explicit(
        &context->read_sectors,
        memory_order_relaxed
    );
    output->bytes = atomic_load_explicit(
        &context->read_bytes,
        memory_order_relaxed
    );
    output->last_lba = atomic_load_explicit(
        &context->last_read_lba,
        memory_order_relaxed
    );
    return true;
}

static bool mt_prepare_close(void *raw_context, gdox_error *error)
{
    gdox_mt1887_context *context = raw_context;
    bool prepared = false;

    gdox_error_clear(error);
    if (!gdox_mutex_lock(&context->mutex)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not lock optical transport during close preparation"
        );
        return false;
    }
    if (context->restoration_required) {
        if (!restore_stock_after_streaming(
                &context->transport,
                context->profile,
                context->media,
                error
            )) {
            goto done;
        }
        context->restoration_required = false;
    }
    prepared = gdox_scsi_transport_prepare_close(
        &context->transport, error
    );

done:
    gdox_mutex_unlock(&context->mutex);
    return prepared;
}

static bool mt_close(void *raw_context, gdox_error *error)
{
    gdox_mt1887_context *context = raw_context;
    const bool closed = gdox_scsi_transport_close(
        &context->transport, error
    );

    gdox_mutex_destroy(&context->mutex);
    free(context);
    return closed;
}

static void mt_abort(void *raw_context)
{
    gdox_mt1887_context *context = raw_context;
    atomic_store_explicit(&context->abort, true, memory_order_release);
}

static const gdox_sector_source_ops mt_source_ops = {
    mt_sector_count,
    mt_read,
    mt_media_present,
    mt_close,
    mt_evidence,
    mt_physical_read_stats,
    mt_abort,
    mt_prepare_close,
    mt_observe_media,
};

static bool open_discovered_gp63(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)context;
    return gdox_usb_bot_open(
        GDOX_USB_BOT_GP63,
        transport,
        error
    );
}

static bool open_discovered_gp65(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)context;
    return gdox_usb_bot_open(
        GDOX_USB_BOT_GP65,
        transport,
        error
    );
}

static bool open_validated_transport(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    gdox_usb_bot_identity expected_identity,
    gdox_scsi_transport *transport,
    gdox_mmc_identity *identity,
    const gdox_mt1887_profile **profile,
    gdox_error *error
)
{
    uint32_t attempt;

    for (attempt = 0U; attempt < GDOX_MT_INQUIRY_ATTEMPTS; ++attempt) {
        if (!opener(opener_context, transport, error)) {
            return false;
        }
        if (gdox_mmc_inquiry(
                transport,
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                identity,
                error
            )) {
            break;
        }
        {
            const gdox_error operation_error = *error;
            gdox_error close_error;

            if (!gdox_scsi_transport_close(transport, &close_error)) {
                *error = close_error;
                return false;
            }
            *error = operation_error;
        }
        if (error->code != GDOX_ERROR_TRANSPORT
            || attempt + 1U == GDOX_MT_INQUIRY_ATTEMPTS) {
            return false;
        }
        gdox_sleep_ms(UINT32_C(100));
    }
    *profile = gdox_mt1887_profile_find(
        expected_identity,
        identity->vendor,
        identity->model,
        identity->revision
    );
    if (*profile == NULL) {
        gdox_error operation_error;
        gdox_error close_error;

        gdox_error_set(
            &operation_error,
            GDOX_ERROR_UNSUPPORTED,
            "USB device does not match the selected validated optical profile"
        );
        if (!gdox_scsi_transport_close(transport, &close_error)) {
            *error = close_error;
        } else {
            *error = operation_error;
        }
        return false;
    }
    return true;
}

static void retain_failed_open(
    gdox_mt1887_context *context,
    gdox_sector_source *source
)
{
    source->context = context;
    source->ops = &mt_source_ops;
}

static void cleanup_failed_open(
    gdox_mt1887_context *context,
    gdox_sector_source *source,
    const gdox_error *operation_error,
    gdox_error *error
)
{
    gdox_error restoration_error;
    gdox_error close_error;
    if (context->restoration_required) {
        if (context->profile == NULL || context->media == NULL) {
            retain_failed_open(context, source);
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "optical initialization cannot restore an incomplete drive profile"
            );
            return;
        }
        if (!restore_stock_after_streaming(
            &context->transport,
            context->profile,
            context->media,
            &restoration_error
        )) {
            retain_failed_open(context, source);
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "optical initialization failed and SRAM restoration also failed; power-cycle the drive"
            );
            return;
        }
        context->restoration_required = false;
    }
    if (!gdox_scsi_transport_close(&context->transport, &close_error)) {
        retain_failed_open(context, source);
        *error = close_error;
        return;
    }
    gdox_mutex_destroy(&context->mutex);
    free(context);
    *error = *operation_error;
}

static bool wait_for_ready(
    gdox_mt1887_context *context,
    uint32_t ready_timeout_ms,
    gdox_error *error
)
{
    const uint32_t attempts = ready_timeout_ms / UINT32_C(500) + 1U;
    uint32_t attempt;

    gdox_error_clear(error);
    for (attempt = 0U; attempt < attempts; ++attempt) {
        if (gdox_mmc_test_unit_ready(
                &context->transport,
                GDOX_MT_DEFAULT_TIMEOUT_MS,
                error
            )) {
            return true;
        }
        if (attempt + 1U < attempts) {
            gdox_sleep_ms(UINT32_C(500));
        }
    }
    return false;
}

static bool read_and_validate_physical_media(
    gdox_mt1887_context *context,
    bool detect_media,
    gdox_error *error
)
{
    uint8_t pfi[2052];
    const gdox_mt1887_media_profile *physical_media;
    size_t transferred;

    if (!gdox_mmc_read_dvd_structure(
            &context->transport,
            0U,
            pfi,
            sizeof(pfi),
            UINT32_C(10000),
            &transferred,
            error
        ) || transferred != sizeof(pfi)) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "disc did not return a complete physical-format descriptor"
            );
        }
        return false;
    }
    physical_media = detect_media
        ? gdox_mt1887_media_profile_select_physical_geometry(
            context->profile, pfi + 17U
        )
        : context->media;
    if (physical_media == NULL
        || !gdox_mt1887_media_stock_geometry_matches(
            physical_media, pfi + 17U
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            detect_media
                ? "disc physical format does not match one validated GP63 XGD profile"
                : "disc does not have the selected media profile's physical format"
        );
        return false;
    }
    context->media = physical_media;
    context->evidence.pfi_present = true;
    memcpy(context->evidence.pfi, pfi + 4U, GDOX_DISC_STRUCTURE_BYTES);
    return true;
}

static void collect_optional_disc_evidence(gdox_mt1887_context *context)
{
    uint8_t dmi[2052];
    size_t transferred;
    gdox_error dmi_error;

    gdox_error_clear(&dmi_error);
    if (gdox_mmc_read_dvd_structure(
            &context->transport,
            0x04U,
            dmi,
            sizeof(dmi),
            UINT32_C(10000),
            &transferred,
            &dmi_error
        )
        && transferred == sizeof(dmi)) {
        context->evidence.dmi_present = true;
        memcpy(context->evidence.dmi, dmi + 4U, GDOX_DISC_STRUCTURE_BYTES);
    } else {
        (void)snprintf(
            context->evidence.note,
            sizeof(context->evidence.note),
            "%s",
            "DMI evidence was unavailable; full-disc output cannot claim complete evidence."
        );
    }
    if (context->evidence.note[0] == '\0') {
        (void)snprintf(
            context->evidence.note,
            sizeof(context->evidence.note),
            "%s",
            "This drive does not expose decrypted security-sector evidence."
        );
    }
}

static bool normalize_stock_state(
    gdox_mt1887_context *context,
    gdox_error *error
)
{
    return recover_known_partial(
        &context->transport,
        context->profile,
        context->media,
        &context->abort,
        &context->restoration_required,
        0U,
        error
    );
}

static bool normalize_detected_media(
    gdox_mt1887_context *context,
    gdox_error *error
)
{
    gdox_mt1887_state state;
    const gdox_mt1887_media_profile *state_media;
    char message[GDOX_ERROR_MESSAGE_CAPACITY];

    if (!read_state(
            &context->transport,
            context->profile,
            &context->abort,
            &state,
            0U,
            error
        )) {
        return false;
    }
    if (gdox_mt1887_media_state_classify(
            context->media, context->profile, &state
        ) != GDOX_MT1887_MEDIA_STATE_UNKNOWN) {
        return recover_known_state(
            &context->transport,
            context->profile,
            context->media,
            &context->abort,
            &state,
            &context->restoration_required,
            0U,
            error
        );
    }
    state_media = gdox_mt1887_media_profile_select_known(
        context->profile, &state
    );
    if (state_media == NULL) {
        (void)snprintf(
            message,
            sizeof(message),
            "refusing GP63 session because physical media is validated but "
            "volatile state is unknown (capacity=%02x%02x%02x "
            "geometry=%02x%02x%02x last_lba=%08x block_size=%u)",
            state.capacity[0],
            state.capacity[1],
            state.capacity[2],
            state.geometry[0],
            state.geometry[1],
            state.geometry[2],
            state.last_lba,
            state.block_size
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
        return false;
    }
    if (state.block_size != GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "refusing GP63 profile switch because the logical block size is not 2048 bytes"
        );
        return false;
    }
    /*
     * The physical-format descriptor belongs to the inserted disc. A prior
     * disc can leave an otherwise exact validated profile in volatile SRAM,
     * so retarget that known state transactionally before activation.
     */
    context->restoration_required = true;
    return restore_stock(
        &context->transport,
        context->profile,
        context->media,
        &context->abort,
        0U,
        error
    );
}

static bool activate_live_state(
    gdox_mt1887_context *context,
    gdox_error *error
)
{
    /* This flag must be armed before the first volatile write. */
    context->restoration_required = true;
    return apply_xgd(
        &context->transport,
        context->profile,
        context->media,
        &context->abort,
        0U,
        error
    );
}

static void request_configured_read_speed(gdox_mt1887_context *context)
{
    gdox_error operation_error;

    if (request_read_speed(
            &context->transport,
            context->read_speed_kbps,
            0U,
            &operation_error
        )) {
        (void)fprintf(
            stderr,
            "GDOX: requested optical read speed %u KB/s\n",
            (unsigned int)context->read_speed_kbps
        );
        (void)fflush(stderr);
    } else {
        (void)fprintf(
            stderr,
            "GDOX: drive retained its current read speed: %s\n",
            operation_error.message
        );
        (void)fflush(stderr);
    }
}

static bool validate_live_descriptor(
    gdox_mt1887_context *context,
    gdox_error *error
)
{
    uint8_t descriptor[GDOX_LOGICAL_SECTOR_BYTES];
    gdox_error operation_error;

    if (gdox_mmc_read_12(
            &context->transport,
            context->media->descriptor_lba,
            1U,
            context->max_read_blocks,
            GDOX_LOGICAL_SECTOR_BYTES,
            descriptor,
            sizeof(descriptor),
            GDOX_MT_READ_TIMEOUT_MS,
            error
        ) && gdox_mt1887_media_descriptor_valid(
            context->media,
            descriptor,
            sizeof(descriptor)
        )) {
        return true;
    }
    operation_error = *error;
    if (!gdox_error_is_set(&operation_error)) {
        gdox_error_set(
            &operation_error,
            GDOX_ERROR_NOT_FOUND,
            "live XGD view did not contain an XDVDFS descriptor"
        );
    }
    *error = operation_error;
    return false;
}

static bool mt1887_source_open_for_media(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    gdox_usb_bot_identity expected_identity,
    const gdox_mt1887_media_profile *media,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    gdox_mt1887_context *context;
    gdox_mmc_identity identity;
    gdox_error operation_error;
    const bool detect_media = media == NULL;

    gdox_error_clear(error);
    if (opener == NULL
        || (expected_identity != GDOX_USB_BOT_GP63
            && expected_identity != GDOX_USB_BOT_GP65)
        || (detect_media && expected_identity != GDOX_USB_BOT_GP63)
        || (!detect_media
            && (media->kind == GDOX_MT1887_MEDIA_GP63_XGD2
                || media->kind == GDOX_MT1887_MEDIA_GP63_XGD3)
            && expected_identity != GDOX_USB_BOT_GP63)
        || source == NULL || gdox_source_is_valid(source)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a transport opener and empty source output are required"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate MT1887 source");
        return false;
    }
    atomic_init(&context->read_commands, UINT64_C(0));
    atomic_init(&context->read_sectors, UINT64_C(0));
    atomic_init(&context->read_bytes, UINT64_C(0));
    atomic_init(&context->last_read_lba, UINT64_C(0));
    atomic_init(&context->abort, false);
    context->media = media;
    context->read_speed_kbps = read_speed_kbps;
    if (!gdox_mutex_init(&context->mutex)) {
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize MT1887 source lock");
        return false;
    }
    if (!open_validated_transport(
            opener,
            opener_context,
            expected_identity,
            &context->transport,
            &identity,
            &context->profile,
            error
        )) {
        if (gdox_scsi_transport_is_valid(&context->transport)) {
            retain_failed_open(context, source);
        } else {
            gdox_mutex_destroy(&context->mutex);
            free(context);
        }
        return false;
    }
    if (!detect_media && !gdox_mt1887_media_profile_supports_hardware(
            context->media, context->profile
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "validated drive does not support the selected media profile"
        );
        operation_error = *error;
        cleanup_failed_open(context, source, &operation_error, error);
        return false;
    }
#if defined(_WIN32)
    context->max_read_blocks =
        gdox_mt1887_max_read_blocks(context->profile, true);
#else
    context->max_read_blocks =
        gdox_mt1887_max_read_blocks(context->profile, false);
#endif
    if (!wait_for_ready(context, ready_timeout_ms, error)) {
        operation_error = *error;
        cleanup_failed_open(context, source, &operation_error, error);
        return false;
    }
    if (!read_and_validate_physical_media(context, detect_media, error)
        || !(detect_media
            ? normalize_detected_media(context, error)
            : normalize_stock_state(context, error))) {
        operation_error = *error;
        cleanup_failed_open(context, source, &operation_error, error);
        return false;
    }
    collect_optional_disc_evidence(context);
    if (!activate_live_state(context, error)) {
        operation_error = *error;
        cleanup_failed_open(context, source, &operation_error, error);
        return false;
    }
    /*
     * SET CD SPEED applies to DVD reads as well. The platform adapter chooses
     * a rate appropriate to its power and throughput constraints. A drive may
     * legally reject this optional command, so streaming remains available at
     * its current speed in that case.
     */
    request_configured_read_speed(context);
    if (!validate_live_descriptor(context, error)) {
        operation_error = *error;
        cleanup_failed_open(context, source, &operation_error, error);
        return false;
    }
    context->retries = read_retries;
    gdox_mmc_media_tracker_begin_session(
        &context->transport,
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        &context->media_tracker
    );
    source->context = context;
    source->ops = &mt_source_ops;
    return true;
}

bool gdox_mt1887_source_open(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    gdox_usb_bot_identity expected_identity,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return mt1887_source_open_for_media(
        opener,
        opener_context,
        expected_identity,
        gdox_mt1887_media_profile_xgd1(),
        read_speed_kbps,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

bool gdox_mt1887_detected_source_open(
    gdox_mt1887_transport_opener opener,
    void *opener_context,
    uint16_t read_speed_kbps,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    const gdox_mt1887_media_profile **selected_media,
    gdox_error *error
)
{
    gdox_mt1887_context *context;

    if (selected_media == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "selected GP63 media output is required"
        );
        return false;
    }
    *selected_media = NULL;
    if (!mt1887_source_open_for_media(
            opener,
            opener_context,
            GDOX_USB_BOT_GP63,
            NULL,
            read_speed_kbps,
            read_retries,
            ready_timeout_ms,
            source,
            error
        )) {
        return false;
    }
    context = source->context;
    *selected_media = context->media;
    return true;
}

bool gdox_optical_open_gp63(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_mt1887_source_open(
        open_discovered_gp63,
        NULL,
        GDOX_USB_BOT_GP63,
        GDOX_MT_MAXIMUM_READ_SPEED,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

bool gdox_optical_open_gp63_media(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
)
{
    const gdox_mt1887_media_profile *selected;
    gdox_error close_error;

    if (info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical media information output is required"
        );
        return false;
    }
    memset(info, 0, sizeof(*info));
    if (!gdox_mt1887_detected_source_open(
            open_discovered_gp63,
            NULL,
            GDOX_MT_MAXIMUM_READ_SPEED,
            read_retries,
            ready_timeout_ms,
            source,
            &selected,
            error
        )) {
        return false;
    }
    switch (selected->kind) {
        case GDOX_MT1887_MEDIA_XGD1:
            info->profile = GDOX_OPTICAL_MEDIA_XGD1;
            break;
        case GDOX_MT1887_MEDIA_GP63_XGD2:
            info->profile = GDOX_OPTICAL_MEDIA_XGD2;
            info->game_partition_lba = selected->game_partition_lba;
            break;
        case GDOX_MT1887_MEDIA_GP63_XGD3:
            info->profile = GDOX_OPTICAL_MEDIA_XGD3;
            info->game_partition_lba = selected->game_partition_lba;
            break;
    }
    if (info->profile != GDOX_OPTICAL_MEDIA_UNKNOWN) {
        return true;
    }
    if (!gdox_source_close(source, &close_error)) {
        *error = close_error;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "selected GP63 media profile has no optical mapping"
        );
    }
    return false;
}

bool gdox_optical_open_gp65(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_mt1887_source_open(
        open_discovered_gp65,
        NULL,
        GDOX_USB_BOT_GP65,
        GDOX_MT_MAXIMUM_READ_SPEED,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

static bool eject_profile(
    gdox_mt1887_transport_opener opener,
    gdox_usb_bot_identity expected_identity,
    gdox_error *error
)
{
    gdox_scsi_transport transport = {0};
    gdox_mmc_identity identity;
    const gdox_mt1887_profile *profile;
    static const uint8_t allow_removal[6] = {0x1eU, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t eject[6] = {0x1bU, 0U, 0U, 0U, 0x02U, 0U};
    gdox_error ignored;
    gdox_error close_error;
    bool success;

    if (!open_validated_transport(
            opener,
            NULL,
            expected_identity,
            &transport,
            &identity,
            &profile,
            error
        )) {
        return false;
    }
    (void)gdox_scsi_command_none(
        &transport,
        "PREVENT ALLOW MEDIUM REMOVAL (allow)",
        allow_removal,
        sizeof(allow_removal),
        GDOX_MT_DEFAULT_TIMEOUT_MS,
        &ignored
    );
    success = gdox_scsi_command_none(
        &transport,
        "START STOP UNIT (eject)",
        eject,
        sizeof(eject),
        GDOX_MT_READ_TIMEOUT_MS,
        error
    );
    if (!gdox_scsi_transport_close(&transport, &close_error) && success) {
        *error = close_error;
        success = false;
    }
    return success;
}

bool gdox_optical_eject_gp63(gdox_error *error)
{
    return eject_profile(
        open_discovered_gp63,
        GDOX_USB_BOT_GP63,
        error
    );
}

bool gdox_optical_eject_gp65(gdox_error *error)
{
    return eject_profile(
        open_discovered_gp65,
        GDOX_USB_BOT_GP65,
        error
    );
}
