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
#define GDOX_GP63_SCSI_VENDOR "HL-DT-ST"
#define GDOX_GP63_SCSI_MODEL "DVDRAM GP63EX70"
#define GDOX_GP63_SCSI_REVISION "RF02"
#define GDOX_GP65_USB_VENDOR_ID UINT16_C(0x0e8d)
#define GDOX_GP65_USB_PRODUCT_ID UINT16_C(0x1887)
#define GDOX_GP65_SCSI_VENDOR "HL-DT-ST"
#define GDOX_GP65_SCSI_MODEL "DVDRAM GP65NB60"
#define GDOX_GP65_SCSI_REVISION "PB00"
#define GDOX_GP08_USB_VENDOR_ID UINT16_C(0x152e)
#define GDOX_GP08_USB_PRODUCT_ID UINT16_C(0x2507)
#define GDOX_GP08_SCSI_VENDOR "HL-DT-ST"
#define GDOX_GP08_SCSI_MODEL "DVDRAM GP08NU10"
#define GDOX_GP08_SCSI_REVISION "JE01"
#define GDOX_ASUS_USB_VENDOR_ID UINT16_C(0x13fd)
#define GDOX_ASUS_USB_PRODUCT_ID UINT16_C(0x1640)
#define GDOX_ASUS_SCSI_VENDOR "ASUS"
#define GDOX_ASUS_SCSI_MODEL "SDRW-08D1S-U"
#define GDOX_ASUS_SCSI_REVISION "A202"
#define GDOX_XGD1_TOTAL_SECTORS UINT64_C(3820880)
#define GDOX_XGD2_TOTAL_SECTORS UINT64_C(0x3a6104)
#define GDOX_XGD2_GAME_PARTITION_LBA UINT64_C(0x1fb20)
#define GDOX_GP63_XGD3_GAME_PARTITION_LBA UINT64_C(0x4100)

typedef enum gdox_optical_drive {
    GDOX_OPTICAL_DRIVE_NONE = 0,
    GDOX_OPTICAL_DRIVE_GP63,
    GDOX_OPTICAL_DRIVE_GP08,
    GDOX_OPTICAL_DRIVE_GP65,
    GDOX_OPTICAL_DRIVE_ASUS_NR09,
} gdox_optical_drive;

typedef struct gdox_optical_presence {
    bool drive_present;
    bool media_status_known;
    bool media_present;
    gdox_optical_drive drive;
} gdox_optical_presence;

typedef enum gdox_optical_media_profile {
    GDOX_OPTICAL_MEDIA_UNKNOWN = 0,
    GDOX_OPTICAL_MEDIA_XGD1,
    GDOX_OPTICAL_MEDIA_XGD2,
    GDOX_OPTICAL_MEDIA_XGD3,
} gdox_optical_media_profile;

typedef struct gdox_optical_media_info {
    gdox_optical_media_profile profile;
    uint64_t game_partition_lba;
    uint32_t sequential_read_blocks;
} gdox_optical_media_info;

typedef enum gdox_optical_eject_completion {
    GDOX_OPTICAL_EJECT_COMPLETION_NONE = 0,
    GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED,
    GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT,
} gdox_optical_eject_completion;

const char *gdox_optical_drive_name(gdox_optical_drive drive);
bool gdox_optical_drive_can_eject(gdox_optical_drive drive);
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
bool gdox_optical_open_media(
    gdox_optical_drive drive,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
);
bool gdox_optical_eject(
    gdox_optical_drive drive,
    gdox_error *error
);
/* Call only after playback, export, and source ownership have been released. */
bool gdox_optical_complete_eject_request(
    gdox_optical_drive drive,
    gdox_optical_eject_completion *completion,
    gdox_error *error
);

#ifdef __cplusplus
}
#endif

#endif
