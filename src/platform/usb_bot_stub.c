#include "platform/usb_bot.h"

#include <stddef.h>

bool gdox_usb_bot_list_devices(
    gdox_usb_bot_device *devices, size_t capacity, size_t *count,
    bool query_media, gdox_error *error
)
{
    (void)query_media;
    if (count != NULL) {
        *count = 0U;
    }
    if (devices == NULL || capacity == 0U || count == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "optical device storage and count are required");
    } else {
        gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
            "this build has no libusb transport");
    }
    return false;
}

bool gdox_usb_bot_open_device(
    gdox_usb_bot_identity identity, const char *device_id,
    gdox_scsi_transport *transport, gdox_error *error
)
{
    (void)device_id;
    return gdox_usb_bot_open(identity, transport, error);
}

bool gdox_usb_bot_device_connected(
    gdox_usb_bot_identity identity, const char *device_id,
    bool *connected, gdox_error *error
)
{
    (void)identity;
    (void)device_id;
    if (connected != NULL) {
        *connected = false;
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
        "this build has no libusb transport");
    return false;
}

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

bool gdox_usb_bot_observe_all(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    size_t index;

    gdox_error_clear(error);
    if (observations == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical observations output is required"
        );
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        observations[index] = (gdox_usb_bot_observation){0};
    }
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED, "this build has no libusb transport");
    return false;
}

bool gdox_usb_bot_present_all(
    bool drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    size_t index;

    gdox_error_clear(error);
    if (drive_present == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        drive_present[index] = false;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "this build has no libusb transport"
    );
    return false;
}
