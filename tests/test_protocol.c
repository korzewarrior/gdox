#include "test.h"

#include "gdox/protocol.h"

#include <stdint.h>
#include <string.h>

void gdox_test_protocol(void)
{
    const gdox_read_disc_raw_request request = {
        .address = UINT32_C(0x00123456),
        .blocks = UINT32_C(0x20),
        .raw_addressing = true,
        .force_unit_access = true,
        .descramble = true,
    };
    const uint8_t expected[GDOX_C0_CDB_BYTES] = {
        0xc0, 0x1d, 0x00, 0x12, 0x34, 0x56, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
    };
    uint8_t cdb[GDOX_C0_CDB_BYTES] = {0};
    uint8_t raw[GDOX_RAW_DVD_FRAME_BYTES * 2U];
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES * 2U];
    gdox_error error;

    GDOX_TEST_CHECK(gdox_protocol_build_c0(&request, cdb, &error));
    GDOX_TEST_CHECK(memcmp(cdb, expected, sizeof(expected)) == 0);

    gdox_read_disc_raw_request individual = {
        .address = 0U,
        .blocks = 1U,
        .raw_addressing = true,
        .force_unit_access = false,
        .descramble = false,
    };
    GDOX_TEST_CHECK(gdox_protocol_build_c0(&individual, cdb, &error));
    GDOX_TEST_CHECK(cdb[1] == UINT8_C(0x05));
    individual.raw_addressing = false;
    individual.descramble = true;
    GDOX_TEST_CHECK(gdox_protocol_build_c0(&individual, cdb, &error));
    GDOX_TEST_CHECK(cdb[1] == UINT8_C(0x11));

    memset(raw, 0xaa, sizeof(raw));
    memset(raw + GDOX_RAW_DVD_MAIN_OFFSET, 0x11, GDOX_LOGICAL_SECTOR_BYTES);
    memset(
        raw + GDOX_RAW_DVD_FRAME_BYTES + GDOX_RAW_DVD_MAIN_OFFSET,
        0x22,
        GDOX_LOGICAL_SECTOR_BYTES
    );
    GDOX_TEST_CHECK(gdox_protocol_extract_main_data(raw, sizeof(raw), output, sizeof(output), &error));
    for (size_t index = 0U; index < GDOX_LOGICAL_SECTOR_BYTES; ++index) {
        GDOX_TEST_CHECK(output[index] == UINT8_C(0x11));
        GDOX_TEST_CHECK(output[GDOX_LOGICAL_SECTOR_BYTES + index] == UINT8_C(0x22));
    }
}
