#ifndef GDOX_OPTICAL_H
#define GDOX_OPTICAL_H

#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stddef.h>
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
#define GDOX_SP80_USB_VENDOR_ID UINT16_C(0x0e8d)
#define GDOX_SP80_USB_PRODUCT_ID UINT16_C(0x1887)
#define GDOX_SP80_SCSI_VENDOR "HL-DT-ST"
#define GDOX_SP80_SCSI_MODEL "DVDRAM SP80NB80"
#define GDOX_SP80_SCSI_REVISION "RF02"
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
#define GDOX_ASUS_MT1862_SCSI_VENDOR "ASUS"
#define GDOX_ASUS_MT1862_SCSI_MODEL "DRW-24D5MT"
#define GDOX_ASUS_MT1862_SCSI_REVISION "2.00"
#define GDOX_GP57_USB_VENDOR_ID UINT16_C(0x0e8d)
#define GDOX_GP57_USB_PRODUCT_ID UINT16_C(0x1887)
#define GDOX_GP57_SCSI_VENDOR "HL-DT-ST"
#define GDOX_GP57_SCSI_MODEL "DVDRAM GP57EB40"
#define GDOX_GP57_SCSI_REVISION "PB00"
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
    GDOX_OPTICAL_DRIVE_SP80,
    GDOX_OPTICAL_DRIVE_ASUS_MT1862,
    GDOX_OPTICAL_DRIVE_GP57,
} gdox_optical_drive;

#define GDOX_OPTICAL_DEVICE_ID_CAPACITY 1024U
#define GDOX_OPTICAL_DEVICE_NAME_CAPACITY 160U
#define GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY 96U
#define GDOX_OPTICAL_MAX_DEVICES 32U

typedef enum gdox_optical_connection {
    GDOX_OPTICAL_CONNECTION_UNKNOWN = 0,
    GDOX_OPTICAL_CONNECTION_USB,
    GDOX_OPTICAL_CONNECTION_SATA,
    GDOX_OPTICAL_CONNECTION_OTHER,
} gdox_optical_connection;

/* IDs identify physical devices, not a model or its current drive letter. */
typedef struct gdox_optical_device {
    char id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    char name[GDOX_OPTICAL_DEVICE_NAME_CAPACITY];
    char location[GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY];
    gdox_optical_drive drive;
    gdox_optical_connection connection;
    bool media_status_known;
    bool media_present;
    bool accessible;
} gdox_optical_device;

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

/*
 * Inventory never takes ownership or changes a drive's state. query_media=false
 * also avoids SCSI commands. count is the number of populated entries, never
 * more than capacity; an insufficient buffer is reported as an error.
 */
bool gdox_optical_list_devices(
    gdox_optical_device *devices,
    size_t capacity,
    size_t *count,
    bool query_media,
    gdox_error *error
);
bool gdox_optical_device_connected(
    const gdox_optical_device *device,
    bool *connected,
    gdox_error *error
);
bool gdox_optical_open_device_media(
    const gdox_optical_device *device,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
);
bool gdox_optical_eject_device(
    const gdox_optical_device *device,
    gdox_error *error
);
bool gdox_optical_complete_device_eject_request(
    const gdox_optical_device *device,
    gdox_optical_eject_completion *completion,
    gdox_error *error
);

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
