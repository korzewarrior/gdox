#define _POSIX_C_SOURCE 200809L

#include "platform/usb_bot_libusb_handoff.h"

#include "platform/portable_sync.h"

#include <libusb.h>

#include <stddef.h>
#include <string.h>

enum {
    GDOX_LIBUSB_REATTACH_ATTEMPTS = 20U,
    GDOX_LIBUSB_REATTACH_DELAY_MS = 25U,
};

static void set_result(
    gdox_libusb_handoff_result *result,
    gdox_libusb_handoff_phase phase,
    int code
)
{
    if (result != NULL) {
        result->phase = phase;
        result->code = code;
    }
}

static bool valid_ops(const gdox_libusb_handoff_ops *ops)
{
    return ops != NULL
        && ops->driver_active != NULL
        && ops->set_auto_detach != NULL
        && ops->detach_driver != NULL
        && ops->claim_interface != NULL
        && ops->release_interface != NULL
        && ops->attach_driver != NULL
        && ops->wait_ms != NULL;
}

bool gdox_libusb_handoff_claim_with_ops(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
)
{
    int active;
    int operation;

    if (handle == NULL || state == NULL || !valid_ops(ops)) {
        set_result(
            result, GDOX_LIBUSB_HANDOFF_CLAIM_INTERFACE,
            LIBUSB_ERROR_INVALID_PARAM
        );
        return false;
    }
    memset(state, 0, sizeof(*state));
    set_result(result, GDOX_LIBUSB_HANDOFF_NONE, LIBUSB_SUCCESS);
    active = ops->driver_active(handle, interface_number);
    if (active < 0 && active != LIBUSB_ERROR_NOT_SUPPORTED) {
        set_result(result, GDOX_LIBUSB_HANDOFF_QUERY_DRIVER, active);
        return false;
    }
    state->reattach_required = active == 1;
    operation = ops->set_auto_detach(handle, 1);
    if (operation == LIBUSB_ERROR_NOT_SUPPORTED) {
        if (state->reattach_required) {
            operation = ops->detach_driver(handle, interface_number);
            if (operation != LIBUSB_SUCCESS) {
                set_result(
                    result, GDOX_LIBUSB_HANDOFF_DETACH_DRIVER, operation
                );
                return false;
            }
        }
    } else if (operation != LIBUSB_SUCCESS) {
        set_result(
            result, GDOX_LIBUSB_HANDOFF_ENABLE_AUTO_DETACH, operation
        );
        return false;
    }
    operation = ops->claim_interface(handle, interface_number);
    if (operation != LIBUSB_SUCCESS) {
        set_result(result, GDOX_LIBUSB_HANDOFF_CLAIM_INTERFACE, operation);
        return false;
    }
    state->interface_claimed = true;
    return true;
}

static bool driver_is_active(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    bool *query_succeeded,
    bool *device_gone,
    gdox_libusb_handoff_result *result
)
{
    const int active = ops->driver_active(handle, interface_number);

    *device_gone = active == LIBUSB_ERROR_NO_DEVICE;
    *query_succeeded = active >= 0 || *device_gone;
    if (active == 1) {
        return true;
    }
    if (active < 0 && !*device_gone) {
        set_result(result, GDOX_LIBUSB_HANDOFF_VERIFY_DRIVER, active);
    }
    return false;
}

