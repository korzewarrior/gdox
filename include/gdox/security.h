#ifndef GDOX_SECURITY_H
#define GDOX_SECURITY_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_XGD1_SECURITY_SECTOR_BYTES 2048U
#define GDOX_XGD1_SECURITY_RANGE_COUNT 16U
#define GDOX_XGD1_RANGE_SECTORS 4096U
#define GDOX_XGD1_NORMALIZED_SECTORS 65536U
#define GDOX_XGD1_REDUMP_SECTORS UINT64_C(3820880)

typedef struct gdox_security_range {
    uint64_t start_lba;
    uint64_t end_lba;
} gdox_security_range;

typedef struct gdox_security_sector_report {
    uint32_t raw_crc32;
    uint32_t fixed_xgd1_crc32;
    gdox_security_range ranges[GDOX_XGD1_SECURITY_RANGE_COUNT];
    size_t range_count;
    uint64_t normalized_sectors;
} gdox_security_sector_report;

uint64_t gdox_security_range_length(gdox_security_range range);

bool gdox_security_ranges_validate(
    const gdox_security_range ranges[GDOX_XGD1_SECURITY_RANGE_COUNT],
    gdox_error *error
);

bool gdox_security_sector_inspect(
    const uint8_t *bytes,
    size_t length,
    gdox_security_sector_report *report,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
