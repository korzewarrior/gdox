#include "platform/mmc_commands.h"

#include <limits.h>
#include <string.h>

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static void put_be_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void copy_ascii_field(
    char *output,
    size_t output_bytes,
    const uint8_t *input,
    size_t input_bytes
)
{
    size_t begin = 0U;
    size_t end = input_bytes;
    size_t length;

    while (begin < end && (input[begin] == ' ' || input[begin] == 0U)) {
        ++begin;
    }
    while (end > begin
        && (input[end - 1U] == ' ' || input[end - 1U] == 0U)) {
        --end;
    }
    length = end - begin;
    if (length >= output_bytes) {
        length = output_bytes - 1U;
    }
    memcpy(output, input + begin, length);
    output[length] = '\0';
}

bool gdox_mmc_inquiry(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_identity *identity,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x12U, 0U, 0U, 0U, 96U, 0U};
    uint8_t response[96];
    size_t transferred;

    if (identity == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "MMC identity output is required"
        );
        return false;
    }
    if (!gdox_scsi_command_in(
            transport,
            "INQUIRY",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            timeout_ms,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred < 36U) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "INQUIRY returned fewer than 36 bytes"
        );
        return false;
    }
    copy_ascii_field(
        identity->vendor,
        sizeof(identity->vendor),
        response + 8U,
        8U
    );
    copy_ascii_field(
        identity->model,
        sizeof(identity->model),
        response + 16U,
        16U
    );
    copy_ascii_field(
        identity->revision,
        sizeof(identity->revision),
        response + 32U,
        4U
    );
    return true;
}

bool gdox_mmc_test_unit_ready(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0};
    return gdox_scsi_command_none(
        transport,
        "TEST UNIT READY",
        cdb,
        sizeof(cdb),
        timeout_ms,
        error
    );
}

bool gdox_mmc_request_sense(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    uint8_t output[18],
    size_t *transferred,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x03U, 0U, 0U, 0U, 18U, 0U};

    if (output == NULL || transferred == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "REQUEST SENSE outputs are required"
        );
        return false;
    }
    return gdox_scsi_command_in(
        transport,
        "REQUEST SENSE",
        cdb,
        sizeof(cdb),
        output,
        18U,
        timeout_ms,
        transferred,
        error
    );
}

bool gdox_mmc_parse_media_event(
    const uint8_t *response,
    size_t response_bytes,
    gdox_media_event *event
)
{
    uint16_t payload_bytes;
    uint8_t event_code;

    if (event == NULL) {
        return false;
    }
    *event = GDOX_MEDIA_EVENT_NONE;
    if (response == NULL || response_bytes < 4U) {
        return false;
    }
    payload_bytes = (uint16_t)((uint16_t)response[0] << 8U)
        | response[1];
    if ((response[2] & 0x80U) != 0U) {
        return true;
    }
    if ((response[2] & 0x07U) != 0x04U
        || payload_bytes < 6U || response_bytes < 8U) {
        return false;
    }
    event_code = (uint8_t)(response[4] & 0x0fU);
    switch (event_code) {
        case 0U:
            break;
        case 1U:
            *event = GDOX_MEDIA_EVENT_EJECT_REQUEST;
            break;
        case 2U:
            *event = GDOX_MEDIA_EVENT_NEW_MEDIA;
            break;
        case 3U:
            *event = GDOX_MEDIA_EVENT_REMOVAL;
            break;
        case 4U:
            *event = GDOX_MEDIA_EVENT_CHANGED;
            break;
        default:
            break;
    }
    return true;
}

static bool decode_sense(
    const uint8_t *sense,
    size_t sense_bytes,
    uint8_t *sense_key,
    uint8_t *additional_code
)
{
    const uint8_t response_code = sense_bytes != 0U
        ? (uint8_t)(sense[0] & 0x7fU)
        : 0U;

    if ((response_code == 0x70U || response_code == 0x71U)
        && sense_bytes >= 14U) {
        *sense_key = (uint8_t)(sense[2] & 0x0fU);
        *additional_code = sense[12];
        return true;
    }
    if ((response_code == 0x72U || response_code == 0x73U)
        && sense_bytes >= 4U) {
        *sense_key = (uint8_t)(sense[1] & 0x0fU);
        *additional_code = sense[2];
        return true;
    }
    return false;
}

static void advance_media_generation(gdox_mmc_media_tracker *tracker)
{
    ++tracker->generation;
    if (tracker->generation == 0U) {
        ++tracker->generation;
    }
}

static void note_media_absent(gdox_mmc_media_tracker *tracker)
{
    if (!tracker->absence_latched && !tracker->change_latched) {
        advance_media_generation(tracker);
    }
    tracker->absence_latched = true;
    tracker->pending_event = GDOX_MEDIA_EVENT_NONE;
}

