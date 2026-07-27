#include "test.h"

#include "gdox/disc.h"
#include "gdox/nbd.h"
#include "gdox/source.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET test_socket;
typedef int test_socket_count;
#define GDOX_TEST_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int test_socket;
typedef ssize_t test_socket_count;
#define GDOX_TEST_INVALID_SOCKET (-1)
#endif

typedef struct nbd_memory_source {
    uint64_t sectors;
    uint32_t read_calls;
    uint32_t failures_remaining;
} nbd_memory_source;

static uint64_t nbd_source_count(const void *context)
{
    const nbd_memory_source *source = context;
    return source->sectors;
}

static bool nbd_source_read(
    void *context,
    uint64_t lba,
    uint32_t blocks,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    nbd_memory_source *source = context;
    uint32_t index;
    ++source->read_calls;
    if (source->failures_remaining != 0U) {
        --source->failures_remaining;
        gdox_error_set(error, GDOX_ERROR_IO, "simulated transient read failure");
        return false;
    }
    if (!gdox_source_validate_read(
            source->sectors,
            lba,
            blocks,
            output_bytes,
            error
        )) {
        return false;
    }
    for (index = 0U; index < blocks; ++index) {
        memset(
            output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES,
            (int)(uint8_t)(lba + index),
            GDOX_LOGICAL_SECTOR_BYTES
        );
    }
    return true;
}

static bool nbd_source_present(const void *context)
{
    (void)context;
    return true;
}

static bool nbd_source_close(void *context, gdox_error *error)
{
    gdox_error_clear(error);
    free(context);
    return true;
}

static const gdox_sector_source_ops nbd_source_ops = {
    nbd_source_count,
    nbd_source_read,
    nbd_source_present,
    nbd_source_close,
    NULL,
    NULL,
    NULL,
};

