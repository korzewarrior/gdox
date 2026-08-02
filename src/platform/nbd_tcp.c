#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gdox/nbd.h"

#include "platform/portable_sync.h"

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <windows.h>

typedef SOCKET gdox_socket;
typedef int gdox_socklen;
typedef int gdox_socket_io_count;

#define GDOX_INVALID_SOCKET INVALID_SOCKET
#define GDOX_SOCKET_CONNECTION_ABORTED WSAECONNABORTED
#define GDOX_SOCKET_CONNECTION_RESET WSAECONNRESET
#define GDOX_SOCKET_INTERRUPTED WSAEINTR
#define GDOX_SOCKET_NO_MEMORY WSAENOBUFS
#define GDOX_SOCKET_PERMISSION WSAEACCES
#define GDOX_SOCKET_PROTOCOL WSAEPROTONOSUPPORT
#define GDOX_SOCKET_WOULD_BLOCK WSAEWOULDBLOCK

static int socket_error_get(void)
{
    return WSAGetLastError();
}

static void socket_error_set(int code)
{
    WSASetLastError(code);
}

static bool socket_is_valid(gdox_socket socket_handle)
{
    return socket_handle != GDOX_INVALID_SOCKET;
}

static void socket_close(gdox_socket socket_handle)
{
    if (socket_is_valid(socket_handle)) {
        (void)closesocket(socket_handle);
    }
}

static gdox_socket_io_count socket_receive(
    gdox_socket socket_handle,
    uint8_t *output,
    size_t bytes
)
{
    return recv(socket_handle, (char *)output, (int)bytes, 0);
}

static gdox_socket_io_count socket_send(
    gdox_socket socket_handle,
    const uint8_t *input,
    size_t bytes,
    int flags
)
{
    return send(
        socket_handle,
        (const char *)input,
        (int)bytes,
        flags
    );
}

static const char *socket_error_text(
    int code,
    char output[160]
)
{
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        (DWORD)code,
        0U,
        output,
        160U,
        NULL
    );
    while (length > 0U
        && (output[length - 1U] == '\r'
            || output[length - 1U] == '\n'
            || output[length - 1U] == ' ')) {
        output[--length] = '\0';
    }
    if (length == 0U) {
        (void)snprintf(output, 160U, "Windows socket error %d", code);
    }
    return output;
}

#else

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef int gdox_socket;
typedef socklen_t gdox_socklen;
typedef ssize_t gdox_socket_io_count;

#define GDOX_INVALID_SOCKET (-1)
#define GDOX_SOCKET_CONNECTION_ABORTED ECONNABORTED
#define GDOX_SOCKET_CONNECTION_RESET ECONNRESET
#define GDOX_SOCKET_INTERRUPTED EINTR
#define GDOX_SOCKET_NO_MEMORY ENOMEM
#define GDOX_SOCKET_PERMISSION EACCES
#define GDOX_SOCKET_PROTOCOL EPROTO
#define GDOX_SOCKET_WOULD_BLOCK EWOULDBLOCK

static int socket_error_get(void)
{
    return errno;
}

static void socket_error_set(int code)
{
    errno = code;
}

static bool socket_is_valid(gdox_socket socket_handle)
{
    return socket_handle >= 0;
}

static void socket_close(gdox_socket socket_handle)
{
    if (socket_is_valid(socket_handle)) {
        (void)close(socket_handle);
    }
}

static gdox_socket_io_count socket_receive(
    gdox_socket socket_handle,
    uint8_t *output,
    size_t bytes
)
{
    return recv(socket_handle, output, bytes, 0);
}

static gdox_socket_io_count socket_send(
    gdox_socket socket_handle,
    const uint8_t *input,
    size_t bytes,
    int flags
)
{
    return send(socket_handle, input, bytes, flags);
}

static const char *socket_error_text(
    int code,
    char output[160]
)
{
    (void)snprintf(output, 160U, "%s", strerror(code));
    return output;
}

#if defined(__linux__)
#include <sys/random.h>
#endif

#endif

#define NBD_INIT_MAGIC UINT64_C(0x4e42444d41474943)
#define NBD_OPTS_MAGIC UINT64_C(0x49484156454f5054)
#define NBD_REP_MAGIC UINT64_C(0x0003e889045565a9)
#define NBD_REQUEST_MAGIC UINT32_C(0x25609513)
#define NBD_SIMPLE_REPLY_MAGIC UINT32_C(0x67446698)

