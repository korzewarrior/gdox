#include "gdox/protocol.h"

#include <string.h>

bool gdox_protocol_build_c0(
    const gdox_read_disc_raw_request *request,
    uint8_t cdb[GDOX_C0_CDB_BYTES],
    gdox_error *error
)
{
    uint8_t flags = 0U;

    gdox_error_clear(error);
    if (request == NULL || cdb == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "C0 request and output are required");
        return false;
    }
    if (request->blocks == 0U) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "C0 transfer must contain at least one block");
        return false;
    }

    if (request->raw_addressing) {
        flags |= UINT8_C(0x04);
    }
    if (request->force_unit_access) {
        flags |= UINT8_C(0x08);
    }
    if (request->descramble) {
        flags |= UINT8_C(0x10);
    }
    flags |= UINT8_C(0x01);

    memset(cdb, 0, GDOX_C0_CDB_BYTES);
    cdb[0] = UINT8_C(0xc0);
    cdb[1] = flags;
    cdb[2] = (uint8_t)(request->address >> 24U);
    cdb[3] = (uint8_t)(request->address >> 16U);
    cdb[4] = (uint8_t)(request->address >> 8U);
    cdb[5] = (uint8_t)request->address;
    cdb[6] = (uint8_t)(request->blocks >> 24U);
    cdb[7] = (uint8_t)(request->blocks >> 16U);
    cdb[8] = (uint8_t)(request->blocks >> 8U);
    cdb[9] = (uint8_t)request->blocks;
    return true;
}

bool gdox_protocol_extract_main_data(
    const uint8_t *raw,
    size_t raw_bytes,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    size_t blocks = 0U;

    gdox_error_clear(error);
    if (raw == NULL || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "raw and output buffers are required");
        return false;
    }
    if (raw_bytes == 0U || raw_bytes % GDOX_RAW_DVD_FRAME_BYTES != 0U) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "raw DVD response contains a partial frame");
        return false;
    }
    blocks = raw_bytes / GDOX_RAW_DVD_FRAME_BYTES;
    if (output_bytes != blocks * GDOX_LOGICAL_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "main-data output has the wrong size");
        return false;
    }

    for (size_t index = 0U; index < blocks; ++index) {
        const uint8_t *frame = raw + index * GDOX_RAW_DVD_FRAME_BYTES;
        uint8_t *sector = output + index * GDOX_LOGICAL_SECTOR_BYTES;
        memcpy(sector, frame + GDOX_RAW_DVD_MAIN_OFFSET, GDOX_LOGICAL_SECTOR_BYTES);
    }
    return true;
}
