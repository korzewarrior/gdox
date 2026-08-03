#include "platform/nbd_wire.h"

#include <stdint.h>

#define NBD_INIT_MAGIC UINT64_C(0x4e42444d41474943)
#define NBD_OPTS_MAGIC UINT64_C(0x49484156454f5054)
#define NBD_REP_MAGIC UINT64_C(0x0003e889045565a9)
#define NBD_REQUEST_MAGIC UINT32_C(0x25609513)
#define NBD_SIMPLE_REPLY_MAGIC UINT32_C(0x67446698)

#define NBD_FLAG_FIXED_NEWSTYLE UINT16_C(0x0001)
#define NBD_FLAG_NO_ZEROES UINT16_C(0x0002)
#define NBD_INFO_EXPORT UINT16_C(0)
#define NBD_INFO_BLOCK_SIZE UINT16_C(3)

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
    output[1] = (uint8_t)(value & UINT16_C(0x00ff));
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)((value >> 16U) & UINT32_C(0x000000ff));
    output[2] = (uint8_t)((value >> 8U) & UINT32_C(0x000000ff));
    output[3] = (uint8_t)(value & UINT32_C(0x000000ff));
}

static void put_be_u64(uint8_t *output, uint64_t value)
{
    put_be_u32(output, (uint32_t)(value >> 32U));
    put_be_u32(output + 4U, (uint32_t)(value & UINT64_C(0xffffffff)));
}

void gdox_nbd_wire_greeting(
    uint8_t output[GDOX_NBD_GREETING_BYTES]
)
{
    put_be_u64(output, NBD_INIT_MAGIC);
    put_be_u64(output + 8U, NBD_OPTS_MAGIC);
    put_be_u16(
        output + 16U,
        NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES
    );
}

bool gdox_nbd_wire_parse_client_flags(
    const uint8_t input[4],
    uint32_t *flags
)
{
    const uint32_t value = read_be_u32(input);

    if ((value & GDOX_NBD_FLAG_C_FIXED_NEWSTYLE) == 0U
        || (value & ~(
            GDOX_NBD_FLAG_C_FIXED_NEWSTYLE | GDOX_NBD_FLAG_C_NO_ZEROES
        )) != 0U) {
        return false;
    }
    *flags = value;
    return true;
}

bool gdox_nbd_wire_parse_option_header(
    const uint8_t input[GDOX_NBD_OPTION_HEADER_BYTES],
    uint32_t *option,
    uint32_t *payload_length
)
{
    uint32_t length;

    if (read_be_u64(input) != NBD_OPTS_MAGIC) {
        return false;
    }
    length = read_be_u32(input + 12U);
    if (length > GDOX_NBD_MAX_OPTION_SIZE) {
        return false;
    }
    *option = read_be_u32(input + 8U);
    *payload_length = length;
    return true;
}

bool gdox_nbd_wire_parse_info_name(
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

bool gdox_nbd_wire_parse_request(
    const uint8_t input[GDOX_NBD_REQUEST_BYTES],
    gdox_nbd_request *request
)
{
    if (read_be_u32(input) != NBD_REQUEST_MAGIC) {
        return false;
    }
    request->flags = read_be_u16(input + 4U);
    request->command = read_be_u16(input + 6U);
    request->handle = read_be_u64(input + 8U);
    request->offset = read_be_u64(input + 16U);
    request->length = read_be_u32(input + 24U);
    return true;
}

bool gdox_nbd_wire_read_is_valid(
    const gdox_nbd_request *request,
    uint64_t export_bytes
)
{
    return request->length <= GDOX_NBD_MAX_BUFFER_SIZE
        && request->offset <= export_bytes
        && request->length <= export_bytes - request->offset;
}

void gdox_nbd_wire_option_reply_header(
    uint8_t output[GDOX_NBD_OPTION_REPLY_HEADER_BYTES],
    uint32_t option,
    uint32_t reply,
    uint32_t payload_bytes
)
{
    put_be_u64(output, NBD_REP_MAGIC);
    put_be_u32(output + 8U, option);
    put_be_u32(output + 12U, reply);
    put_be_u32(output + 16U, payload_bytes);
}

void gdox_nbd_wire_simple_reply(
    uint8_t output[GDOX_NBD_SIMPLE_REPLY_BYTES],
    uint32_t reply_error,
    uint64_t handle
)
{
    put_be_u32(output, NBD_SIMPLE_REPLY_MAGIC);
    put_be_u32(output + 4U, reply_error);
    put_be_u64(output + 8U, handle);
}

void gdox_nbd_wire_export_response(
    uint8_t output[GDOX_NBD_EXPORT_RESPONSE_BYTES],
    uint64_t length,
    uint16_t export_flags
)
{
    put_be_u64(output, length);
    put_be_u16(output + 8U, export_flags);
}

void gdox_nbd_wire_block_size_info(
    uint8_t output[GDOX_NBD_BLOCK_SIZE_INFO_BYTES]
)
{
    put_be_u16(output, NBD_INFO_BLOCK_SIZE);
    put_be_u32(output + 2U, 1U);
    put_be_u32(output + 6U, 64U * 1024U);
    put_be_u32(output + 10U, GDOX_NBD_MAX_BUFFER_SIZE);
}

void gdox_nbd_wire_export_info(
    uint8_t output[GDOX_NBD_EXPORT_INFO_BYTES],
    uint64_t length,
    uint16_t export_flags
)
{
    put_be_u16(output, NBD_INFO_EXPORT);
    put_be_u64(output + 2U, length);
    put_be_u16(output + 10U, export_flags);
}