static void note_media_changed(gdox_mmc_media_tracker *tracker)
{
    if (!tracker->change_latched && !tracker->absence_latched) {
        advance_media_generation(tracker);
    }
    tracker->change_latched = true;
    tracker->pending_event = GDOX_MEDIA_EVENT_NONE;
}

static void note_media_event(
    gdox_mmc_media_tracker *tracker,
    gdox_media_event event
)
{
    if (event == GDOX_MEDIA_EVENT_EJECT_REQUEST) {
        if (tracker->pending_event == GDOX_MEDIA_EVENT_NONE) {
            tracker->pending_event = event;
        }
        return;
    }
    if (event == GDOX_MEDIA_EVENT_NEW_MEDIA
        || event == GDOX_MEDIA_EVENT_REMOVAL
        || event == GDOX_MEDIA_EVENT_CHANGED) {
        note_media_changed(tracker);
    }
}

static bool poll_media_event(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker,
    gdox_media_event *observed_event
)
{
    static const uint8_t cdb[10] = {
        0x4aU, 0x01U, 0U, 0U, 0x10U, 0U, 0U, 0U, 8U, 0U,
    };
    uint8_t response[8] = {0};
    size_t transferred = 0U;
    gdox_media_event event;
    gdox_error ignored;

    if (tracker == NULL || !gdox_scsi_transport_is_valid(transport)) {
        return false;
    }
    if (observed_event != NULL) {
        *observed_event = GDOX_MEDIA_EVENT_NONE;
    }
    gdox_error_clear(&ignored);
    if (!gdox_scsi_command_in(
            transport,
            "GET EVENT STATUS NOTIFICATION (media)",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            timeout_ms,
            &transferred,
            &ignored
        )) {
        (void)gdox_mmc_media_tracker_capture_transport_sense(
            transport, timeout_ms, tracker
        );
        return false;
    }
    if (!gdox_mmc_parse_media_event(response, transferred, &event)) {
        return false;
    }
    note_media_event(tracker, event);
    if (observed_event != NULL) {
        *observed_event = event;
    }
    return true;
}

bool gdox_mmc_poll_media_event(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker
)
{
    return poll_media_event(transport, timeout_ms, tracker, NULL);
}

void gdox_mmc_media_tracker_begin_session(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker
)
{
    enum { baseline_event_limit = 16U };
    uint32_t attempt;

    if (tracker == NULL) {
        return;
    }
    for (attempt = 0U; attempt < baseline_event_limit; ++attempt) {
        gdox_media_event event;

        if (!poll_media_event(
                transport, timeout_ms, tracker, &event
            ) || event == GDOX_MEDIA_EVENT_NONE) {
            break;
        }
    }
    tracker->pending_event = GDOX_MEDIA_EVENT_NONE;
}

bool gdox_mmc_media_tracker_transitioned(
    const gdox_mmc_media_tracker *tracker,
    uint64_t expected_generation
)
{
    return tracker != NULL
        && (tracker->generation != expected_generation
            || tracker->pending_event != GDOX_MEDIA_EVENT_NONE);
}

void gdox_mmc_media_tracker_note_sense(
    gdox_mmc_media_tracker *tracker,
    const uint8_t *sense,
    size_t sense_bytes
)
{
    uint8_t sense_key;
    uint8_t additional_code;

    if (tracker == NULL || sense == NULL
        || !decode_sense(
            sense,
            sense_bytes,
            &sense_key,
            &additional_code
        )) {
        return;
    }
    if (sense_key == 0x02U && additional_code == 0x3aU) {
        note_media_absent(tracker);
    } else if (sense_key == 0x06U && additional_code == 0x28U) {
        note_media_changed(tracker);
    }
}

bool gdox_mmc_media_tracker_capture_transport_sense(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker
)
{
    uint8_t sense[32];
    size_t sense_bytes;
    gdox_error error;

    if (tracker == NULL || !gdox_scsi_transport_is_valid(transport)) {
        return false;
    }
    if (!gdox_scsi_transport_last_sense(
            transport,
            sense,
            sizeof(sense),
            &sense_bytes
        )) {
        gdox_error_clear(&error);
        if (!gdox_mmc_request_sense(
                transport,
                timeout_ms,
                sense,
                &sense_bytes,
                &error
            )) {
            return false;
        }
    }
    gdox_mmc_media_tracker_note_sense(tracker, sense, sense_bytes);
    return true;
}