bool gdox_libusb_handoff_restore_with_ops(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
)
{
    int release_result = LIBUSB_SUCCESS;
    int attach_result = LIBUSB_SUCCESS;
    bool query_succeeded;
    bool device_gone;
    unsigned int attempt;

    if (handle == NULL || state == NULL || !valid_ops(ops)) {
        set_result(
            result, GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE,
            LIBUSB_ERROR_INVALID_PARAM
        );
        return false;
    }
    set_result(result, GDOX_LIBUSB_HANDOFF_NONE, LIBUSB_SUCCESS);
    if (state->interface_claimed) {
        release_result = ops->release_interface(handle, interface_number);
        if (release_result == LIBUSB_SUCCESS
            || release_result == LIBUSB_ERROR_NO_DEVICE) {
            state->interface_claimed = false;
        }
        if (release_result == LIBUSB_ERROR_NO_DEVICE) {
            state->reattach_required = false;
            return true;
        }
    }
    if (!state->reattach_required) {
        if (release_result != LIBUSB_SUCCESS) {
            set_result(
                result, GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE,
                release_result
            );
            return false;
        }
        return true;
    }

    for (attempt = 0U; attempt < GDOX_LIBUSB_REATTACH_ATTEMPTS; ++attempt) {
        if (driver_is_active(
                handle,
                interface_number,
                ops,
                &query_succeeded,
                &device_gone,
                result
            )) {
            state->reattach_required = false;
            break;
        }
        if (device_gone) {
            state->interface_claimed = false;
            state->reattach_required = false;
            return true;
        }
        if (!query_succeeded) {
            break;
        }
        attach_result = ops->attach_driver(handle, interface_number);
        if (driver_is_active(
                handle,
                interface_number,
                ops,
                &query_succeeded,
                &device_gone,
                result
            )) {
            state->reattach_required = false;
            break;
        }
        if (device_gone) {
            state->interface_claimed = false;
            state->reattach_required = false;
            return true;
        }
        if (!query_succeeded) {
            break;
        }
        if (attach_result != LIBUSB_SUCCESS
            && attach_result != LIBUSB_ERROR_BUSY
            && attach_result != LIBUSB_ERROR_NOT_FOUND) {
            set_result(
                result, GDOX_LIBUSB_HANDOFF_ATTACH_DRIVER, attach_result
            );
            break;
        }
        if (attempt + 1U < GDOX_LIBUSB_REATTACH_ATTEMPTS) {
            ops->wait_ms(GDOX_LIBUSB_REATTACH_DELAY_MS);
        }
    }
    if (state->reattach_required) {
        if (result == NULL || result->phase == GDOX_LIBUSB_HANDOFF_NONE) {
            set_result(
                result,
                attach_result == LIBUSB_SUCCESS
                    ? GDOX_LIBUSB_HANDOFF_VERIFY_DRIVER
                    : GDOX_LIBUSB_HANDOFF_ATTACH_DRIVER,
                attach_result == LIBUSB_SUCCESS
                    ? LIBUSB_ERROR_OTHER
                    : attach_result
            );
        }
        return false;
    }
    if (release_result != LIBUSB_SUCCESS) {
        set_result(
            result, GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE, release_result
        );
        return false;
    }
    return true;
}

bool gdox_libusb_handoff_discard_with_ops(
    void *handle,
    int interface_number,
    const gdox_libusb_handoff_ops *ops,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result,
    gdox_libusb_close_handle close_handle
)
{
    if (close_handle == NULL) {
        set_result(
            result,
            GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE,
            LIBUSB_ERROR_INVALID_PARAM
        );
        return false;
    }
    if (!gdox_libusb_handoff_restore_with_ops(
            handle,
            interface_number,
            ops,
            state,
            result
        )) {
        return false;
    }
    close_handle(handle);
    return true;
}

static int production_driver_active(void *handle, int interface_number)
{
    return libusb_kernel_driver_active(handle, interface_number);
}

static int production_set_auto_detach(void *handle, int enabled)
{
    return libusb_set_auto_detach_kernel_driver(handle, enabled);
}

static int production_detach_driver(void *handle, int interface_number)
{
    return libusb_detach_kernel_driver(handle, interface_number);
}

static int production_claim_interface(void *handle, int interface_number)
{
    return libusb_claim_interface(handle, interface_number);
}

static int production_release_interface(void *handle, int interface_number)
{
    return libusb_release_interface(handle, interface_number);
}

static int production_attach_driver(void *handle, int interface_number)
{
    return libusb_attach_kernel_driver(handle, interface_number);
}

static const gdox_libusb_handoff_ops production_ops = {
    production_driver_active,
    production_set_auto_detach,
    production_detach_driver,
    production_claim_interface,
    production_release_interface,
    production_attach_driver,
    gdox_sleep_ms,
};

bool gdox_libusb_handoff_claim(
    void *handle,
    int interface_number,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
)
{
    return gdox_libusb_handoff_claim_with_ops(
        handle, interface_number, &production_ops, state, result
    );
}

bool gdox_libusb_handoff_restore(
    void *handle,
    int interface_number,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
)
{
    return gdox_libusb_handoff_restore_with_ops(
        handle, interface_number, &production_ops, state, result
    );
}

static void production_close_handle(void *handle)
{
    libusb_close(handle);
}

bool gdox_libusb_handoff_discard(
    void *handle,
    int interface_number,
    gdox_libusb_handoff_state *state,
    gdox_libusb_handoff_result *result
)
{
    return gdox_libusb_handoff_discard_with_ops(
        handle,
        interface_number,
        &production_ops,
        state,
        result,
        production_close_handle
    );
}
