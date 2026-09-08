#ifndef GDOX_OPTICAL_DRIVER_H
#define GDOX_OPTICAL_DRIVER_H

#include "gdox/optical.h"
#include "platform/usb_bot.h"

gdox_usb_bot_identity gdox_optical_identity_for_drive(gdox_optical_drive drive);
gdox_optical_drive gdox_optical_drive_for_identity(gdox_usb_bot_identity identity);
uint32_t gdox_optical_sequential_read_blocks(gdox_optical_drive drive);

bool gdox_optical_open_gp63(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_open_gp63_media(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
);
bool gdox_optical_eject_gp63(gdox_error *error);
bool gdox_optical_open_gp65(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_eject_gp65(gdox_error *error);
bool gdox_optical_open_sp80(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_open_asus_mt1862(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_open_asus_mt1862_media(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
);
bool gdox_optical_open_gp57(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_open_gp57_media(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
);
bool gdox_optical_open_gp08(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_eject_gp08(gdox_error *error);
bool gdox_optical_open_asus_nr09(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);
bool gdox_optical_open_asus_nr09_media(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
);
bool gdox_optical_select_presence(
    const gdox_usb_bot_observation
        observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_optical_presence *presence,
    gdox_error *error
);

#endif
