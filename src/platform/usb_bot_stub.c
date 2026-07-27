#include "platform/usb_bot.h"

bool gdox_usb_bot_open(
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    (void)vendor_id;
    (void)product_id;
    (void)transport;
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}

bool gdox_usb_bot_present(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    gdox_error *error
)
{
    (void)vendor_id;
    (void)product_id;
    if (drive_present != NULL) {
        *drive_present = false;
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}

bool gdox_usb_bot_observe(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    bool *media_status_known,
    bool *media_present,
    gdox_error *error
)
{
    (void)vendor_id;
    (void)product_id;
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
    uint16_t vendor_id,
    uint16_t product_id,
    bool *reattached,
    gdox_error *error
)
{
    (void)vendor_id;
    (void)product_id;
    if (reattached != NULL) {
        *reattached = false;
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}
