#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/nbd_socket.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <ws2tcpip.h>
#include <windows.h>

typedef int gdox_nbd_socket_io_count;

#define GDOX_NBD_CONNECTION_ABORTED WSAECONNABORTED
#define GDOX_NBD_CONNECTION_RESET WSAECONNRESET
#define GDOX_NBD_INTERRUPTED WSAEINTR
#define GDOX_NBD_NO_MEMORY WSAENOBUFS
#define GDOX_NBD_PERMISSION WSAEACCES
#define GDOX_NBD_PROTOCOL WSAEPROTONOSUPPORT

static void set_socket_error(int code)
{
    WSASetLastError(code);
}

static gdox_nbd_socket_io_count receive_bytes(
    gdox_nbd_socket socket_handle,
    uint8_t *output,
    size_t bytes
)
{
    return recv(socket_handle, (char *)output, (int)bytes, 0);
}

static gdox_nbd_socket_io_count send_bytes(
    gdox_nbd_socket socket_handle,
    const uint8_t *input,
    size_t bytes,
    int flags
)
{
    return send(socket_handle, (const char *)input, (int)bytes, flags);
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

typedef ssize_t gdox_nbd_socket_io_count;

#define GDOX_NBD_CONNECTION_ABORTED ECONNABORTED
#define GDOX_NBD_CONNECTION_RESET ECONNRESET
#define GDOX_NBD_INTERRUPTED EINTR
#define GDOX_NBD_NO_MEMORY ENOMEM
#define GDOX_NBD_PERMISSION EACCES
#define GDOX_NBD_PROTOCOL EPROTO

static void set_socket_error(int code)
{
    errno = code;
}

static gdox_nbd_socket_io_count receive_bytes(
    gdox_nbd_socket socket_handle,
    uint8_t *output,
    size_t bytes
)
{
    return recv(socket_handle, output, bytes, 0);
}

static gdox_nbd_socket_io_count send_bytes(
    gdox_nbd_socket socket_handle,
    const uint8_t *input,
    size_t bytes,
    int flags
)
{
    return send(socket_handle, input, bytes, flags);
}

#endif

bool gdox_nbd_socket_platform_start(void)
{
#if defined(_WIN32)
    WSADATA winsock;

    return WSAStartup(MAKEWORD(2, 2), &winsock) == 0;
#else
    return true;
#endif
}

void gdox_nbd_socket_platform_stop(void)
{
#if defined(_WIN32)
    (void)WSACleanup();
#endif
}

gdox_nbd_socket gdox_nbd_socket_create(void)
{
    return socket(AF_INET, SOCK_STREAM, 0);
}

bool gdox_nbd_socket_bind_loopback_listener(
    gdox_nbd_socket listener,
    uint16_t *port
)
{
    struct sockaddr_in address;
#if defined(_WIN32)
    int address_bytes = (int)sizeof(address);
#else
    socklen_t address_bytes = (socklen_t)sizeof(address);
#endif

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
    address.sin_port = 0U;
    if (bind(
            listener,
            (const struct sockaddr *)&address,
            (int)sizeof(address)
        ) != 0
        || listen(listener, 4) != 0
        || getsockname(
            listener,
            (struct sockaddr *)&address,
            &address_bytes
        ) != 0) {
        return false;
    }
    *port = ntohs(address.sin_port);
    return true;
}

gdox_nbd_socket gdox_nbd_socket_accept(gdox_nbd_socket listener)
{
    return accept(listener, NULL, NULL);
}

void gdox_nbd_socket_wake_loopback(uint16_t port)
{
    gdox_nbd_socket wake = gdox_nbd_socket_create();

    if (gdox_nbd_socket_is_valid(wake)) {
        struct sockaddr_in address;
        int wake_result;

        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
        address.sin_port = htons(port);
        wake_result = connect(
            wake,
            (const struct sockaddr *)&address,
            (int)sizeof(address)
        );
        (void)wake_result;
        gdox_nbd_socket_close(wake);
    }
}

void gdox_nbd_socket_shutdown(gdox_nbd_socket socket_handle)
{
#if defined(_WIN32)
    (void)shutdown(socket_handle, SD_BOTH);
#else
    (void)shutdown(socket_handle, SHUT_RDWR);
#endif
}

void gdox_nbd_socket_close(gdox_nbd_socket socket_handle)
{
    if (!gdox_nbd_socket_is_valid(socket_handle)) {
        return;
    }
#if defined(_WIN32)
    (void)closesocket(socket_handle);
#else
    (void)close(socket_handle);
#endif
}

bool gdox_nbd_socket_is_valid(gdox_nbd_socket socket_handle)
{
#if defined(_WIN32)
    return socket_handle != GDOX_NBD_INVALID_SOCKET;
#else
    return socket_handle >= 0;
#endif
}

bool gdox_nbd_socket_configure_client(gdox_nbd_socket client)
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
        || setsockopt(
            client,
            IPPROTO_TCP,
            TCP_NODELAY,
            &enabled,
            sizeof(enabled)
        ) != 0
        || setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0
        || setsockopt(
            client,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0) {
        return false;
    }
#if defined(SO_NOSIGPIPE)
    if (setsockopt(
            client,
            SOL_SOCKET,
            SO_NOSIGPIPE,
            &enabled,
            sizeof(enabled)
        ) != 0) {
        return false;
    }
#endif
    return true;
#endif
}