#define NBD_FLAG_FIXED_NEWSTYLE UINT16_C(0x0001)
#define NBD_FLAG_NO_ZEROES UINT16_C(0x0002)
#define NBD_FLAG_C_FIXED_NEWSTYLE UINT32_C(0x00000001)
#define NBD_FLAG_C_NO_ZEROES UINT32_C(0x00000002)
#define NBD_EXPORT_FLAGS UINT16_C(0x0003)

#define NBD_OPT_EXPORT_NAME UINT32_C(1)
#define NBD_OPT_ABORT UINT32_C(2)
#define NBD_OPT_INFO UINT32_C(6)
#define NBD_OPT_GO UINT32_C(7)
#define NBD_REP_ACK UINT32_C(1)
#define NBD_REP_INFO UINT32_C(3)
#define NBD_REP_ERR_UNSUP (UINT32_C(0x80000000) | UINT32_C(1))
#define NBD_REP_ERR_INVALID (UINT32_C(0x80000000) | UINT32_C(3))
#define NBD_REP_ERR_UNKNOWN (UINT32_C(0x80000000) | UINT32_C(6))
#define NBD_INFO_EXPORT UINT16_C(0)
#define NBD_INFO_BLOCK_SIZE UINT16_C(3)

#define NBD_CMD_READ UINT16_C(0)
#define NBD_CMD_WRITE UINT16_C(1)
#define NBD_CMD_DISC UINT16_C(2)
#define NBD_EPERM UINT32_C(1)
#define NBD_EIO UINT32_C(5)
#define NBD_EINVAL UINT32_C(22)

#define NBD_MAX_BUFFER_SIZE (32U * 1024U * 1024U)
#define NBD_MAX_OPTION_SIZE (64U * 1024U)
#define NBD_READ_ATTEMPTS 2U
#define NBD_READ_RETRY_DELAY_MS 100U
#define NBD_SLOW_READ_MS UINT64_C(1000)
#define NBD_TOKEN_BYTES 16U

struct gdox_nbd_export {
    gdox_socket listener;
    gdox_socket active;
    uint16_t port;
    char export_name[NBD_TOKEN_BYTES * 2U + 1U];
    char uri[96];
    char display_uri[96];
    gdox_random_disc disc;
    gdox_thread thread;
    gdox_mutex state_mutex;
    atomic_bool stopping;
    bool thread_started;
#if defined(_WIN32)
    bool winsock_started;
#endif
    bool runtime_failed;
    gdox_error runtime_error;
};

static void record_runtime_error(gdox_nbd_export *exported, const char *message);

static uint16_t read_be_u16(const uint8_t *input)
{
    return (uint16_t)(
        (uint16_t)((uint16_t)input[0] << 8U) | (uint16_t)input[1]
    );
}

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static uint64_t read_be_u64(const uint8_t *input)
{
    return (uint64_t)read_be_u32(input) << 32U | read_be_u32(input + 4U);
}

static void put_be_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)(value & 0xffU);
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)((value >> 16U) & 0xffU);
    output[2] = (uint8_t)((value >> 8U) & 0xffU);
    output[3] = (uint8_t)(value & 0xffU);
}

static void put_be_u64(uint8_t *output, uint64_t value)
{
    put_be_u32(output, (uint32_t)(value >> 32U));
    put_be_u32(output + 4U, (uint32_t)(value & UINT64_C(0xffffffff)));
}

static bool socket_read_exact(
    gdox_socket socket_handle,
    uint8_t *output,
    size_t bytes
)
{
    size_t completed = 0U;
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const size_t requested =
            remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
        const gdox_socket_io_count received = socket_receive(
            socket_handle,
            output + completed,
            requested
        );
        if (received > 0) {
            completed += (size_t)received;
        } else if (received == 0) {
            socket_error_set(GDOX_SOCKET_CONNECTION_RESET);
            return false;
        } else if (socket_error_get() != GDOX_SOCKET_INTERRUPTED) {
            return false;
        }
    }
    return true;
}

