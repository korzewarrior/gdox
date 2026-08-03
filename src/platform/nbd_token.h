#ifndef GDOX_NBD_TOKEN_H
#define GDOX_NBD_TOKEN_H

#include "gdox/error.h"

#include <stdbool.h>

#define GDOX_NBD_TOKEN_BYTES 16U
#define GDOX_NBD_TOKEN_TEXT_BYTES (GDOX_NBD_TOKEN_BYTES * 2U + 1U)

bool gdox_nbd_create_token(
    char output[GDOX_NBD_TOKEN_TEXT_BYTES],
    gdox_error *error
);

#endif
