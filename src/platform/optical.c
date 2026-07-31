#include "gdox/optical.h"

#include <string.h>

const char *gdox_optical_drive_name(gdox_optical_drive drive)
{
    switch (drive) {
        case GDOX_OPTICAL_DRIVE_GP63:
            return GDOX_GP63_SCSI_VENDOR " " GDOX_GP63_SCSI_MODEL " "
                GDOX_GP63_SCSI_REVISION;
        case GDOX_OPTICAL_DRIVE_GP65:
            return GDOX_GP65_SCSI_VENDOR " " GDOX_GP65_SCSI_MODEL " "
                GDOX_GP65_SCSI_REVISION;
        case GDOX_OPTICAL_DRIVE_GP08:
            return GDOX_GP08_SCSI_VENDOR " " GDOX_GP08_SCSI_MODEL " "
                GDOX_GP08_SCSI_REVISION;
        case GDOX_OPTICAL_DRIVE_ASUS_NR09:
            return GDOX_ASUS_SCSI_VENDOR " " GDOX_ASUS_SCSI_MODEL " "
                GDOX_ASUS_SCSI_REVISION;
        case GDOX_OPTICAL_DRIVE_NONE:
            break;
    }
    return "Supported optical drive";
}

bool gdox_optical_drive_can_eject(gdox_optical_drive drive)
{
    return drive == GDOX_OPTICAL_DRIVE_GP63
        || drive == GDOX_OPTICAL_DRIVE_GP65
        || drive == GDOX_OPTICAL_DRIVE_GP08;
}

bool gdox_optical_observe(
    gdox_optical_presence *presence,
    gdox_error *error
)
{
    gdox_optical_presence gp63 = {0};
    gdox_optical_presence gp65 = {0};
    gdox_optical_presence gp08 = {0};
    gdox_optical_presence asus = {0};
    gdox_error gp63_error;
    gdox_error gp65_error;
    gdox_error gp08_error;
    gdox_error asus_error;
    bool gp63_observed;
    bool gp65_observed;
    bool gp08_observed;
    bool asus_observed;

    gdox_error_clear(error);
    if (presence == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    memset(presence, 0, sizeof(*presence));
    gp63_observed = gdox_optical_observe_gp63(&gp63, &gp63_error);
    gp65_observed = gdox_optical_observe_gp65(&gp65, &gp65_error);
    gp08_observed = gdox_optical_observe_gp08(&gp08, &gp08_error);
    asus_observed = gdox_optical_observe_asus_nr09(
        &asus,
        &asus_error
    );
    if (!gp63_observed && gp63_error.code != GDOX_ERROR_UNSUPPORTED) {
        *error = gp63_error;
        return false;
    }
    if (!gp08_observed && gp08_error.code != GDOX_ERROR_UNSUPPORTED) {
        *error = gp08_error;
        return false;
    }
    if (!gp65_observed && gp65_error.code != GDOX_ERROR_UNSUPPORTED) {
        *error = gp65_error;
        return false;
    }
    if (!asus_observed && asus_error.code != GDOX_ERROR_UNSUPPORTED) {
        *error = asus_error;
        return false;
    }
    if (!gp63_observed && !gp65_observed && !gp08_observed
        && !asus_observed) {
        *error = gp63_error.code != GDOX_ERROR_UNSUPPORTED
            ? gp63_error
            : gp65_error.code != GDOX_ERROR_UNSUPPORTED
                ? gp65_error
                : gp08_error.code != GDOX_ERROR_UNSUPPORTED
                    ? gp08_error
                    : asus_error;
        return false;
    }
    if ((unsigned int)gp63.drive_present
            + (unsigned int)gp65.drive_present
            + (unsigned int)gp08.drive_present
            + (unsigned int)asus.drive_present > 1U) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "connect only one supported optical drive at a time"
        );
        return false;
    }
    if (gp63.drive_present) {
        *presence = gp63;
        presence->drive = GDOX_OPTICAL_DRIVE_GP63;
    } else if (gp65.drive_present) {
        *presence = gp65;
        presence->drive = GDOX_OPTICAL_DRIVE_GP65;
    } else if (gp08.drive_present) {
        *presence = gp08;
        presence->drive = GDOX_OPTICAL_DRIVE_GP08;
    } else if (asus.drive_present) {
        *presence = asus;
        presence->drive = GDOX_OPTICAL_DRIVE_ASUS_NR09;
    }
    return true;
}

bool gdox_optical_connected(
    gdox_optical_drive drive,
    bool *connected,
    gdox_error *error
)
{
    switch (drive) {
        case GDOX_OPTICAL_DRIVE_GP63:
            return gdox_optical_gp63_connected(connected, error);
        case GDOX_OPTICAL_DRIVE_GP65:
            return gdox_optical_gp65_connected(connected, error);
        case GDOX_OPTICAL_DRIVE_GP08:
            return gdox_optical_gp08_connected(connected, error);
        case GDOX_OPTICAL_DRIVE_ASUS_NR09:
            return gdox_optical_asus_nr09_connected(connected, error);
        case GDOX_OPTICAL_DRIVE_NONE:
            break;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_ARGUMENT,
        "a supported optical drive selection is required"
    );
    return false;
}

bool gdox_optical_open(
    gdox_optical_drive drive,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    switch (drive) {
        case GDOX_OPTICAL_DRIVE_GP63:
            return gdox_optical_open_gp63(
                read_retries,
                ready_timeout_ms,
                source,
                error
            );
        case GDOX_OPTICAL_DRIVE_GP65:
            return gdox_optical_open_gp65(
                read_retries,
                ready_timeout_ms,
                source,
                error
            );
        case GDOX_OPTICAL_DRIVE_GP08:
            return gdox_optical_open_gp08(
                read_retries,
                ready_timeout_ms,
                source,
                error
            );
        case GDOX_OPTICAL_DRIVE_ASUS_NR09:
            return gdox_optical_open_asus_nr09(
                read_retries,
                ready_timeout_ms,
                source,
                error
            );
        case GDOX_OPTICAL_DRIVE_NONE:
            break;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_ARGUMENT,
        "a supported optical drive selection is required"
    );
    return false;
}

bool gdox_optical_eject(
    gdox_optical_drive drive,
    gdox_error *error
)
{
    switch (drive) {
        case GDOX_OPTICAL_DRIVE_GP63:
            return gdox_optical_eject_gp63(error);
        case GDOX_OPTICAL_DRIVE_GP65:
            return gdox_optical_eject_gp65(error);
        case GDOX_OPTICAL_DRIVE_GP08:
            return gdox_optical_eject_gp08(error);
        case GDOX_OPTICAL_DRIVE_ASUS_NR09:
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "the ASUS SDRW-08D1S-U tray must be operated manually"
            );
            return false;
        case GDOX_OPTICAL_DRIVE_NONE:
            break;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_ARGUMENT,
        "a supported optical drive selection is required"
    );
    return false;
}
