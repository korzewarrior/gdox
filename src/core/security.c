#include "gdox/security.h"

#include <string.h>

#define GDOX_DVD_DATA_START_PSN UINT32_C(0x030000)
#define GDOX_XGD1_LAYER_ZERO_SECTORS UINT32_C(1913776)
#define GDOX_SECURITY_TABLE_ONE 1633U
#define GDOX_SECURITY_TABLE_TWO 1840U
#define GDOX_SECURITY_ENTRY_BYTES ((size_t)9U)
#define GDOX_SECURITY_TABLE_ENTRIES ((size_t)23U)
#define GDOX_XGD1_FIX_START 0x200U
#define GDOX_XGD1_FIX_END 0x2d0U

static uint32_t gdox_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;

    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & UINT32_C(1)));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static uint32_t gdox_u24(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 16U) | ((uint32_t)bytes[1] << 8U) | (uint32_t)bytes[2];
}

static bool gdox_psn_to_lba(uint32_t psn, uint64_t *lba)
{
    const uint32_t boundary = GDOX_DVD_DATA_START_PSN + GDOX_XGD1_LAYER_ZERO_SECTORS;
    uint32_t logical = 0U;

    if (lba == NULL) {
        return false;
    }
    if (psn < boundary) {
        if (psn < GDOX_DVD_DATA_START_PSN) {
            return false;
        }
        logical = psn - GDOX_DVD_DATA_START_PSN;
    } else {
        const uint32_t inverted = (psn ^ UINT32_C(0x00ffffff)) + UINT32_C(1);
        const uint32_t doubled_boundary = boundary * UINT32_C(2);
        if (inverted > doubled_boundary - GDOX_DVD_DATA_START_PSN) {
            return false;
        }
        logical = doubled_boundary - inverted - GDOX_DVD_DATA_START_PSN;
    }
    *lba = logical;
    return true;
}

uint64_t gdox_security_range_length(gdox_security_range range)
{
    if (range.end_lba < range.start_lba) {
        return 0U;
    }
    return range.end_lba - range.start_lba + UINT64_C(1);
}

bool gdox_security_ranges_validate(
    const gdox_security_range ranges[GDOX_XGD1_SECURITY_RANGE_COUNT],
    gdox_error *error
)
{
    uint64_t total = 0U;

    gdox_error_clear(error);
    if (ranges == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "security ranges are required");
        return false;
    }
    for (size_t index = 0U; index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
        const gdox_security_range range = ranges[index];
        if (range.start_lba > range.end_lba || range.end_lba >= GDOX_XGD1_REDUMP_SECTORS) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "security range is outside the XGD1 image");
            return false;
        }
        if (gdox_security_range_length(range) != GDOX_XGD1_RANGE_SECTORS) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "each XGD1 range must contain 4096 sectors");
            return false;
        }
        if (index > 0U && ranges[index - 1U].end_lba >= range.start_lba) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "security ranges overlap or are unsorted");
            return false;
        }
        total += gdox_security_range_length(range);
    }
    if (total != GDOX_XGD1_NORMALIZED_SECTORS) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "XGD1 ranges do not total 65536 sectors");
        return false;
    }
    return true;
}

bool gdox_security_sector_inspect(
    const uint8_t *bytes,
    size_t length,
    gdox_security_sector_report *report,
    gdox_error *error
)
{
    const size_t table_bytes = GDOX_SECURITY_TABLE_ENTRIES * GDOX_SECURITY_ENTRY_BYTES;
    uint8_t fixed[GDOX_XGD1_SECURITY_SECTOR_BYTES];

    gdox_error_clear(error);
    if (bytes == NULL || report == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "security sector and report are required");
        return false;
    }
    memset(report, 0, sizeof(*report));
    if (length != GDOX_XGD1_SECURITY_SECTOR_BYTES) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "XGD1 SS.bin must be exactly 2048 bytes");
        return false;
    }
    if (bytes[13] != UINT8_C(0x20) || bytes[14] != UINT8_C(0x33)
        || bytes[15] != UINT8_C(0xaf)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "security sector is not XGD1");
        return false;
    }
    if (memcmp(
            bytes + GDOX_SECURITY_TABLE_ONE,
            bytes + GDOX_SECURITY_TABLE_TWO,
            table_bytes
        )
        != 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "security range table copies differ");
        return false;
    }

    for (size_t index = 0U; index < GDOX_XGD1_SECURITY_RANGE_COUNT; ++index) {
        const size_t entry = GDOX_SECURITY_TABLE_ONE + index * GDOX_SECURITY_ENTRY_BYTES;
        gdox_security_range *range = &report->ranges[index];
        if (!gdox_psn_to_lba(gdox_u24(bytes + entry + 3U), &range->start_lba)
            || !gdox_psn_to_lba(gdox_u24(bytes + entry + 6U), &range->end_lba)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "security range contains an invalid PSN");
            return false;
        }
    }
    if (!gdox_security_ranges_validate(report->ranges, error)) {
        return false;
    }

    memcpy(fixed, bytes, sizeof(fixed));
    memset(fixed + GDOX_XGD1_FIX_START, 0, GDOX_XGD1_FIX_END - GDOX_XGD1_FIX_START);
    report->raw_crc32 = gdox_crc32(bytes, length);
    report->fixed_xgd1_crc32 = gdox_crc32(fixed, sizeof(fixed));
    report->range_count = GDOX_XGD1_SECURITY_RANGE_COUNT;
    report->normalized_sectors = GDOX_XGD1_NORMALIZED_SECTORS;
    return true;
}