void gdox_mmc_observe_media(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    gdox_mmc_media_tracker *tracker,
    gdox_media_observation *output
)
{
    uint8_t sense[18] = {0};
    size_t sense_bytes = 0U;
    gdox_error error;
    uint8_t sense_key = 0U;
    uint8_t additional_code = 0U;
    bool sense_known = false;

    if (output == NULL) {
        return;
    }
    output->readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    output->generation = tracker != NULL ? tracker->generation : 0U;
    output->event = tracker != NULL
        ? tracker->pending_event
        : GDOX_MEDIA_EVENT_NONE;
    if (tracker == NULL || !gdox_scsi_transport_is_valid(transport)) {
        return;
    }
    (void)gdox_mmc_poll_media_event(transport, timeout_ms, tracker);
    gdox_error_clear(&error);
    if (gdox_mmc_test_unit_ready(transport, timeout_ms, &error)) {
        tracker->absence_latched = false;
        tracker->change_latched = false;
        output->readiness = GDOX_MEDIA_READINESS_PRESENT;
        output->generation = tracker->generation;
        output->event = tracker->pending_event;
        return;
    }
    if (gdox_scsi_transport_last_sense(
            transport, sense, sizeof(sense), &sense_bytes
        )) {
        sense_known = decode_sense(
            sense,
            sense_bytes,
            &sense_key,
            &additional_code
        );
        gdox_mmc_media_tracker_note_sense(tracker, sense, sense_bytes);
    } else {
        gdox_error_clear(&error);
    }
    if (sense_bytes == 0U
        && gdox_mmc_request_sense(
            transport,
            timeout_ms,
            sense,
            &sense_bytes,
            &error
        )) {
        sense_known = decode_sense(
            sense,
            sense_bytes,
            &sense_key,
            &additional_code
        );
        gdox_mmc_media_tracker_note_sense(tracker, sense, sense_bytes);
    }
    if (sense_known && sense_key == 0x02U && additional_code == 0x3aU) {
        output->readiness = GDOX_MEDIA_READINESS_ABSENT;
    }
    output->generation = tracker->generation;
    output->event = tracker->pending_event;
}

bool gdox_mmc_read_capacity_10(
    gdox_scsi_transport *transport,
    uint32_t timeout_ms,
    uint32_t *last_lba,
    uint32_t *block_size,
    gdox_error *error
)
{
    static const uint8_t cdb[10] = {0x25U};
    uint8_t response[8];
    size_t transferred;

    if (last_lba == NULL || block_size == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "READ CAPACITY outputs are required"
        );
        return false;
    }
    if (!gdox_scsi_command_in(
            transport,
            "READ CAPACITY(10)",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            timeout_ms,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != sizeof(response)) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "READ CAPACITY(10) returned a short response"
        );
        return false;
    }
    *last_lba = read_be_u32(response);
    *block_size = read_be_u32(response + 4U);
    return true;
}

bool gdox_mmc_read_dvd_structure(
    gdox_scsi_transport *transport,
    uint8_t format,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};

    if (output == NULL || transferred == NULL
        || output_bytes > UINT16_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "invalid READ DVD STRUCTURE output"
        );
        return false;
    }
    cdb[0] = 0xadU;
    cdb[7] = format;
    put_be_u16(cdb + 8U, (uint16_t)output_bytes);
    return gdox_scsi_command_in(
        transport,
        "READ DVD STRUCTURE",
        cdb,
        sizeof(cdb),
        output,
        output_bytes,
        timeout_ms,
        transferred,
        error
    );
}

static bool read_blocks(
    gdox_scsi_transport *transport,
    uint8_t opcode,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};
    size_t transferred;
    const uint64_t expected = (uint64_t)blocks * block_bytes;

    if (output == NULL || blocks == 0U || blocks > maximum_blocks
        || block_bytes == 0U || expected != output_bytes
        || (opcode == 0x28U && blocks > UINT16_MAX)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "invalid bounded MMC read request"
        );
        return false;
    }
    cdb[0] = opcode;
    put_be_u32(cdb + 2U, lba);
    if (opcode == 0x28U) {
        put_be_u16(cdb + 7U, (uint16_t)blocks);
    } else {
        put_be_u32(cdb + 6U, blocks);
    }
    if (!gdox_scsi_command_in(
            transport,
            opcode == 0x28U ? "READ(10)" : "READ(12)",
            cdb,
            opcode == 0x28U ? 10U : sizeof(cdb),
            output,
            output_bytes,
            timeout_ms,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            opcode == 0x28U
                ? "READ(10) returned a short transfer"
                : "READ(12) returned a short transfer"
        );
        return false;
    }
    return true;
}

bool gdox_mmc_read_10(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    return read_blocks(
        transport,
        0x28U,
        lba,
        blocks,
        maximum_blocks,
        block_bytes,
        output,
        output_bytes,
        timeout_ms,
        error
    );
}

bool gdox_mmc_read_12(
    gdox_scsi_transport *transport,
    uint32_t lba,
    uint32_t blocks,
    uint32_t maximum_blocks,
    uint32_t block_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    return read_blocks(
        transport,
        0xa8U,
        lba,
        blocks,
        maximum_blocks,
        block_bytes,
        output,
        output_bytes,
        timeout_ms,
        error
    );
}
