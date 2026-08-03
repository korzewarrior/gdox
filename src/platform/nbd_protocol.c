#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/nbd_protocol.h"

#include "gdox/disc.h"
#include "gdox/error.h"

#include "platform/nbd_internal.h"
#include "platform/nbd_telemetry.h"
#include "platform/nbd_wire.h"
#include "platform/portable_sync.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBD_SLOW_READ_MS UINT64_C(1000)

static bool send_option_reply(
    gdox_nbd_socket client,
    uint32_t option,
    uint32_t reply,
    const uint8_t *payload,
    size_t payload_bytes
)
{
    uint8_t header[GDOX_NBD_OPTION_REPLY_HEADER_BYTES];

    gdox_nbd_wire_option_reply_header(
        header,
        option,
        reply,
        (uint32_t)payload_bytes
    );
    return gdox_nbd_socket_write_all(client, header, sizeof(header))
        && (payload_bytes == 0U
            || gdox_nbd_socket_write_all(client, payload, payload_bytes));
}

static bool send_simple_reply(
    gdox_nbd_socket client,
    uint32_t reply_error,
    uint64_t handle,
    const uint8_t *data,
    size_t data_bytes
)
{
    uint8_t header[GDOX_NBD_SIMPLE_REPLY_BYTES];

    gdox_nbd_wire_simple_reply(header, reply_error, handle);
    return gdox_nbd_socket_write_all(client, header, sizeof(header))
        && (data_bytes == 0U
            || gdox_nbd_socket_write_all(client, data, data_bytes));
}

static bool read_disc_request(
    gdox_nbd_export *exported,
    uint64_t offset,
    uint8_t *output,
    uint32_t output_bytes,
    gdox_error *error
)
{
    size_t received = 0U;

    gdox_error_clear(error);
    if (gdox_disc_read_at(
            &exported->disc,
            offset,
            output,
            output_bytes,
            &received,
            error
        ) && received == output_bytes) {
        return true;
    }
    if (!gdox_error_is_set(error)) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "disc source returned an incomplete read"
        );
    }
    return false;
}

static bool send_export_info(
    gdox_nbd_socket client,
    uint32_t option,
    uint64_t length,
    uint16_t export_flags
)
{
    uint8_t block_sizes[GDOX_NBD_BLOCK_SIZE_INFO_BYTES];
    uint8_t export_info[GDOX_NBD_EXPORT_INFO_BYTES];

    gdox_nbd_wire_block_size_info(block_sizes);
    if (!send_option_reply(
            client,
            option,
            GDOX_NBD_REP_INFO,
            block_sizes,
            sizeof(block_sizes)
        )) {
        return false;
    }
    gdox_nbd_wire_export_info(export_info, length, export_flags);
    return send_option_reply(
        client,
        option,
        GDOX_NBD_REP_INFO,
        export_info,
        sizeof(export_info)
    );
}

static bool read_option_request(
    gdox_nbd_socket client,
    uint32_t *option,
    uint8_t **payload,
    uint32_t *payload_length
)
{
    uint8_t header[GDOX_NBD_OPTION_HEADER_BYTES];

    *payload = NULL;
    if (!gdox_nbd_socket_read_exact(client, header, sizeof(header))) {
        gdox_nbd_socket_set_protocol_error();
        return false;
    }
    if (!gdox_nbd_wire_parse_option_header(
            header,
            option,
            payload_length
        )) {
        gdox_nbd_socket_set_protocol_error();
        return false;
    }
    if (*payload_length == 0U) {
        return true;
    }
    *payload = malloc(*payload_length);
    if (*payload == NULL) {
        gdox_nbd_socket_set_no_memory_error();
        return false;
    }
    if (!gdox_nbd_socket_read_exact(client, *payload, *payload_length)) {
        free(*payload);
        *payload = NULL;
        return false;
    }
    return true;
}

static bool export_name_matches(
    const gdox_nbd_export *exported,
    const uint8_t *name,
    size_t name_bytes
)
{
    const size_t expected_bytes = strlen(exported->export_name);

    return name_bytes == expected_bytes
        && (name_bytes == 0U
            || (name != NULL
                && memcmp(name, exported->export_name, name_bytes) == 0));
}

static bool negotiate_export_name(
    const gdox_nbd_export *exported,
    gdox_nbd_socket client,
    const uint8_t *payload,
    uint32_t payload_length,
    bool no_zeroes
)
{
    uint8_t response[GDOX_NBD_EXPORT_RESPONSE_BYTES];
    uint8_t zeroes[124] = {0};

    if (!export_name_matches(exported, payload, payload_length)) {
        gdox_nbd_socket_set_permission_error();
        return false;
    }
    gdox_nbd_wire_export_response(
        response,
        gdox_disc_length(&exported->disc),
        exported->export_flags
    );
    return gdox_nbd_socket_write_all(client, response, sizeof(response))
        && (no_zeroes
            || gdox_nbd_socket_write_all(client, zeroes, sizeof(zeroes)));
}

