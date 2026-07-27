#ifndef GDOX_PROTOCOL_H
#define GDOX_PROTOCOL_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_LOGICAL_SECTOR_BYTES 2048U
#define GDOX_RAW_DVD_FRAME_BYTES 2064U
#define GDOX_RAW_DVD_MAIN_OFFSET 12U
#define GDOX_C0_CDB_BYTES 12U
#define GDOX_RECOMMENDED_TRANSFER_BLOCKS 31U

typedef struct gdox_read_disc_raw_request {
    uint32_t address;
    uint32_t blocks;
    bool raw_addressing;
    bool force_unit_access;
    bool descramble;
} gdox_read_disc_raw_request;

bool gdox_protocol_build_c0(
    const gdox_read_disc_raw_request *request,
    uint8_t cdb[GDOX_C0_CDB_BYTES],
    gdox_error *error
);

bool gdox_protocol_extract_main_data(
    const uint8_t *raw,
    size_t raw_bytes,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
