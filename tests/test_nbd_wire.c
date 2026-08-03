#include "test.h"

#include "platform/nbd_wire.h"

#include <stdint.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)(
        (uint16_t)((uint16_t)input[0] << 8U) | (uint16_t)input[1]
    );
}

static uint32_t read_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static uint64_t read_u64(const uint8_t *input)
{
    return (uint64_t)read_u32(input) << 32U | read_u32(input + 4U);
}

static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)(value & UINT16_C(0x00ff));
}

static void put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
    output[2] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
    output[3] = (uint8_t)(value & UINT32_C(0xff));
}

static void put_u64(uint8_t *output, uint64_t value)
{
    put_u32(output, (uint32_t)(value >> 32U));
    put_u32(output + 4U, (uint32_t)(value & UINT64_C(0xffffffff)));
}

static void test_handshake_wire(void)
{
    uint8_t greeting[GDOX_NBD_GREETING_BYTES];
    uint8_t flags[4];
    uint32_t parsed_flags = 0U;

    gdox_nbd_wire_greeting(greeting);
    GDOX_TEST_CHECK(
        read_u64(greeting) == UINT64_C(0x4e42444d41474943)
    );
    GDOX_TEST_CHECK(
        read_u64(greeting + 8U) == UINT64_C(0x49484156454f5054)
    );
    GDOX_TEST_CHECK(read_u16(greeting + 16U) == 3U);

    put_u32(flags, GDOX_NBD_FLAG_C_FIXED_NEWSTYLE);
    GDOX_TEST_CHECK(gdox_nbd_wire_parse_client_flags(flags, &parsed_flags));
    GDOX_TEST_CHECK(parsed_flags == GDOX_NBD_FLAG_C_FIXED_NEWSTYLE);
    put_u32(
        flags,
        GDOX_NBD_FLAG_C_FIXED_NEWSTYLE | GDOX_NBD_FLAG_C_NO_ZEROES
    );
    GDOX_TEST_CHECK(gdox_nbd_wire_parse_client_flags(flags, &parsed_flags));
    put_u32(flags, 0U);
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_client_flags(flags, &parsed_flags));
    put_u32(flags, GDOX_NBD_FLAG_C_FIXED_NEWSTYLE | UINT32_C(4));
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_client_flags(flags, &parsed_flags));
}

static void test_option_wire(void)
{
    uint8_t header[GDOX_NBD_OPTION_HEADER_BYTES];
    uint8_t info[11];
    const uint8_t *name = NULL;
    size_t name_bytes = 0U;
    uint32_t option = 0U;
    uint32_t payload_bytes = 0U;

    put_u64(header, UINT64_C(0x49484156454f5054));
    put_u32(header + 8U, GDOX_NBD_OPT_GO);
    put_u32(header + 12U, GDOX_NBD_MAX_OPTION_SIZE);
    GDOX_TEST_CHECK(gdox_nbd_wire_parse_option_header(
        header,
        &option,
        &payload_bytes
    ));
    GDOX_TEST_CHECK(option == GDOX_NBD_OPT_GO);
    GDOX_TEST_CHECK(payload_bytes == GDOX_NBD_MAX_OPTION_SIZE);
    put_u32(header + 12U, GDOX_NBD_MAX_OPTION_SIZE + 1U);
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_option_header(
        header,
        &option,
        &payload_bytes
    ));
    header[0] ^= UINT8_C(1);
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_option_header(
        header,
        &option,
        &payload_bytes
    ));

    put_u32(info, 3U);
    memcpy(info + 4U, "nbd", 3U);
    put_u16(info + 7U, 1U);
    put_u16(info + 9U, 0U);
    GDOX_TEST_CHECK(gdox_nbd_wire_parse_info_name(
        info,
        sizeof(info),
        &name,
        &name_bytes
    ));
    GDOX_TEST_CHECK(name_bytes == 3U && memcmp(name, "nbd", 3U) == 0);
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_info_name(
        info,
        sizeof(info) - 1U,
        &name,
        &name_bytes
    ));
    put_u32(info, UINT32_MAX);
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_info_name(
        info,
        sizeof(info),
        &name,
        &name_bytes
    ));
}