static bool negotiate_info(
    const gdox_nbd_export *exported,
    gdox_nbd_socket client,
    uint32_t option,
    const uint8_t *payload,
    uint32_t payload_length,
    bool *ready
)
{
    const uint8_t *name = NULL;
    size_t name_bytes = 0U;
    bool result;

    *ready = false;
    if (!gdox_nbd_wire_parse_info_name(
            payload,
            payload_length,
            &name,
            &name_bytes
        )) {
        return send_option_reply(
            client,
            option,
            GDOX_NBD_REP_ERR_INVALID,
            (const uint8_t *)"invalid info request",
            20U
        );
    }
    if (!export_name_matches(exported, name, name_bytes)) {
        return send_option_reply(
            client,
            option,
            GDOX_NBD_REP_ERR_UNKNOWN,
            (const uint8_t *)"unknown export",
            14U
        );
    }
    result = send_export_info(
        client,
        option,
        gdox_disc_length(&exported->disc),
        exported->export_flags
    ) && send_option_reply(
        client,
        option,
        GDOX_NBD_REP_ACK,
        NULL,
        0U
    );
    *ready = result && option == GDOX_NBD_OPT_GO;
    return result;
}

static bool negotiate(
    gdox_nbd_export *exported,
    gdox_nbd_socket client,
    bool no_zeroes
)
{
    for (;;) {
        uint32_t option;
        uint32_t payload_length;
        uint8_t *payload = NULL;
        bool ready = false;
        bool result;

        if (!read_option_request(
                client,
                &option,
                &payload,
                &payload_length
            )) {
            return false;
        }
        if (option == GDOX_NBD_OPT_ABORT) {
            result = send_option_reply(
                client,
                option,
                GDOX_NBD_REP_ACK,
                NULL,
                0U
            );
            free(payload);
            if (result) {
                gdox_nbd_socket_set_connection_aborted();
            }
            return false;
        }
        if (option == GDOX_NBD_OPT_EXPORT_NAME) {
            result = negotiate_export_name(
                exported,
                client,
                payload,
                payload_length,
                no_zeroes
            );
            free(payload);
            return result;
        }
        if (option == GDOX_NBD_OPT_INFO || option == GDOX_NBD_OPT_GO) {
            result = negotiate_info(
                exported,
                client,
                option,
                payload,
                payload_length,
                &ready
            );
        } else {
            result = send_option_reply(
                client,
                option,
                GDOX_NBD_REP_ERR_UNSUP,
                NULL,
                0U
            );
        }
        free(payload);
        if (ready) {
            return true;
        }
        if (!result) {
            return false;
        }
    }
}

static bool reserve_request_buffer(
    uint8_t **buffer,
    size_t *capacity,
    uint32_t length
)
{
    uint8_t *resized;

    if (length <= *capacity || length > GDOX_NBD_MAX_BUFFER_SIZE) {
        return true;
    }
    resized = realloc(*buffer, length);
    if (resized == NULL && length != 0U) {
        gdox_nbd_socket_set_no_memory_error();
        return false;
    }
    *buffer = resized;
    *capacity = length;
    return true;
}

static void report_slow_read(
    const gdox_nbd_request *request,
    uint64_t elapsed_ms
)
{
    if (elapsed_ms < NBD_SLOW_READ_MS) {
        return;
    }
    (void)fprintf(
        stderr,
        "GDOX: slow live read at sector %" PRIu64
        " (%u bytes, %" PRIu64 " ms)\n",
        request->offset / GDOX_LOGICAL_SECTOR_BYTES,
        request->length,
        elapsed_ms
    );
    (void)fflush(stderr);
}

static bool report_read_failure(
    gdox_nbd_export *exported,
    gdox_nbd_socket client,
    const gdox_nbd_request *request,
    const gdox_error *read_error
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];

    (void)snprintf(
        message,
        sizeof(message),
        "disc read failed at sector %" PRIu64
        " (byte offset %" PRIu64 ", %u bytes): %.240s",
        request->offset / GDOX_LOGICAL_SECTOR_BYTES,
        request->offset,
        request->length,
        read_error->message
    );
    (void)fprintf(stderr, "GDOX: %s\n", message);
    (void)fflush(stderr);
    gdox_nbd_export_record_runtime_error(exported, message);
    return send_simple_reply(
        client,
        GDOX_NBD_EIO,
        request->handle,
        NULL,
        0U
    );
}

