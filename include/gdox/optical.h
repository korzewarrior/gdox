#ifndef GDOX_OPTICAL_H
#define GDOX_OPTICAL_H

#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_GP63_USB_VENDOR_ID UINT16_C(0x0e8d)
#define GDOX_GP63_USB_PRODUCT_ID UINT16_C(0x1887)
#define GDOX_XGD1_TOTAL_SECTORS UINT64_C(3820880)

typedef struct gdox_optical_presence {
    bool drive_present;
    bool media_status_known;
    bool media_present;
} gdox_optical_presence;

bool gdox_optical_observe_gp63(
    gdox_optical_presence *presence,
    gdox_error *error
);
bool gdox_optical_gp63_connected(
    bool *connected,
    gdox_error *error
);
bool gdox_optical_open_gp63(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_eject_gp63(gdox_error *error);

#ifdef __cplusplus
}
#endif

#endif
