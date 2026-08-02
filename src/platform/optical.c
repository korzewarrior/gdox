#include "gdox/optical.h"

#include "platform/optical_driver.h"
#include "platform/usb_bot.h"

#include <stddef.h>
#include <string.h>

typedef bool (*gdox_optical_open_fn)(
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
);

typedef bool (*gdox_optical_eject_fn)(gdox_error *error);

typedef struct gdox_optical_driver {
    gdox_optical_drive drive;
    gdox_usb_bot_identity identity;
    const char *name;
    gdox_optical_open_fn open;
    gdox_optical_eject_fn eject;
} gdox_optical_driver;

static const gdox_optical_driver drivers[] = {
    {
        GDOX_OPTICAL_DRIVE_GP63,
        GDOX_USB_BOT_GP63,
        GDOX_GP63_SCSI_VENDOR " " GDOX_GP63_SCSI_MODEL " "
            GDOX_GP63_SCSI_REVISION,
        gdox_optical_open_gp63,
        gdox_optical_eject_gp63,
    },
    {
        GDOX_OPTICAL_DRIVE_GP65,
        GDOX_USB_BOT_GP65,
        GDOX_GP65_SCSI_VENDOR " " GDOX_GP65_SCSI_MODEL " "
            GDOX_GP65_SCSI_REVISION,
        gdox_optical_open_gp65,
        gdox_optical_eject_gp65,
    },
    {
        GDOX_OPTICAL_DRIVE_GP08,
        GDOX_USB_BOT_GP08,
        GDOX_GP08_SCSI_VENDOR " " GDOX_GP08_SCSI_MODEL " "
            GDOX_GP08_SCSI_REVISION,
        gdox_optical_open_gp08,
        gdox_optical_eject_gp08,
    },
    {
        GDOX_OPTICAL_DRIVE_ASUS_NR09,
        GDOX_USB_BOT_ASUS_NR09,
        GDOX_ASUS_SCSI_VENDOR " " GDOX_ASUS_SCSI_MODEL " "
            GDOX_ASUS_SCSI_REVISION,
        gdox_optical_open_asus_nr09,
        NULL,
    },
};

_Static_assert(
    sizeof(drivers) / sizeof(drivers[0]) == GDOX_USB_BOT_IDENTITY_COUNT,
    "optical driver registry must cover every USB BOT identity"
);

static const gdox_optical_driver *find_driver(gdox_optical_drive drive)
{
    size_t index;

    for (index = 0U; index < sizeof(drivers) / sizeof(drivers[0]); ++index) {
        if (drivers[index].drive == drive) {
            return &drivers[index];
        }
    }
    return NULL;
}

const char *gdox_optical_drive_name(gdox_optical_drive drive)
{
    const gdox_optical_driver *driver = find_driver(drive);
    return driver != NULL ? driver->name : "Supported optical drive";
}

bool gdox_optical_drive_can_eject(gdox_optical_drive drive)
{
    const gdox_optical_driver *driver = find_driver(drive);
    return driver != NULL && driver->eject != NULL;
}

bool gdox_optical_select_presence(
    const gdox_usb_bot_observation
        observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_optical_presence *presence,
    gdox_error *error
)
{
    const gdox_optical_driver *selected = NULL;
    size_t index;

    gdox_error_clear(error);
    if (observations == NULL || presence == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical observation input and presence output are required"
        );
        return false;
    }
    memset(presence, 0, sizeof(*presence));
    for (index = 0U; index < sizeof(drivers) / sizeof(drivers[0]); ++index) {
        const gdox_optical_driver *driver = &drivers[index];
        const gdox_usb_bot_observation *observation =
            &observations[(size_t)driver->identity];

        if (!observation->drive_present) {
            continue;
        }
        if (selected != NULL) {
            memset(presence, 0, sizeof(*presence));
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "connect only one supported optical drive at a time"
            );
            return false;
        }
        selected = driver;
        presence->drive_present = true;
        presence->media_status_known = observation->media_status_known;
        presence->media_present = observation->media_present;
        presence->drive = driver->drive;
    }
    return true;
}

bool gdox_optical_observe(
    gdox_optical_presence *presence,
    gdox_error *error
)
{
    gdox_usb_bot_observation
        observations[GDOX_USB_BOT_IDENTITY_COUNT];

    gdox_error_clear(error);
    if (presence == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    if (!gdox_usb_bot_observe_all(observations, error)) {
        memset(presence, 0, sizeof(*presence));
        return false;
    }
    return gdox_optical_select_presence(observations, presence, error);
}

bool gdox_optical_connected(
    gdox_optical_drive drive,
    bool *connected,
    gdox_error *error
)
{
    bool drive_present[GDOX_USB_BOT_IDENTITY_COUNT];
    const gdox_optical_driver *driver = find_driver(drive);

    gdox_error_clear(error);
    if (driver == NULL || connected == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            driver == NULL
                ? "a supported optical drive selection is required"
                : "optical connection output is required"
        );
        return false;
    }
    *connected = false;
    if (!gdox_usb_bot_present_all(drive_present, error)) {
        return false;
    }
    *connected = drive_present[(size_t)driver->identity];
    return true;
}

bool gdox_optical_open(
    gdox_optical_drive drive,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    const gdox_optical_driver *driver = find_driver(drive);

    if (driver == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a supported optical drive selection is required"
        );
        return false;
    }
    return driver->open(
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

bool gdox_optical_eject(
    gdox_optical_drive drive,
    gdox_error *error
)
{
    const gdox_optical_driver *driver = find_driver(drive);

    if (driver == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a supported optical drive selection is required"
        );
        return false;
    }
    if (driver->eject == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "the ASUS SDRW-08D1S-U tray must be operated manually"
        );
        return false;
    }
    return driver->eject(error);
}
