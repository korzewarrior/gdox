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
#define GDOX_GP08_USB_VENDOR_ID UINT16_C(0x152e)
#define GDOX_GP08_USB_PRODUCT_ID UINT16_C(0x2507)
#define GDOX_GP08_SCSI_VENDOR "HL-DT-ST"
#define GDOX_GP08_SCSI_MODEL "DVDRAM GP08NU10"
#define GDOX_GP08_SCSI_REVISION "JE01"
#define GDOX_XGD1_TOTAL_SECTORS UINT64_C(3820880)

typedef enum gdox_optical_drive {
    GDOX_OPTICAL_DRIVE_NONE = 0,
    GDOX_OPTICAL_DRIVE_GP63,
    GDOX_OPTICAL_DRIVE_GP08,
} gdox_optical_drive;

typedef struct gdox_optical_presence {
    bool drive_present;
    bool media_status_known;
    bool media_present;
    gdox_optical_drive drive;
} gdox_optical_presence;

const char *gdox_optical_drive_name(gdox_optical_drive drive);
bool gdox_optical_observe(
    gdox_optical_presence *presence,
    gdox_error *error
);
bool gdox_optical_connected(
    gdox_optical_drive drive,
    bool *connected,
    gdox_error *error
);
bool gdox_optical_open(
    gdox_optical_drive drive,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_eject(
    gdox_optical_drive drive,
    gdox_error *error
);

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

bool gdox_optical_observe_gp08(
    gdox_optical_presence *presence,
    gdox_error *error
);
bool gdox_optical_gp08_connected(
    bool *connected,
    gdox_error *error
);
bool gdox_optical_open_gp08(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_eject_gp08(gdox_error *error);

#ifdef __cplusplus
}
#endif

#endif
