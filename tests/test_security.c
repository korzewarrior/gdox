#include "test.h"

#include "gdox/security.h"

#include <stdint.h>
#include <string.h>

#define TEST_DVD_DATA_START_PSN UINT32_C(0x030000)
#define TEST_LAYER_ZERO_SECTORS UINT32_C(1913776)
#define TEST_TABLE_ONE 1633U
#define TEST_TABLE_TWO 1840U
#define TEST_ENTRY_BYTES 9U

static uint32_t test_lba_to_psn(uint32_t lba)
{
    if (lba < TEST_LAYER_ZERO_SECTORS) {
        return TEST_DVD_DATA_START_PSN + lba;
    }
    const uint32_t boundary = TEST_DVD_DATA_START_PSN + TEST_LAYER_ZERO_SECTORS;
    const uint32_t layer_one_psn = boundary * UINT32_C(2) - TEST_DVD_DATA_START_PSN - lba;
    return (layer_one_psn - UINT32_C(1)) ^ UINT32_C(0x00ffffff);
}

static void test_write_u24(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 16U);
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)value;
}

void gdox_test_security(void)
{
    static const gdox_security_range expected[GDOX_XGD1_SECURITY_RANGE_COUNT] = {
        {368086, 372181},
        {525408, 529503},
        {681570, 685665},
        {832282, 836377},
        {988248, 992343},
        {1140874, 1144969},
        {1300696, 1304791},
        {1840940, 1845035},
        {2446418, 2450513},
        {2605898, 2609993},
        {2754620, 2758715},
        {2913906, 2918001},
        {3063402, 3067497},
        {3225774, 3229869},
        {3379016, 3383111},
        {3534182, 3538277},
    };
    uint8_t security[GDOX_XGD1_SECURITY_SECTOR_BYTES] = {0};
    gdox_security_sector_report report;
    gdox_error error;

    security[13] = UINT8_C(0x20);
    security[14] = UINT8_C(0x33);
    security[15] = UINT8_C(0xaf);
    for (size_t index = 0U; index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
        for (size_t table_index = 0U; table_index < 2U; ++table_index) {
            const size_t table = table_index == 0U ? TEST_TABLE_ONE : TEST_TABLE_TWO;
            const size_t entry = table + index * TEST_ENTRY_BYTES;
            test_write_u24(security + entry + 3U, test_lba_to_psn((uint32_t)expected[index].start_lba));
            test_write_u24(security + entry + 6U, test_lba_to_psn((uint32_t)expected[index].end_lba));
        }
    }

    GDOX_TEST_CHECK(gdox_security_sector_inspect(security, sizeof(security), &report, &error));
    GDOX_TEST_CHECK(report.range_count == GDOX_XGD1_SECURITY_RANGE_COUNT);
    GDOX_TEST_CHECK(report.normalized_sectors == GDOX_XGD1_NORMALIZED_SECTORS);
    GDOX_TEST_CHECK(memcmp(report.ranges, expected, sizeof(expected)) == 0);

    security[TEST_TABLE_TWO + 3U] ^= UINT8_C(1);
    GDOX_TEST_CHECK(!gdox_security_sector_inspect(security, sizeof(security), &report, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
}