static bool socket_write_all(
    gdox_socket socket_handle,
    const uint8_t *input,
    size_t bytes
)
{
    size_t completed = 0U;
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const size_t requested =
            remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
#if defined(MSG_NOSIGNAL)
        const gdox_socket_io_count sent = socket_send(
            socket_handle,
            input + completed,
            requested,
            MSG_NOSIGNAL
        );
#else
        const gdox_socket_io_count sent = socket_send(
            socket_handle,
            input + completed,
            requested,
            0
        );
#endif
        if (sent > 0) {
            completed += (size_t)sent;
        } else if (sent == 0) {
            socket_error_set(GDOX_SOCKET_CONNECTION_RESET);
            return false;
        } else if (socket_error_get() != GDOX_SOCKET_INTERRUPTED) {
            return false;
        }
    }
    return true;
}

static bool send_option_reply(
    gdox_socket client,
    uint32_t option,
    uint32_t reply,
    const uint8_t *payload,
    size_t payload_bytes
)
{
    uint8_t header[20];
    put_be_u64(header, NBD_REP_MAGIC);
    put_be_u32(header + 8U, option);
    put_be_u32(header + 12U, reply);
    put_be_u32(header + 16U, (uint32_t)payload_bytes);
    return socket_write_all(client, header, sizeof(header))
        && (payload_bytes == 0U || socket_write_all(client, payload, payload_bytes));
}

static bool send_simple_reply(
    gdox_socket client,
    uint32_t reply_error,
    uint64_t handle,
    const uint8_t *data,
    size_t data_bytes
)
{
    uint8_t header[16];
    put_be_u32(header, NBD_SIMPLE_REPLY_MAGIC);
    put_be_u32(header + 4U, reply_error);
    put_be_u64(header + 8U, handle);
    return socket_write_all(client, header, sizeof(header))
        && (data_bytes == 0U || socket_write_all(client, data, data_bytes));
}

static bool read_disc_request(
    gdox_nbd_export *exported,
    uint64_t offset,
    uint8_t *output,
    uint32_t output_bytes,
    gdox_error *error
)
{
    uint32_t attempt;

    for (attempt = 0U; attempt < NBD_READ_ATTEMPTS; ++attempt) {
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
        if (error->code == GDOX_ERROR_NOT_FOUND
            || error->code == GDOX_ERROR_CANCELLED) {
            return false;
        }
        if (attempt + 1U < NBD_READ_ATTEMPTS) {
            gdox_sleep_ms(NBD_READ_RETRY_DELAY_MS);
        }
    }
    return false;
}

static bool parse_info_name(
    const uint8_t *payload,
    size_t payload_bytes,
    const uint8_t **name,
    size_t *name_bytes
)
{
    uint32_t declared_name;
    size_t end;
    uint16_t request_count;
    size_t expected;

    if (payload_bytes < 6U) {
        return false;
    }
    declared_name = read_be_u32(payload);
    if ((uint64_t)declared_name + 6U > payload_bytes) {
        return false;
    }
    end = 4U + (size_t)declared_name;
    request_count = read_be_u16(payload + end);
    if ((size_t)request_count > (SIZE_MAX - end - 2U) / 2U) {
        return false;
    }
    expected = end + 2U + (size_t)request_count * 2U;
    if (expected != payload_bytes) {
        return false;
    }
    *name = payload + 4U;
    *name_bytes = declared_name;
    return true;
}

static bool send_export_info(
    gdox_socket client,
    uint32_t option,
    uint64_t length
)
{
    uint8_t block_sizes[14];
    uint8_t export_info[12];

    put_be_u16(block_sizes, NBD_INFO_BLOCK_SIZE);
    put_be_u32(block_sizes + 2U, 1U);
    put_be_u32(block_sizes + 6U, 64U * 1024U);
    put_be_u32(block_sizes + 10U, NBD_MAX_BUFFER_SIZE);
    if (!send_option_reply(
            client,
            option,
            NBD_REP_INFO,
            block_sizes,
            sizeof(block_sizes)
        )) {
        return false;
    }
    put_be_u16(export_info, NBD_INFO_EXPORT);
    put_be_u64(export_info + 2U, length);
    put_be_u16(export_info + 10U, NBD_EXPORT_FLAGS);
    return send_option_reply(
        client,
        option,
        NBD_REP_INFO,
        export_info,
        sizeof(export_info)
    );
}

