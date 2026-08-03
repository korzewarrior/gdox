#ifndef GDOX_NBD_INTERNAL_H
#define GDOX_NBD_INTERNAL_H

#include "gdox/nbd.h"

#include "platform/nbd_socket.h"
#include "platform/nbd_telemetry.h"
#include "platform/nbd_token.h"
#include "platform/portable_sync.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct gdox_nbd_export {
    gdox_nbd_socket listener;
    gdox_nbd_socket active;
    uint16_t port;
    char export_name[GDOX_NBD_TOKEN_TEXT_BYTES];
    char uri[96];
    char display_uri[96];
    uint16_t export_flags;
    gdox_random_disc disc;
    gdox_thread thread;
    gdox_mutex state_mutex;
    atomic_bool stopping;
    bool thread_started;
    bool socket_platform_started;
    bool runtime_failed;
    gdox_error runtime_error;
    gdox_nbd_telemetry telemetry;
};

void gdox_nbd_export_record_runtime_error(
    gdox_nbd_export *exported,
    const char *message
);

#endif
