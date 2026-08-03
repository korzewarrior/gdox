#ifndef GDOX_NBD_PROTOCOL_H
#define GDOX_NBD_PROTOCOL_H

#include "platform/nbd_socket.h"

#include <stdbool.h>

typedef struct gdox_nbd_export gdox_nbd_export;

bool gdox_nbd_protocol_handle(
    gdox_nbd_export *exported,
    gdox_nbd_socket client
);

#endif
