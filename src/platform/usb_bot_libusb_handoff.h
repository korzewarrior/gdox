#ifndef GDOX_USB_BOT_LIBUSB_HANDOFF_H
#define GDOX_USB_BOT_LIBUSB_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

typedef enum gdox_libusb_handoff_phase {
    GDOX_LIBUSB_HANDOFF_NONE = 0,
    GDOX_LIBUSB_HANDOFF_QUERY_DRIVER,
    GDOX_LIBUSB_HANDOFF_ENABLE_AUTO_DETACH,
    GDOX_LIBUSB_HANDOFF_DETACH_DRIVER,
    GDOX_LIBUSB_HANDOFF_CLAIM_INTERFACE,
    GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE,
    GDOX_LIBUSB_HANDOFF_ATTACH_DRIVER,
    GDOX_LIBUSB_HANDOFF_VERIFY_DRIVER,
} gdox_libusb_handoff_phase;

typedef struct gdox_libusb_handoff_result {
    gdox_libusb_handoff_phase phase;
    int code;
} gdox_libusb_handoff_result;

typedef struct gdox_libusb_handoff_state {
    bool interface_claimed;
    bool reattach_required;
} gdox_libusb_handoff_state;

typedef struct gdox_libusb_handoff_ops {
    int (*driver_active)(void *handle, int interface_number);
    int (*set_auto_detach)(void *handle, int enabled);
    int (*detach_driver)(void *handle, int interface_number);
    int (*claim_interface)(void *handle, int interface_number);
    int (*release_interface)(void *handle, int interface_number);
    int (*attach_driver)(void *handle, int interface_number);
    void (*wait_ms)(uint32_t milliseconds);
} gdox_libusb_handoff_ops;

typedef void (*gdox_libusb_close_handle)(void *handle);

bool gdox_libusb_handoff_claim_with_ops(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
);

bool gdox_libusb_handoff_restore_with_ops(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
);

bool gdox_libusb_handoff_discard_with_ops(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result,
    gdox_libusb_close_handle close_handle
);

bool gdox_libusb_handoff_claim(
    void *handle,
    int interface_number,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
);

bool gdox_libusb_handoff_restore(
    void *handle,
    int interface_number,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
);

bool gdox_libusb_handoff_discard(
    void *handle,
    int interface_number,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
);

#endif
