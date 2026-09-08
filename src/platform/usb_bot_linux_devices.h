#ifndef GDOX_USB_BOT_LINUX_DEVICES_H
#define GDOX_USB_BOT_LINUX_DEVICES_H

#include "platform/usb_bot_identity.h"

struct libusb_device;

/* Cached USB topology and sysfs serial only; never opens or claims a device. */
bool gdox_usb_bot_linux_device_id(
    struct libusb_device *device,
    char output[GDOX_OPTICAL_DEVICE_ID_CAPACITY]
);

#ifdef GDOX_LINUX_DEVICES_TESTING
typedef struct gdox_linux_device_roots {
    const char *blocks;
    const char *usb;
    const char *devices;
} gdox_linux_device_roots;
bool gdox_usb_bot_linux_list_fixture(
    const gdox_linux_device_roots *roots,
    gdox_usb_bot_device *devices,
    size_t capacity,
    size_t *count,
    bool query_media,
    gdox_error *error
);
#endif

#endif