static bool transmit_read(
    gdox_nbd_export *exported,
    gdox_nbd_socket client,
    const gdox_nbd_request *request,
    uint8_t *buffer
)
{
    const uint64_t disc_bytes = gdox_disc_length(&exported->disc);
    const uint64_t started_ms = gdox_monotonic_ms();
    gdox_physical_read_stats physical_before;
    gdox_physical_read_stats physical_after;
    gdox_error read_error;
    uint64_t finished_ms;
    uint64_t elapsed_ms;
    bool before_valid;
    bool after_valid;
    bool succeeded;

    if (!gdox_nbd_wire_read_is_valid(request, disc_bytes)) {
        finished_ms = gdox_monotonic_ms();
        elapsed_ms = finished_ms >= started_ms
            ? finished_ms - started_ms
            : 0U;
        gdox_nbd_telemetry_record(
            &exported->telemetry,
            &exported->state_mutex,
            request->offset,
            request->length,
            false,
            false,
            NULL,
            NULL,
            elapsed_ms
        );
        return send_simple_reply(
            client,
            GDOX_NBD_EINVAL,
            request->handle,
            NULL,
            0U
        );
    }
    before_valid = gdox_disc_physical_read_stats(
        &exported->disc,
        &physical_before
    );
    succeeded = read_disc_request(
        exported,
        request->offset,
        buffer,
        request->length,
        &read_error
    );
    after_valid = gdox_disc_physical_read_stats(
        &exported->disc,
        &physical_after
    );
    finished_ms = gdox_monotonic_ms();
    elapsed_ms = finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0U;
    gdox_nbd_telemetry_record(
        &exported->telemetry,
        &exported->state_mutex,
        request->offset,
        request->length,
        true,
        succeeded,
        before_valid ? &physical_before : NULL,
        after_valid ? &physical_after : NULL,
        elapsed_ms
    );
    if (!succeeded) {
        return report_read_failure(exported, client, request, &read_error);
    }
    report_slow_read(request, elapsed_ms);
    return send_simple_reply(
        client,
        0U,
        request->handle,
        buffer,
        request->length
    );
}

static bool transmit_write(
    gdox_nbd_socket client,
    const gdox_nbd_request *request,
    uint8_t *buffer,
    uint32_t reply_error
)
{
    if (request->length > GDOX_NBD_MAX_BUFFER_SIZE) {
        gdox_nbd_socket_set_protocol_error();
        return false;
    }
    if (request->length != 0U
        && !gdox_nbd_socket_read_exact(
            client,
            buffer,
            request->length
        )) {
        return false;
    }
    return send_simple_reply(
        client,
        reply_error,
        request->handle,
        NULL,
        0U
    );
}

static bool transmit(gdox_nbd_export *exported, gdox_nbd_socket client)
{
    uint8_t input[GDOX_NBD_REQUEST_BYTES];
    uint8_t *buffer = NULL;
    size_t buffer_capacity = 0U;
    bool result = false;

    for (;;) {
        gdox_nbd_request request;

        if (!gdox_nbd_socket_read_exact(client, input, sizeof(input))) {
            result = gdox_nbd_socket_error_is_connection_reset(
                gdox_nbd_socket_error()
            );
            break;
        }
        if (!gdox_nbd_wire_parse_request(input, &request)) {
            gdox_nbd_socket_set_protocol_error();
            break;
        }
        if (request.command == GDOX_NBD_CMD_DISC) {
            result = true;
            break;
        }
        if (!reserve_request_buffer(
                &buffer,
                &buffer_capacity,
                request.length
            )) {
            break;
        }
        if (request.command == GDOX_NBD_CMD_READ && request.flags == 0U) {
            result = transmit_read(exported, client, &request, buffer);
        } else if (request.command == GDOX_NBD_CMD_WRITE) {
            result = transmit_write(
                client,
                &request,
                buffer,
                request.flags == 0U
                    ? GDOX_NBD_EPERM
                    : GDOX_NBD_EINVAL
            );
        } else {
            result = send_simple_reply(
                client,
                GDOX_NBD_EINVAL,
                request.handle,
                NULL,
                0U
            );
        }
        if (!result) {
            break;
        }
    }
    free(buffer);
    return result;
}

bool gdox_nbd_protocol_handle(
    gdox_nbd_export *exported,
    gdox_nbd_socket client
)
{
    uint8_t greeting[GDOX_NBD_GREETING_BYTES];
    uint8_t client_flags_bytes[4];
    uint32_t client_flags;

    if (!gdox_nbd_socket_configure_client(client)) {
        return false;
    }
    gdox_nbd_wire_greeting(greeting);
    if (!gdox_nbd_socket_write_all(client, greeting, sizeof(greeting))
        || !gdox_nbd_socket_read_exact(
            client,
            client_flags_bytes,
            sizeof(client_flags_bytes)
        )) {
        return false;
    }
    if (!gdox_nbd_wire_parse_client_flags(
            client_flags_bytes,
            &client_flags
        )) {
        gdox_nbd_socket_set_protocol_error();
        return false;
    }
    if (!negotiate(
            exported,
            client,
            (client_flags & GDOX_NBD_FLAG_C_NO_ZEROES) != 0U
        )) {
        return false;
    }
    if (!gdox_nbd_socket_clear_timeouts(client)) {
        return false;
    }
    return transmit(exported, client);
}
