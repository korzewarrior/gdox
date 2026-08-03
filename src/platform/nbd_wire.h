#ifndef GDOX_NBD_WIRE_H
#define GDOX_NBD_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_NBD_GREETING_BYTES 18U
#define GDOX_NBD_OPTION_HEADER_BYTES 16U
#define GDOX_NBD_OPTION_REPLY_HEADER_BYTES 20U
#define GDOX_NBD_SIMPLE_REPLY_BYTES 16U
#define GDOX_NBD_REQUEST_BYTES 28U
#define GDOX_NBD_EXPORT_RESPONSE_BYTES 10U
#define GDOX_NBD_BLOCK_SIZE_INFO_BYTES 14U
#define GDOX_NBD_EXPORT_INFO_BYTES 12U

#define GDOX_NBD_MAX_BUFFER_SIZE (32U * 1024U * 1024U)
#define GDOX_NBD_MAX_OPTION_SIZE (64U * 1024U)

#define GDOX_NBD_FLAG_C_FIXED_NEWSTYLE UINT32_C(0x00000001)
#define GDOX_NBD_FLAG_C_NO_ZEROES UINT32_C(0x00000002)

#define GDOX_NBD_OPT_EXPORT_NAME UINT32_C(1)
#define GDOX_NBD_OPT_ABORT UINT32_C(2)
#define GDOX_NBD_OPT_INFO UINT32_C(6)
#define GDOX_NBD_OPT_GO UINT32_C(7)

#define GDOX_NBD_REP_ACK UINT32_C(1)
#define GDOX_NBD_REP_INFO UINT32_C(3)
#define GDOX_NBD_REP_ERR_UNSUP (UINT32_C(0x80000000) | UINT32_C(1))
#define GDOX_NBD_REP_ERR_INVALID (UINT32_C(0x80000000) | UINT32_C(3))
#define GDOX_NBD_REP_ERR_UNKNOWN (UINT32_C(0x80000000) | UINT32_C(6))

#define GDOX_NBD_CMD_READ UINT16_C(0)
#define GDOX_NBD_CMD_WRITE UINT16_C(1)
#define GDOX_NBD_CMD_DISC UINT16_C(2)

#define GDOX_NBD_EPERM UINT32_C(1)
#define GDOX_NBD_EIO UINT32_C(5)
#define GDOX_NBD_EINVAL UINT32_C(22)

typedef struct gdox_nbd_request {
    uint16_t flags;
    uint16_t command;
    uint64_t handle;
    uint64_t offset;
    uint32_t length;
} gdox_nbd_request;

void gdox_nbd_wire_greeting(
    uint8_t output[GDOX_NBD_GREETING_BYTES]
);
bool gdox_nbd_wire_parse_client_flags(
    const uint8_t input[4],
    uint32_t *flags
);
bool gdox_nbd_wire_parse_option_header(
    const uint8_t input[GDOX_NBD_OPTION_HEADER_BYTES],
    uint32_t *option,
    uint32_t *payload_length
);
bool gdox_nbd_wire_parse_info_name(
    const uint8_t *payload,
    size_t payload_bytes,
    const uint8_t **name,
    size_t *name_bytes
);
bool gdox_nbd_wire_parse_request(
    const uint8_t input[GDOX_NBD_REQUEST_BYTES],
    gdox_nbd_request *request
);
bool gdox_nbd_wire_read_is_valid(
    const gdox_nbd_request *request,
    uint64_t export_bytes
);

void gdox_nbd_wire_option_reply_header(
    uint8_t output[GDOX_NBD_OPTION_REPLY_HEADER_BYTES],
    uint32_t option,
    uint32_t reply,
    uint32_t payload_bytes
);
void gdox_nbd_wire_simple_reply(
    uint8_t output[GDOX_NBD_SIMPLE_REPLY_BYTES],
    uint32_t reply_error,
    uint64_t handle
);
void gdox_nbd_wire_export_response(
    uint8_t output[GDOX_NBD_EXPORT_RESPONSE_BYTES],
    uint64_t length,
    uint16_t export_flags
);
void gdox_nbd_wire_block_size_info(
    uint8_t output[GDOX_NBD_BLOCK_SIZE_INFO_BYTES]
);
void gdox_nbd_wire_export_info(
    uint8_t output[GDOX_NBD_EXPORT_INFO_BYTES],
    uint64_t length,
    uint16_t export_flags
);

#endif
