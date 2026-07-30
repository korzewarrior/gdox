#include "platform/usb_bot.h"

bool gdox_usb_bot_open(
    gdox_usb_bot_identity identity,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)identity;
    (void)transport;
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}

bool gdox_usb_bot_present(
    gdox_usb_bot_identity identity,
    bool *drive_present,
    gdox_error *error
)
{
    (void)identity;
    if (drive_present != NULL) {
        *drive_present = false;
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}

bool gdox_usb_bot_observe(
    gdox_usb_bot_identity identity,
    bool *drive_present,
    bool *media_status_known,
    bool *media_present,
    gdox_error *error
)
{
    (void)identity;
    if (drive_present != NULL) {
        *drive_present = false;
    }
    if (media_status_known != NULL) {
        *media_status_known = false;
    }
    if (media_present != NULL) {
        *media_present = false;
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}

bool gdox_usb_bot_restore_kernel_driver(
    gdox_usb_bot_identity identity,
    bool *reattached,
    gdox_error *error
)
{
    (void)identity;
    if (reattached != NULL) {
        *reattached = false;
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}
