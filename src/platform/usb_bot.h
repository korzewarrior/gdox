#ifndef GDOX_USB_BOT_H
#define GDOX_USB_BOT_H

#include "platform/scsi_transport.h"
#include "gdox/optical.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum gdox_usb_bot_identity {
    GDOX_USB_BOT_GP63,
    GDOX_USB_BOT_GP65,
    GDOX_USB_BOT_GP08,
    GDOX_USB_BOT_ASUS_NR09,
    GDOX_USB_BOT_SP80,
    /* Native Windows SATA/SPTI profile; never a USB VID/PID wildcard. */
    GDOX_SATA_ASUS_MT1862,
    /* Exact experimental Windows USB/SPTI profile. */
    GDOX_USB_BOT_GP57,
    GDOX_USB_BOT_IDENTITY_COUNT,
} gdox_usb_bot_identity;

typedef struct gdox_usb_bot_observation {
    bool drive_present;
    bool media_status_known;
    bool media_present;
} gdox_usb_bot_observation;

typedef struct gdox_usb_bot_device {
    gdox_usb_bot_identity identity;
    char id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    char name[GDOX_OPTICAL_DEVICE_NAME_CAPACITY];
    char location[GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY];
    gdox_optical_connection connection;
    bool media_status_known;
    bool media_present;
    bool accessible;
} gdox_usb_bot_device;

bool gdox_usb_bot_list_devices(
    gdox_usb_bot_device *devices,
    size_t capacity,
    size_t *count,
    bool query_media,
    gdox_error *error
);
bool gdox_usb_bot_list_devices_filtered(
    gdox_usb_bot_device *devices,
    size_t capacity,
    size_t *count,
    const gdox_optical_media_query *query,
    gdox_error *error
);
bool gdox_usb_bot_open_device(
    gdox_usb_bot_identity identity,
    const char *device_id,
    gdox_scsi_transport *transport,
    gdox_error *error
);
bool gdox_usb_bot_device_connected(
    gdox_usb_bot_identity identity,
    const char *device_id,
    bool *connected,
    gdox_error *error
);

bool gdox_usb_bot_open(
    gdox_usb_bot_identity identity,
    gdox_scsi_transport *transport,
    gdox_error *error
);
#if defined(__ANDROID__)
/*
 * Android grants USB access through UsbManager. The Java-owned file
 * descriptor must remain open until the returned transport is closed.
 */
bool gdox_usb_bot_open_file_descriptor(
    int file_descriptor,
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
);
/*
 * Opens a short-lived Android observer without resetting the USB device when
 * the transport closes. This permits a clean handoff to a subsequent live
 * optical session without forcing the device to re-enumerate.
 */
bool gdox_usb_bot_open_observer_file_descriptor(
    int file_descriptor,
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
);
/*
 * Keeps the kernel mass-storage driver detached when the transport closes so
 * the next GDOX owner can claim the same enumerated device without a bind
 * cycle. The successor is responsible for eventually restoring ownership.
 */
bool gdox_usb_bot_prepare_handoff(
    gdox_scsi_transport *transport,
    gdox_error *error
);
#endif
bool gdox_usb_bot_observe_all(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
);
bool gdox_usb_bot_present_all(
    bool drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
);

#endif
