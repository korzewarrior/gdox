#ifndef GDOX_USB_BOT_IDENTITY_H
#define GDOX_USB_BOT_IDENTITY_H

#include "platform/usb_bot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDOX_USB_BOT_MAX_PORT_DEPTH 8U

typedef struct gdox_usb_bot_identity_spec {
    gdox_usb_bot_identity identity;
    uint16_t vendor_id;
    uint16_t product_id;
    const char *scsi_vendor;
    const char *scsi_model;
    const char *scsi_revision;
} gdox_usb_bot_identity_spec;

typedef struct gdox_usb_bot_observed_identity {
    uint16_t vendor_id;
    uint16_t product_id;
    const char *scsi_vendor;
    const char *scsi_model;
    const char *scsi_revision;
} gdox_usb_bot_observed_identity;

typedef struct gdox_usb_bot_location {
    uint8_t bus;
    uint8_t address;
    uint8_t ports[GDOX_USB_BOT_MAX_PORT_DEPTH];
    size_t port_count;
} gdox_usb_bot_location;

const gdox_usb_bot_identity_spec *gdox_usb_bot_identity_get(
    gdox_usb_bot_identity identity
);
bool gdox_usb_bot_identity_matches(
    gdox_usb_bot_identity requested,
    const gdox_usb_bot_observed_identity *observed
);
bool gdox_usb_bot_location_matches(
    const gdox_usb_bot_location *expected,
    const gdox_usb_bot_location *observed
);
bool gdox_usb_bot_candidate_matches(
    gdox_usb_bot_identity requested,
    const gdox_usb_bot_location *expected_location,
    const gdox_usb_bot_observed_identity *observed_identity,
    const gdox_usb_bot_location *observed_location
);

#endif
