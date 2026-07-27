#ifndef GDOX_USB_BOT_H
#define GDOX_USB_BOT_H

#include "platform/scsi_transport.h"

#include <stdbool.h>
#include <stdint.h>

bool gdox_usb_bot_open(
    uint16_t vendor_id,
    uint16_t product_id,
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
bool gdox_usb_bot_present(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    gdox_error *error
);
bool gdox_usb_bot_observe(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    bool *media_status_known,
    bool *media_present,
    gdox_error *error
);
bool gdox_usb_bot_restore_kernel_driver(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *reattached,
    gdox_error *error
);

#endif