bool gdox_nbd_socket_clear_timeouts(gdox_nbd_socket client)
{
#if defined(_WIN32)
    const DWORD no_timeout = 0U;
#else
    const struct timeval no_timeout = {0, 0};
#endif

    return setsockopt(
        client,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (const char *)&no_timeout,
        (int)sizeof(no_timeout)
    ) == 0
        && setsockopt(
            client,
            SOL_SOCKET,
            SO_SNDTIMEO,
            (const char *)&no_timeout,
            (int)sizeof(no_timeout)
        ) == 0;
}

bool gdox_nbd_socket_read_exact(
    gdox_nbd_socket socket_handle,
    uint8_t *output,
    size_t bytes
)
{
    size_t completed = 0U;

    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const size_t requested = remaining > (size_t)INT_MAX
            ? (size_t)INT_MAX
            : remaining;
        const gdox_nbd_socket_io_count received = receive_bytes(
            socket_handle,
            output + completed,
            requested
        );

        if (received > 0) {
            completed += (size_t)received;
        } else if (received == 0) {
            set_socket_error(GDOX_NBD_CONNECTION_RESET);
            return false;
        } else if (gdox_nbd_socket_error() != GDOX_NBD_INTERRUPTED) {
            return false;
        }
    }
    return true;
}

bool gdox_nbd_socket_write_all(
    gdox_nbd_socket socket_handle,
    const uint8_t *input,
    size_t bytes
)
{
    size_t completed = 0U;

    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const size_t requested = remaining > (size_t)INT_MAX
            ? (size_t)INT_MAX
            : remaining;
#if defined(MSG_NOSIGNAL)
        const gdox_nbd_socket_io_count sent = send_bytes(
            socket_handle,
            input + completed,
            requested,
            MSG_NOSIGNAL
        );
#else
        const gdox_nbd_socket_io_count sent = send_bytes(
            socket_handle,
            input + completed,
            requested,
            0
        );
#endif

        if (sent > 0) {
            completed += (size_t)sent;
        } else if (sent == 0) {
            set_socket_error(GDOX_NBD_CONNECTION_RESET);
            return false;
        } else if (gdox_nbd_socket_error() != GDOX_NBD_INTERRUPTED) {
            return false;
        }
    }
    return true;
}

int gdox_nbd_socket_error(void)
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

const char *gdox_nbd_socket_error_text(int code, char output[160])
{
#if defined(_WIN32)
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
#else
    (void)snprintf(output, 160U, "%s", strerror(code));
#endif
    return output;
}

void gdox_nbd_socket_set_protocol_error(void)
{
    set_socket_error(GDOX_NBD_PROTOCOL);
}

void gdox_nbd_socket_set_permission_error(void)
{
    set_socket_error(GDOX_NBD_PERMISSION);
}

void gdox_nbd_socket_set_no_memory_error(void)
{
    set_socket_error(GDOX_NBD_NO_MEMORY);
}

void gdox_nbd_socket_set_connection_aborted(void)
{
    set_socket_error(GDOX_NBD_CONNECTION_ABORTED);
}

bool gdox_nbd_socket_error_is_interrupted(int code)
{
    return code == GDOX_NBD_INTERRUPTED;
}

bool gdox_nbd_socket_error_is_connection_reset(int code)
{
    return code == GDOX_NBD_CONNECTION_RESET;
}

bool gdox_nbd_socket_error_is_protocol(int code)
{
    return code == GDOX_NBD_PROTOCOL;
}

bool gdox_nbd_socket_error_is_permission(int code)
{
    return code == GDOX_NBD_PERMISSION;
}

bool gdox_nbd_socket_error_is_connection_aborted(int code)
{
    return code == GDOX_NBD_CONNECTION_ABORTED;
}
