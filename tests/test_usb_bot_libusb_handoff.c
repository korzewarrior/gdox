#include "platform/usb_bot_libusb_handoff.h"

#include "test.h"

#include <libusb.h>

#include <stddef.h>
#include <string.h>

int gdox_test_failures = 0;

enum { ACTIVE_SCRIPT_CAPACITY = 48 };

typedef struct fake_usb {
    int active_script[ACTIVE_SCRIPT_CAPACITY];
    size_t active_count;
    size_t active_index;
    int default_active;
    int auto_detach_result;
    int detach_result;
    int claim_result;
    int release_result;
    int attach_result;
    unsigned int auto_detach_calls;
    unsigned int detach_calls;
    unsigned int claim_calls;
    unsigned int release_calls;
    unsigned int attach_calls;
    unsigned int wait_calls;
    unsigned int close_calls;
} fake_usb;

static int fake_driver_active(void *handle, int interface_number)
{
    fake_usb *usb = handle;

    (void)interface_number;
    if (usb->active_index < usb->active_count) {
        return usb->active_script[usb->active_index++];
    }
    return usb->default_active;
}

static int fake_set_auto_detach(void *handle, int enabled)
{
    fake_usb *usb = handle;

    (void)enabled;
    ++usb->auto_detach_calls;
    return usb->auto_detach_result;
}

static int fake_detach(void *handle, int interface_number)
{
    fake_usb *usb = handle;

    (void)interface_number;
    ++usb->detach_calls;
    return usb->detach_result;
}

static int fake_claim(void *handle, int interface_number)
{
    fake_usb *usb = handle;

    (void)interface_number;
    ++usb->claim_calls;
    return usb->claim_result;
}

static int fake_release(void *handle, int interface_number)
{
    fake_usb *usb = handle;

    (void)interface_number;
    ++usb->release_calls;
    return usb->release_result;
}

static int fake_attach(void *handle, int interface_number)
{
    fake_usb *usb = handle;

    (void)interface_number;
    ++usb->attach_calls;
    return usb->attach_result;
}

static fake_usb *waiting_usb;

static void fake_wait(uint32_t milliseconds)
{
    (void)milliseconds;
    ++waiting_usb->wait_calls;
}

static void fake_close(void *handle)
{
    fake_usb *usb = handle;

    ++usb->close_calls;
}

static const gdox_libusb_handoff_ops fake_ops = {
    fake_driver_active,
    fake_set_auto_detach,
    fake_detach,
    fake_claim,
    fake_release,
    fake_attach,
    fake_wait,
};

static fake_usb make_usb(void)
{
    fake_usb usb;

    memset(&usb, 0, sizeof(usb));
    usb.auto_detach_result = LIBUSB_SUCCESS;
    usb.detach_result = LIBUSB_SUCCESS;
    usb.claim_result = LIBUSB_SUCCESS;
    usb.release_result = LIBUSB_SUCCESS;
    usb.attach_result = LIBUSB_SUCCESS;
    return usb;
}

