#ifndef GDOX_OPTICAL_DRIVER_H
#define GDOX_OPTICAL_DRIVER_H

#include "gdox/optical.h"
#include "platform/usb_bot.h"

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