static void test_request_wire(void)
{
    uint8_t input[GDOX_NBD_REQUEST_BYTES];
    gdox_nbd_request request;

    put_u32(input, UINT32_C(0x25609513));
    put_u16(input + 4U, 0U);
    put_u16(input + 6U, GDOX_NBD_CMD_READ);
    put_u64(input + 8U, UINT64_C(0x123456789abcdef0));
    put_u64(input + 16U, UINT64_C(4096));
    put_u32(input + 24U, 8192U);
    GDOX_TEST_CHECK(gdox_nbd_wire_parse_request(input, &request));
    GDOX_TEST_CHECK(request.flags == 0U);
    GDOX_TEST_CHECK(request.command == GDOX_NBD_CMD_READ);
    GDOX_TEST_CHECK(request.handle == UINT64_C(0x123456789abcdef0));
    GDOX_TEST_CHECK(request.offset == UINT64_C(4096));
    GDOX_TEST_CHECK(request.length == 8192U);
    GDOX_TEST_CHECK(gdox_nbd_wire_read_is_valid(
        &request,
        UINT64_C(12288)
    ));
    GDOX_TEST_CHECK(!gdox_nbd_wire_read_is_valid(
        &request,
        UINT64_C(12287)
    ));
    request.offset = UINT64_MAX;
    GDOX_TEST_CHECK(!gdox_nbd_wire_read_is_valid(&request, UINT64_MAX - 1U));
    request.offset = 0U;
    request.length = GDOX_NBD_MAX_BUFFER_SIZE + 1U;
    GDOX_TEST_CHECK(!gdox_nbd_wire_read_is_valid(&request, UINT64_MAX));
    input[0] ^= UINT8_C(1);
    GDOX_TEST_CHECK(!gdox_nbd_wire_parse_request(input, &request));
}

static void test_reply_wire(void)
{
    uint8_t option[GDOX_NBD_OPTION_REPLY_HEADER_BYTES];
    uint8_t simple[GDOX_NBD_SIMPLE_REPLY_BYTES];
    uint8_t response[GDOX_NBD_EXPORT_RESPONSE_BYTES];
    uint8_t block_sizes[GDOX_NBD_BLOCK_SIZE_INFO_BYTES];
    uint8_t export_info[GDOX_NBD_EXPORT_INFO_BYTES];

    gdox_nbd_wire_option_reply_header(
        option,
        GDOX_NBD_OPT_INFO,
        GDOX_NBD_REP_ACK,
        7U
    );
    GDOX_TEST_CHECK(
        read_u64(option) == UINT64_C(0x0003e889045565a9)
    );
    GDOX_TEST_CHECK(read_u32(option + 8U) == GDOX_NBD_OPT_INFO);
    GDOX_TEST_CHECK(read_u32(option + 12U) == GDOX_NBD_REP_ACK);
    GDOX_TEST_CHECK(read_u32(option + 16U) == 7U);

    gdox_nbd_wire_simple_reply(simple, GDOX_NBD_EIO, UINT64_C(9));
    GDOX_TEST_CHECK(read_u32(simple) == UINT32_C(0x67446698));
    GDOX_TEST_CHECK(read_u32(simple + 4U) == GDOX_NBD_EIO);
    GDOX_TEST_CHECK(read_u64(simple + 8U) == UINT64_C(9));

    gdox_nbd_wire_export_response(response, UINT64_C(4096), 3U);
    GDOX_TEST_CHECK(read_u64(response) == UINT64_C(4096));
    GDOX_TEST_CHECK(read_u16(response + 8U) == 3U);
    gdox_nbd_wire_block_size_info(block_sizes);
    GDOX_TEST_CHECK(read_u16(block_sizes) == 3U);
    GDOX_TEST_CHECK(read_u32(block_sizes + 2U) == 1U);
    GDOX_TEST_CHECK(read_u32(block_sizes + 6U) == 64U * 1024U);
    GDOX_TEST_CHECK(
        read_u32(block_sizes + 10U) == GDOX_NBD_MAX_BUFFER_SIZE
    );
    gdox_nbd_wire_export_info(export_info, UINT64_C(4096), 3U);
    GDOX_TEST_CHECK(read_u16(export_info) == 0U);
    GDOX_TEST_CHECK(read_u64(export_info + 2U) == UINT64_C(4096));
    GDOX_TEST_CHECK(read_u16(export_info + 10U) == 3U);
}

void gdox_test_nbd_wire(void)
{
    test_handshake_wire();
    test_option_wire();
    test_request_wire();
    test_reply_wire();
}