static void normal_handoff_restores_driver(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state;
    gdox_libusb_handoff_result result;

    usb.active_script[0] = 1;
    usb.active_script[1] = 0;
    usb.active_script[2] = 1;
    usb.active_count = 3U;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(gdox_libusb_handoff_claim_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(state.interface_claimed);
    GDOX_TEST_CHECK(state.reattach_required);
    GDOX_TEST_CHECK(gdox_libusb_handoff_restore_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(!state.interface_claimed);
    GDOX_TEST_CHECK(!state.reattach_required);
    GDOX_TEST_CHECK(usb.release_calls == 1U);
    GDOX_TEST_CHECK(usb.attach_calls == 1U);
}

static void busy_is_not_success_without_verification(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {false, true};
    gdox_libusb_handoff_result result;

    usb.attach_result = LIBUSB_ERROR_BUSY;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(!gdox_libusb_handoff_restore_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(state.reattach_required);
    GDOX_TEST_CHECK(result.phase == GDOX_LIBUSB_HANDOFF_ATTACH_DRIVER);
    GDOX_TEST_CHECK(result.code == LIBUSB_ERROR_BUSY);
    GDOX_TEST_CHECK(usb.attach_calls == 20U);
    GDOX_TEST_CHECK(usb.wait_calls == 19U);
}

static void busy_succeeds_after_driver_becomes_active(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {false, true};
    gdox_libusb_handoff_result result;

    usb.active_script[0] = 0;
    usb.active_script[1] = 0;
    usb.active_script[2] = 1;
    usb.active_count = 3U;
    usb.attach_result = LIBUSB_ERROR_BUSY;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(gdox_libusb_handoff_restore_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(!state.reattach_required);
    GDOX_TEST_CHECK(usb.attach_calls == 1U);
    GDOX_TEST_CHECK(usb.wait_calls == 1U);
}

static void failed_claim_still_restores_detached_driver(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state;
    gdox_libusb_handoff_result result;

    usb.active_script[0] = 1;
    usb.active_script[1] = 0;
    usb.active_script[2] = 1;
    usb.active_count = 3U;
    usb.claim_result = LIBUSB_ERROR_IO;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(!gdox_libusb_handoff_claim_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(!state.interface_claimed);
    GDOX_TEST_CHECK(state.reattach_required);
    GDOX_TEST_CHECK(gdox_libusb_handoff_restore_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(!state.reattach_required);
    GDOX_TEST_CHECK(usb.attach_calls == 1U);
}

static void release_failure_remains_observable(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {true, true};
    gdox_libusb_handoff_result result;

    usb.active_script[0] = 0;
    usb.active_script[1] = 1;
    usb.active_count = 2U;
    usb.release_result = LIBUSB_ERROR_IO;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(!gdox_libusb_handoff_restore_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(result.phase == GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE);
    GDOX_TEST_CHECK(result.code == LIBUSB_ERROR_IO);
    GDOX_TEST_CHECK(state.interface_claimed);
    GDOX_TEST_CHECK(!state.reattach_required);
}

static void unsupported_auto_detach_uses_explicit_detach(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state;
    gdox_libusb_handoff_result result;

    usb.active_script[0] = 1;
    usb.active_script[1] = 0;
    usb.active_script[2] = 1;
    usb.active_count = 3U;
    usb.auto_detach_result = LIBUSB_ERROR_NOT_SUPPORTED;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(gdox_libusb_handoff_claim_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(usb.detach_calls == 1U);
    GDOX_TEST_CHECK(gdox_libusb_handoff_restore_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
}

static void release_no_device_is_safe_terminal_cleanup(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {true, true};
    gdox_libusb_handoff_result result;

    usb.release_result = LIBUSB_ERROR_NO_DEVICE;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(gdox_libusb_handoff_discard_with_ops(
        &usb, 0, &fake_ops, &state, &result, fake_close
    ));
    GDOX_TEST_CHECK(!state.interface_claimed);
    GDOX_TEST_CHECK(!state.reattach_required);
    GDOX_TEST_CHECK(usb.release_calls == 1U);
    GDOX_TEST_CHECK(usb.attach_calls == 0U);
    GDOX_TEST_CHECK(usb.wait_calls == 0U);
    GDOX_TEST_CHECK(usb.close_calls == 1U);
}

static void query_no_device_is_safe_terminal_cleanup(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {false, true};
    gdox_libusb_handoff_result result;

    usb.default_active = LIBUSB_ERROR_NO_DEVICE;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(gdox_libusb_handoff_discard_with_ops(
        &usb, 0, &fake_ops, &state, &result, fake_close
    ));
    GDOX_TEST_CHECK(!state.interface_claimed);
    GDOX_TEST_CHECK(!state.reattach_required);
    GDOX_TEST_CHECK(usb.attach_calls == 0U);
    GDOX_TEST_CHECK(usb.wait_calls == 0U);
    GDOX_TEST_CHECK(usb.close_calls == 1U);
}

static void not_found_retains_present_device_for_retry(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {false, true};
    gdox_libusb_handoff_result result;

    usb.attach_result = LIBUSB_ERROR_NOT_FOUND;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(!gdox_libusb_handoff_discard_with_ops(
        &usb, 0, &fake_ops, &state, &result, fake_close
    ));
    GDOX_TEST_CHECK(state.reattach_required);
    GDOX_TEST_CHECK(result.phase == GDOX_LIBUSB_HANDOFF_ATTACH_DRIVER);
    GDOX_TEST_CHECK(result.code == LIBUSB_ERROR_NOT_FOUND);
    GDOX_TEST_CHECK(usb.attach_calls == 20U);
    GDOX_TEST_CHECK(usb.close_calls == 0U);
}

static void failed_discard_retains_handle_for_retry(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state = {false, true};
    gdox_libusb_handoff_result result;

    usb.attach_result = LIBUSB_ERROR_BUSY;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(!gdox_libusb_handoff_discard_with_ops(
        &usb, 0, &fake_ops, &state, &result, fake_close
    ));
    GDOX_TEST_CHECK(state.reattach_required);
    GDOX_TEST_CHECK(usb.close_calls == 0U);

    usb.default_active = 1;
    GDOX_TEST_CHECK(gdox_libusb_handoff_discard_with_ops(
        &usb, 0, &fake_ops, &state, &result, fake_close
    ));
    GDOX_TEST_CHECK(!state.reattach_required);
    GDOX_TEST_CHECK(usb.close_calls == 1U);
}

static void tentative_unbound_candidate_is_restored(void)
{
    fake_usb usb = make_usb();
    gdox_libusb_handoff_state state;
    gdox_libusb_handoff_result result;

    usb.active_script[0] = 0;
    usb.active_script[1] = 0;
    usb.active_script[2] = 1;
    usb.active_count = 3U;
    waiting_usb = &usb;
    GDOX_TEST_CHECK(gdox_libusb_handoff_claim_with_ops(
        &usb, 0, &fake_ops, &state, &result
    ));
    GDOX_TEST_CHECK(!state.reattach_required);

    state.reattach_required = true;
    GDOX_TEST_CHECK(gdox_libusb_handoff_discard_with_ops(
        &usb, 0, &fake_ops, &state, &result, fake_close
    ));
    GDOX_TEST_CHECK(usb.release_calls == 1U);
    GDOX_TEST_CHECK(usb.attach_calls == 1U);
    GDOX_TEST_CHECK(usb.close_calls == 1U);
}

int main(void)
{
    normal_handoff_restores_driver();
    busy_is_not_success_without_verification();
    busy_succeeds_after_driver_becomes_active();
    failed_claim_still_restores_detached_driver();
    release_failure_remains_observable();
    unsupported_auto_detach_uses_explicit_detach();
    release_no_device_is_safe_terminal_cleanup();
    query_no_device_is_safe_terminal_cleanup();
    not_found_retains_present_device_for_retry();
    failed_discard_retains_handle_for_retry();
    tentative_unbound_candidate_is_restored();
    return gdox_test_failures == 0 ? 0 : 1;
}