static bool read_option_request(
    gdox_socket client,
    uint32_t *option,
    uint8_t **payload,
    uint32_t *payload_length
)
{
    uint8_t header[16];

    *payload = NULL;
    if (!socket_read_exact(client, header, sizeof(header))
        || read_be_u64(header) != NBD_OPTS_MAGIC) {
        socket_error_set(GDOX_SOCKET_PROTOCOL);
        return false;
    }
    *option = read_be_u32(header + 8U);
    *payload_length = read_be_u32(header + 12U);
    if (*payload_length > NBD_MAX_OPTION_SIZE) {
        socket_error_set(GDOX_SOCKET_PROTOCOL);
        return false;
    }
    if (*payload_length == 0U) {
        return true;
    }
    *payload = malloc(*payload_length);
    if (*payload == NULL) {
        socket_error_set(GDOX_SOCKET_NO_MEMORY);
        return false;
    }
    if (!socket_read_exact(client, *payload, *payload_length)) {
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
    gdox_socket client,
    const uint8_t *payload,
    uint32_t payload_length,
    bool no_zeroes
)
{
    uint8_t response[10];
    uint8_t zeroes[124] = {0};

    if (!export_name_matches(exported, payload, payload_length)) {
        socket_error_set(GDOX_SOCKET_PERMISSION);
        return false;
    }
    put_be_u64(response, gdox_disc_length(&exported->disc));
    put_be_u16(response + 8U, NBD_EXPORT_FLAGS);
    return socket_write_all(client, response, sizeof(response))
        && (no_zeroes || socket_write_all(client, zeroes, sizeof(zeroes)));
}

static bool negotiate_info(
    const gdox_nbd_export *exported,
    gdox_socket client,
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
    if (!parse_info_name(payload, payload_length, &name, &name_bytes)) {
        return send_option_reply(
            client,
            option,
            NBD_REP_ERR_INVALID,
            (const uint8_t *)"invalid info request",
            20U
        );
    }
    if (!export_name_matches(exported, name, name_bytes)) {
        return send_option_reply(
            client,
            option,
            NBD_REP_ERR_UNKNOWN,
            (const uint8_t *)"unknown export",
            14U
        );
    }
    result = send_export_info(
        client,
        option,
        gdox_disc_length(&exported->disc)
    ) && send_option_reply(client, option, NBD_REP_ACK, NULL, 0U);
    *ready = result && option == NBD_OPT_GO;
    return result;
}

static bool negotiate(
    gdox_nbd_export *exported,
    gdox_socket client,
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
        if (option == NBD_OPT_ABORT) {
            result = send_option_reply(client, option, NBD_REP_ACK, NULL, 0U);
            free(payload);
            if (result) {
                socket_error_set(GDOX_SOCKET_CONNECTION_ABORTED);
            }
            return false;
        }
        if (option == NBD_OPT_EXPORT_NAME) {
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
        if (option == NBD_OPT_INFO || option == NBD_OPT_GO) {
            result = negotiate_info(
                exported,
                client,
                option,
                payload,
                payload_length,
                &ready
            );
        } else {
            result = send_option_reply(client, option, NBD_REP_ERR_UNSUP, NULL, 0U);
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

typedef struct {
    uint16_t flags;
    uint16_t command;
    uint64_t handle;
    uint64_t offset;
    uint32_t length;
} nbd_request;

static bool parse_request(const uint8_t input[28], nbd_request *request)
{
    if (read_be_u32(input) != NBD_REQUEST_MAGIC) {
        socket_error_set(GDOX_SOCKET_PROTOCOL);
        return false;
    }
    request->flags = read_be_u16(input + 4U);
    request->command = read_be_u16(input + 6U);
    request->handle = read_be_u64(input + 8U);
    request->offset = read_be_u64(input + 16U);
    request->length = read_be_u32(input + 24U);
    return true;
}

static bool reserve_request_buffer(
    uint8_t **buffer,
    size_t *capacity,
    uint32_t length
)
{
    uint8_t *resized;

    if (length <= *capacity || length > NBD_MAX_BUFFER_SIZE) {
        return true;
    }
    resized = realloc(*buffer, length);
    if (resized == NULL && length != 0U) {
        socket_error_set(GDOX_SOCKET_NO_MEMORY);
        return false;
    }
    *buffer = resized;
    *capacity = length;
    return true;
}

static void report_slow_read(
    const nbd_request *request,
    uint64_t started_ms
)
{
    const uint64_t finished_ms = gdox_monotonic_ms();
    const uint64_t elapsed_ms =
        finished_ms >= started_ms ? finished_ms - started_ms : 0U;

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
    gdox_socket client,
    const nbd_request *request,
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
    record_runtime_error(exported, message);
    return send_simple_reply(
        client,
        NBD_EIO,
        request->handle,
        NULL,
        0U
    );
}

static bool transmit_read(
    gdox_nbd_export *exported,
    gdox_socket client,
    const nbd_request *request,
    uint8_t *buffer
)
{
    const uint64_t disc_bytes = gdox_disc_length(&exported->disc);
    const uint64_t started_ms = gdox_monotonic_ms();
    gdox_error read_error;

    if (request->length > NBD_MAX_BUFFER_SIZE
        || request->offset > disc_bytes
        || request->length > disc_bytes - request->offset) {
        return send_simple_reply(
            client,
            NBD_EINVAL,
            request->handle,
            NULL,
            0U
        );
    }
    if (!read_disc_request(
            exported,
            request->offset,
            buffer,
            request->length,
            &read_error
        )) {
        return report_read_failure(exported, client, request, &read_error);
    }
    report_slow_read(request, started_ms);
    return send_simple_reply(
        client,
        0U,
        request->handle,
        buffer,
        request->length
    );
}

static bool transmit_write(
    gdox_socket client,
    const nbd_request *request,
    uint8_t *buffer
)
{
    if (request->length > NBD_MAX_BUFFER_SIZE) {
        socket_error_set(GDOX_SOCKET_PROTOCOL);
        return false;
    }
    if (request->length != 0U
        && !socket_read_exact(client, buffer, request->length)) {
        return false;
    }
    return send_simple_reply(
        client,
        NBD_EPERM,
        request->handle,
        NULL,
        0U
    );
}

static bool transmit(gdox_nbd_export *exported, gdox_socket client)
{
    uint8_t input[28];
    uint8_t *buffer = NULL;
    size_t buffer_capacity = 0U;
    bool result = false;

    for (;;) {
        nbd_request request;

        if (!socket_read_exact(client, input, sizeof(input))) {
            result = socket_error_get() == GDOX_SOCKET_CONNECTION_RESET;
            break;
        }
        if (!parse_request(input, &request)) {
            break;
        }
        if (request.command == NBD_CMD_DISC) {
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
        if (request.command == NBD_CMD_READ && request.flags == 0U) {
            result = transmit_read(exported, client, &request, buffer);
        } else if (request.command == NBD_CMD_WRITE) {
            result = transmit_write(client, &request, buffer);
        } else {
            result = send_simple_reply(
                client,
                NBD_EINVAL,
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

static bool configure_client_socket(gdox_socket client)
{
    const int enabled = 1;
#if defined(_WIN32)
    u_long blocking = 0U;
    const DWORD timeout_ms = 5000U;

    return ioctlsocket(client, (long)FIONBIO, &blocking) == 0
        && setsockopt(
            client,
            IPPROTO_TCP,
            TCP_NODELAY,
            (const char *)&enabled,
            (int)sizeof(enabled)
        ) == 0
        && setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            (const char *)&timeout_ms,
            (int)sizeof(timeout_ms)
        ) == 0
        && setsockopt(
            client,
            SOL_SOCKET,
            SO_SNDTIMEO,
            (const char *)&timeout_ms,
            (int)sizeof(timeout_ms)
        ) == 0;
#else
    struct timeval timeout;
    int flags;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    flags = fcntl(client, F_GETFL, 0);
    if (flags < 0 || fcntl(client, F_SETFL, flags & ~O_NONBLOCK) != 0
        || setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0
        || setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0
        || setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return false;
    }
#if defined(SO_NOSIGPIPE)
    if (setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        return false;
    }
#endif
    return true;
#endif
}

static bool handle_client(
    gdox_nbd_export *exported,
    gdox_socket client,
    bool *negotiated
)
{
    uint8_t greeting[18];
    uint8_t client_flags_bytes[4];
    uint32_t client_flags;
#if defined(_WIN32)
    const DWORD no_timeout = 0U;
#else
    struct timeval no_timeout = {0, 0};
#endif

    *negotiated = false;
    if (!configure_client_socket(client)) {
        return false;
    }
    put_be_u64(greeting, NBD_INIT_MAGIC);
    put_be_u64(greeting + 8U, NBD_OPTS_MAGIC);
    put_be_u16(greeting + 16U, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES);
    if (!socket_write_all(client, greeting, sizeof(greeting))
        || !socket_read_exact(client, client_flags_bytes, sizeof(client_flags_bytes))) {
        return false;
    }
    client_flags = read_be_u32(client_flags_bytes);
    if ((client_flags & NBD_FLAG_C_FIXED_NEWSTYLE) == 0U
        || (client_flags & ~(NBD_FLAG_C_FIXED_NEWSTYLE | NBD_FLAG_C_NO_ZEROES)) != 0U) {
        socket_error_set(GDOX_SOCKET_PROTOCOL);
        return false;
    }
    if (!negotiate(
            exported,
            client,
            (client_flags & NBD_FLAG_C_NO_ZEROES) != 0U
        )) {
        return false;
    }
    *negotiated = true;
    if (setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            (const char *)&no_timeout,
            (int)sizeof(no_timeout)
        ) != 0
        || setsockopt(
            client,
            SOL_SOCKET,
            SO_SNDTIMEO,
            (const char *)&no_timeout,
            (int)sizeof(no_timeout)
        ) != 0) {
        return false;
    }
    return transmit(exported, client);
}

static void record_runtime_error(gdox_nbd_export *exported, const char *message)
{
    if (!gdox_mutex_lock(&exported->state_mutex)) {
        return;
    }
    if (!exported->runtime_failed) {
        exported->runtime_failed = true;
        gdox_error_set(&exported->runtime_error, GDOX_ERROR_TRANSPORT, message);
    }
    gdox_mutex_unlock(&exported->state_mutex);
}

static bool set_active_client(gdox_nbd_export *exported, gdox_socket client)
{
    if (!gdox_mutex_lock(&exported->state_mutex)) {
        return false;
    }
    exported->active = client;
    gdox_mutex_unlock(&exported->state_mutex);
    return true;
}

static void server_thread(void *context)
{
    gdox_nbd_export *exported = context;

    while (!atomic_load_explicit(&exported->stopping, memory_order_acquire)) {
        struct sockaddr_in peer;
        gdox_socklen peer_bytes = (gdox_socklen)sizeof(peer);
        const gdox_socket client = accept(
            exported->listener,
            (struct sockaddr *)&peer,
            &peer_bytes
        );
        if (socket_is_valid(client)) {
            bool negotiated = false;
            bool handled;
            int saved_error;

            if (!set_active_client(exported, client)) {
                socket_close(client);
                continue;
            }
            handled = handle_client(exported, client, &negotiated);
            saved_error = socket_error_get();
            (void)set_active_client(exported, GDOX_INVALID_SOCKET);
            socket_close(client);
            if (!handled
                && !atomic_load_explicit(&exported->stopping, memory_order_acquire)
                && saved_error != GDOX_SOCKET_CONNECTION_RESET
                && saved_error != GDOX_SOCKET_PROTOCOL
                && saved_error != GDOX_SOCKET_PERMISSION
                && saved_error != GDOX_SOCKET_CONNECTION_ABORTED) {
                char message[GDOX_ERROR_MESSAGE_CAPACITY];
                char detail[160];
                (void)snprintf(
                    message,
                    sizeof(message),
                    "private NBD session failed: %s",
                    socket_error_text(saved_error, detail)
                );
                record_runtime_error(exported, message);
            }
        } else if (socket_error_get() == GDOX_SOCKET_INTERRUPTED) {
            continue;
        } else if (!atomic_load_explicit(&exported->stopping, memory_order_acquire)) {
            char message[GDOX_ERROR_MESSAGE_CAPACITY];
            char detail[160];
            const int saved_error = socket_error_get();
            (void)snprintf(
                message,
                sizeof(message),
                "private NBD listener failed: %s",
                socket_error_text(saved_error, detail)
            );
            record_runtime_error(exported, message);
            break;
        }
    }
}

static bool random_token(uint8_t output[NBD_TOKEN_BYTES], gdox_error *error)
{
#if defined(_WIN32)
    if (BCryptGenRandom(
            NULL,
            output,
            NBD_TOKEN_BYTES,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        ) < 0) {
        gdox_error_set(error, GDOX_ERROR_IO, "could not create private NBD token");
        return false;
    }
#elif defined(__linux__)
    size_t completed = 0U;
    while (completed < NBD_TOKEN_BYTES) {
        const ssize_t received = getrandom(
            output + completed,
            NBD_TOKEN_BYTES - completed,
            0U
        );
        if (received > 0) {
            completed += (size_t)received;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            gdox_error_set(error, GDOX_ERROR_IO, "could not create private NBD token");
            return false;
        }
    }
#else
    {
        int random_file = open("/dev/urandom", O_RDONLY);
        size_t completed = 0U;
        if (random_file < 0) {
            gdox_error_set(error, GDOX_ERROR_IO, "could not open the operating-system random source");
            return false;
        }
        while (completed < NBD_TOKEN_BYTES) {
            const ssize_t received = read(
                random_file,
                output + completed,
                NBD_TOKEN_BYTES - completed
            );
            if (received > 0) {
                completed += (size_t)received;
            } else if (received < 0 && errno == EINTR) {
                continue;
            } else {
                (void)close(random_file);
                gdox_error_set(error, GDOX_ERROR_IO, "could not create private NBD token");
                return false;
            }
        }
        (void)close(random_file);
    }
#endif
    return true;
}

static void encode_hex(
    const uint8_t input[NBD_TOKEN_BYTES],
    char output[NBD_TOKEN_BYTES * 2U + 1U]
)
{
    static const char alphabet[] = "0123456789abcdef";
    size_t index;
    for (index = 0U; index < NBD_TOKEN_BYTES; ++index) {
        output[index * 2U] = alphabet[input[index] >> 4U];
        output[index * 2U + 1U] = alphabet[input[index] & 0x0fU];
    }
    output[(size_t)NBD_TOKEN_BYTES * 2U] = '\0';
}

bool gdox_nbd_start(
    gdox_random_disc *disc,
    gdox_nbd_export **exported_output,
    gdox_error *error
)
{
    gdox_nbd_export *exported;
    struct sockaddr_in address;
    gdox_socklen address_bytes = (gdox_socklen)sizeof(address);
    uint8_t token[NBD_TOKEN_BYTES];
#if defined(_WIN32)
    WSADATA winsock;
#endif

    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc) || exported_output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "disc and export output are required");
        return false;
    }
    exported = calloc(1U, sizeof(*exported));
    if (exported == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate NBD export");
        return false;
    }
    exported->listener = GDOX_INVALID_SOCKET;
    exported->active = GDOX_INVALID_SOCKET;
#if defined(_WIN32)
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        free(exported);
        gdox_error_set(error, GDOX_ERROR_IO, "could not initialize Windows sockets");
        return false;
    }
    exported->winsock_started = true;
#endif
    if (!gdox_mutex_init(&exported->state_mutex)) {
#if defined(_WIN32)
        (void)WSACleanup();
#endif
        free(exported);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize NBD state");
        return false;
    }
    if (!random_token(token, error)) {
        gdox_mutex_destroy(&exported->state_mutex);
#if defined(_WIN32)
        (void)WSACleanup();
#endif
        free(exported);
        return false;
    }
    encode_hex(token, exported->export_name);
    exported->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (!socket_is_valid(exported->listener)) {
        gdox_mutex_destroy(&exported->state_mutex);
#if defined(_WIN32)
        (void)WSACleanup();
#endif
        free(exported);
        gdox_error_set(error, GDOX_ERROR_IO, "could not create private NBD listener");
        return false;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
    address.sin_port = 0U;
    if (bind(
            exported->listener,
            (const struct sockaddr *)&address,
            (gdox_socklen)sizeof(address)
        ) != 0
        || listen(exported->listener, 4) != 0
        || getsockname(
            exported->listener,
            (struct sockaddr *)&address,
            &address_bytes
        ) != 0) {
        socket_close(exported->listener);
        gdox_mutex_destroy(&exported->state_mutex);
#if defined(_WIN32)
        (void)WSACleanup();
#endif
        free(exported);
        gdox_error_set(error, GDOX_ERROR_IO, "could not bind private NBD listener");
        return false;
    }
    exported->port = ntohs(address.sin_port);
    (void)snprintf(
        exported->uri,
        sizeof(exported->uri),
        "nbd://127.0.0.1:%u/%s",
        (unsigned int)exported->port,
        exported->export_name
    );
    (void)snprintf(
        exported->display_uri,
        sizeof(exported->display_uri),
        "nbd://127.0.0.1:%u/<private-session>",
        (unsigned int)exported->port
    );
    exported->disc = *disc;
    atomic_init(&exported->stopping, false);
    if (!gdox_thread_start(&exported->thread, server_thread, exported)) {
        socket_close(exported->listener);
        gdox_mutex_destroy(&exported->state_mutex);
#if defined(_WIN32)
        (void)WSACleanup();
#endif
        free(exported);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not start private NBD thread");
        return false;
    }
    exported->thread_started = true;
    disc->context = NULL;
    disc->ops = NULL;
    *exported_output = exported;
    return true;
}

const char *gdox_nbd_uri(const gdox_nbd_export *exported)
{
    return exported != NULL ? exported->uri : NULL;
}

const char *gdox_nbd_display_uri(const gdox_nbd_export *exported)
{
    return exported != NULL ? exported->display_uri : NULL;
}

bool gdox_nbd_media_present(const gdox_nbd_export *exported)
{
    return exported != NULL && gdox_disc_media_present(&exported->disc);
}

bool gdox_nbd_physical_read_stats(
    const gdox_nbd_export *exported,
    gdox_physical_read_stats *output
)
{
    return exported != NULL
        && gdox_disc_physical_read_stats(&exported->disc, output);
}

bool gdox_nbd_runtime_error(const gdox_nbd_export *exported, gdox_error *error)
{
    gdox_nbd_export *mutable_export = (gdox_nbd_export *)exported;
    bool failed;

    gdox_error_clear(error);
    if (exported == NULL || !gdox_mutex_lock(&mutable_export->state_mutex)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "NBD export is not available");
        return true;
    }
    failed = exported->runtime_failed;
    if (failed) {
        *error = exported->runtime_error;
    }
    gdox_mutex_unlock(&mutable_export->state_mutex);
    return failed;
}

bool gdox_nbd_close(gdox_nbd_export *exported, gdox_error *error)
{
    gdox_error runtime_error;
    gdox_error disc_error;
    bool runtime_failed;
    bool disc_closed;
    gdox_socket wake = GDOX_INVALID_SOCKET;
    struct sockaddr_in address;

    gdox_error_clear(error);
    if (exported == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "NBD export is not available");
        return false;
    }
    atomic_store_explicit(&exported->stopping, true, memory_order_release);
    gdox_disc_abort(&exported->disc);
    if (gdox_mutex_lock(&exported->state_mutex)) {
        if (socket_is_valid(exported->active)) {
#if defined(_WIN32)
            (void)shutdown(exported->active, SD_BOTH);
#else
            (void)shutdown(exported->active, SHUT_RDWR);
#endif
        }
        gdox_mutex_unlock(&exported->state_mutex);
    }
    wake = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_is_valid(wake)) {
        int wake_result;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
        address.sin_port = htons(exported->port);
        wake_result = connect(
            wake,
            (const struct sockaddr *)&address,
            (gdox_socklen)sizeof(address)
        );
        (void)wake_result;
        socket_close(wake);
    }
    if (exported->thread_started) {
        (void)gdox_thread_join(&exported->thread);
    }
    socket_close(exported->listener);
    runtime_failed = gdox_nbd_runtime_error(exported, &runtime_error);
    disc_closed = gdox_disc_close(&exported->disc, &disc_error);
    gdox_mutex_destroy(&exported->state_mutex);
#if defined(_WIN32)
    if (exported->winsock_started) {
        (void)WSACleanup();
    }
#endif
    free(exported);
    if (runtime_failed) {
        *error = runtime_error;
        return false;
    }
    if (!disc_closed) {
        *error = disc_error;
        return false;
    }
    return true;
}
