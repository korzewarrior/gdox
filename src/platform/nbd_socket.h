#ifndef GDOX_NBD_SOCKET_H
#define GDOX_NBD_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
typedef SOCKET gdox_nbd_socket;
#define GDOX_NBD_INVALID_SOCKET INVALID_SOCKET
#else
typedef int gdox_nbd_socket;
#define GDOX_NBD_INVALID_SOCKET (-1)
#endif

bool gdox_nbd_socket_platform_start(void);
void gdox_nbd_socket_platform_stop(void);

gdox_nbd_socket gdox_nbd_socket_create(void);
bool gdox_nbd_socket_bind_loopback_listener(
    gdox_nbd_socket listener,
    uint16_t *port
);
gdox_nbd_socket gdox_nbd_socket_accept(gdox_nbd_socket listener);
void gdox_nbd_socket_wake_loopback(uint16_t port);
void gdox_nbd_socket_shutdown(gdox_nbd_socket socket_handle);
void gdox_nbd_socket_close(gdox_nbd_socket socket_handle);
bool gdox_nbd_socket_is_valid(gdox_nbd_socket socket_handle);

bool gdox_nbd_socket_configure_client(gdox_nbd_socket client);
bool gdox_nbd_socket_clear_timeouts(gdox_nbd_socket client);
bool gdox_nbd_socket_read_exact(
    gdox_nbd_socket socket_handle,
    uint8_t *output,
    size_t bytes
);
bool gdox_nbd_socket_write_all(
    gdox_nbd_socket socket_handle,
    const uint8_t *input,
    size_t bytes
);

int gdox_nbd_socket_error(void);
const char *gdox_nbd_socket_error_text(int code, char output[160]);
void gdox_nbd_socket_set_protocol_error(void);
void gdox_nbd_socket_set_permission_error(void);
void gdox_nbd_socket_set_no_memory_error(void);
void gdox_nbd_socket_set_connection_aborted(void);
bool gdox_nbd_socket_error_is_interrupted(int code);
bool gdox_nbd_socket_error_is_connection_reset(int code);
bool gdox_nbd_socket_error_is_protocol(int code);
bool gdox_nbd_socket_error_is_permission(int code);
bool gdox_nbd_socket_error_is_connection_aborted(int code);

#endif