static bool read_exact(test_socket socket_fd, uint8_t *output, size_t bytes)
{
    size_t completed = 0U;
    while (completed < bytes) {
#if defined(_WIN32)
        const test_socket_count received = recv(
            socket_fd,
            (char *)output + completed,
            (int)(bytes - completed),
            0
        );
#else
        const test_socket_count received = recv(
            socket_fd,
            output + completed,
            bytes - completed,
            0
        );
#endif
        if (received <= 0) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool write_all(
    test_socket socket_fd,
    const uint8_t *input,
    size_t bytes
)
{
    size_t completed = 0U;
    while (completed < bytes) {
#if defined(_WIN32)
        const test_socket_count sent = send(
            socket_fd,
            (const char *)input + completed,
            (int)(bytes - completed),
            0
        );
#else
        const test_socket_count sent = send(
            socket_fd,
            input + completed,
            bytes - completed,
            0
        );
#endif
        if (sent <= 0) {
            return false;
        }
        completed += (size_t)sent;
    }
    return true;
}

static uint32_t be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | input[3];
}

static uint64_t be_u64(const uint8_t *input)
{
    return (uint64_t)be_u32(input) << 32U | be_u32(input + 4U);
}

static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static void put_u64(uint8_t *output, uint64_t value)
{
    put_u32(output, (uint32_t)(value >> 32U));
    put_u32(output + 4U, (uint32_t)value);
}

static uint16_t uri_port(const char *uri)
{
    const char *colon = strrchr(uri, ':');
    return colon != NULL ? (uint16_t)strtoul(colon + 1, NULL, 10) : 0U;
}

static const char *uri_name(const char *uri)
{
    const char *slash = strrchr(uri, '/');
    return slash != NULL ? slash + 1U : "";
}

static void test_read_only_export(void)
{
    nbd_memory_source *memory = calloc(1U, sizeof(*memory));
    gdox_sector_source source = {0};
    gdox_random_disc disc = {0};
    gdox_nbd_export *exported = NULL;
    gdox_error error;
    struct sockaddr_in address;
    test_socket client;
    uint8_t greeting[18];
    uint8_t flags[4];
    const char *name;
    size_t name_bytes;
    uint8_t option[16 + 4 + 32 + 2];
    size_t option_bytes;
    bool acknowledged = false;
    bool received_reply;
    uint8_t reply[20];
    uint8_t request[28];
    uint8_t simple[16];
    uint8_t data[16];

    GDOX_TEST_CHECK(memory != NULL);
    memory->sectors = 8U;
    memory->failures_remaining = 1U;
    source.context = memory;
    source.ops = &nbd_source_ops;
    GDOX_TEST_CHECK(gdox_disc_from_source(&source, &disc, &error));
    GDOX_TEST_CHECK(gdox_nbd_start(&disc, &exported, &error));
    GDOX_TEST_CHECK(strstr(gdox_nbd_display_uri(exported), "<private-session>") != NULL);
    GDOX_TEST_CHECK(
        strstr(gdox_nbd_display_uri(exported), uri_name(gdox_nbd_uri(exported))) == NULL
    );

    client = socket(AF_INET, SOCK_STREAM, 0);
    GDOX_TEST_CHECK(client != GDOX_TEST_INVALID_SOCKET);
#if defined(SO_NOSIGPIPE)
    {
        const int enabled = 1;
        GDOX_TEST_CHECK(
            setsockopt(
                client,
                SOL_SOCKET,
                SO_NOSIGPIPE,
                &enabled,
                sizeof(enabled)
            ) == 0
        );
    }
#endif
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
    address.sin_port = htons(uri_port(gdox_nbd_uri(exported)));
    GDOX_TEST_CHECK(
        connect(
            client,
            (const struct sockaddr *)&address,
            (int)sizeof(address)
        ) == 0
    );
    GDOX_TEST_CHECK(read_exact(client, greeting, sizeof(greeting)));
    GDOX_TEST_CHECK(be_u64(greeting) == UINT64_C(0x4e42444d41474943));
    GDOX_TEST_CHECK(be_u64(greeting + 8U) == UINT64_C(0x49484156454f5054));
    GDOX_TEST_CHECK(greeting[16] == 0U && greeting[17] == 3U);
    put_u32(flags, 3U);
    GDOX_TEST_CHECK(write_all(client, flags, sizeof(flags)));

    name = uri_name(gdox_nbd_uri(exported));
    name_bytes = strlen(name);
    put_u64(option, UINT64_C(0x49484156454f5054));
    put_u32(option + 8U, 7U);
    put_u32(option + 12U, (uint32_t)(4U + name_bytes + 2U));
    put_u32(option + 16U, (uint32_t)name_bytes);
    memcpy(option + 20U, name, name_bytes);
    put_u16(option + 20U + name_bytes, 0U);
    option_bytes = 22U + name_bytes;
    GDOX_TEST_CHECK(write_all(client, option, option_bytes));
    while (!acknowledged) {
        uint32_t payload_bytes;
        uint8_t payload[64];
        received_reply = read_exact(client, reply, sizeof(reply));
        if (!received_reply) {
            gdox_error runtime_error;
            if (gdox_nbd_runtime_error(exported, &runtime_error)) {
                (void)fprintf(
                    stderr,
                    "NBD test receive failed: errno=%d runtime=%s\n",
#if defined(_WIN32)
                    WSAGetLastError(),
#else
                    errno,
#endif
                    runtime_error.message
                );
            } else {
                (void)fprintf(
                    stderr,
                    "NBD test receive failed: socket_error=%d\n",
#if defined(_WIN32)
                    WSAGetLastError()
#else
                    errno
#endif
                );
            }
        }
        GDOX_TEST_CHECK(received_reply);
        GDOX_TEST_CHECK(be_u64(reply) == UINT64_C(0x0003e889045565a9));
        GDOX_TEST_CHECK(be_u32(reply + 8U) == 7U);
        payload_bytes = be_u32(reply + 16U);
        GDOX_TEST_CHECK(payload_bytes <= sizeof(payload));
        GDOX_TEST_CHECK(read_exact(client, payload, payload_bytes));
        acknowledged = be_u32(reply + 12U) == 1U;
    }

    put_u32(request, UINT32_C(0x25609513));
    put_u16(request + 4U, 0U);
    put_u16(request + 6U, 0U);
    put_u64(request + 8U, UINT64_C(0x12345678));
    put_u64(request + 16U, GDOX_LOGICAL_SECTOR_BYTES - 4U);
    put_u32(request + 24U, sizeof(data));
    GDOX_TEST_CHECK(write_all(client, request, sizeof(request)));
    GDOX_TEST_CHECK(read_exact(client, simple, sizeof(simple)));
    GDOX_TEST_CHECK(be_u32(simple) == UINT32_C(0x67446698));
    GDOX_TEST_CHECK(be_u32(simple + 4U) == 0U);
    GDOX_TEST_CHECK(be_u64(simple + 8U) == UINT64_C(0x12345678));
    GDOX_TEST_CHECK(read_exact(client, data, sizeof(data)));
    GDOX_TEST_CHECK(memory->read_calls == 2U);
    GDOX_TEST_CHECK(data[0] == 0U && data[3] == 0U);
    GDOX_TEST_CHECK(data[4] == 1U && data[15] == 1U);

    memory->failures_remaining = 2U;
    put_u64(request + 8U, UINT64_C(0x87654321));
    put_u64(request + 16U, 2U * GDOX_LOGICAL_SECTOR_BYTES);
    put_u32(request + 24U, sizeof(data));
    GDOX_TEST_CHECK(write_all(client, request, sizeof(request)));
    GDOX_TEST_CHECK(read_exact(client, simple, sizeof(simple)));
    GDOX_TEST_CHECK(be_u32(simple) == UINT32_C(0x67446698));
    GDOX_TEST_CHECK(be_u32(simple + 4U) == 5U);
    GDOX_TEST_CHECK(be_u64(simple + 8U) == UINT64_C(0x87654321));
    GDOX_TEST_CHECK(gdox_nbd_runtime_error(exported, &error));
    GDOX_TEST_CHECK(strstr(error.message, "sector 2") != NULL);
    GDOX_TEST_CHECK(strstr(error.message, "simulated transient read failure") != NULL);

    put_u16(request + 6U, 2U);
    put_u64(request + 8U, 0U);
    put_u64(request + 16U, 0U);
    put_u32(request + 24U, 0U);
    GDOX_TEST_CHECK(write_all(client, request, sizeof(request)));
#if defined(_WIN32)
    GDOX_TEST_CHECK(closesocket(client) == 0);
#else
    GDOX_TEST_CHECK(close(client) == 0);
#endif
    GDOX_TEST_CHECK(!gdox_nbd_close(exported, &error));
    GDOX_TEST_CHECK(strstr(error.message, "sector 2") != NULL);
}

void gdox_test_nbd(void)
{
    test_read_only_export();
}
