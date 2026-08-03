#if defined(__linux__) && !defined(__ANDROID__)
#define _XOPEN_SOURCE 700
#endif

#include "platform/usb_bot.h"
#include "platform/usb_bot_identity.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include "platform/usb_bot_libusb_handoff.h"
#endif

#include "portable_sync.h"

#include <libusb.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && !defined(__ANDROID__)
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define GDOX_USB_INTERFACE 0
#define GDOX_USB_BULK_IN 0x81U
#define GDOX_USB_BULK_OUT 0x02U
#define GDOX_USB_CBW_SIGNATURE UINT32_C(0x43425355)
#define GDOX_USB_CSW_SIGNATURE UINT32_C(0x53425355)
#define GDOX_USB_MAX_DATA_BYTES ((size_t)256U * 1024U)
#if defined(__linux__) && !defined(__ANDROID__)
#define GDOX_USB_REOPEN_ATTEMPTS 30U
#define GDOX_USB_REOPEN_DELAY_MS UINT32_C(100)
#endif

typedef struct gdox_usb_bot_context {
    libusb_context *library;
    libusb_device_handle *handle;
    uint32_t tag;
    bool claimed;
#if defined(__linux__) && !defined(__ANDROID__)
    gdox_usb_bot_identity identity;
    gdox_usb_bot_location location;
    gdox_libusb_handoff_state handoff;
    bool location_valid;
#endif
#if defined(__ANDROID__)
    bool reset_on_close;
    bool reattach_on_close;
#endif
} gdox_usb_bot_context;

#if defined(__linux__) && !defined(__ANDROID__)
static bool open_matching_identity(
    gdox_usb_bot_context *usb,
    gdox_usb_bot_identity identity,
    gdox_error *error
);
static bool linux_identity_location(
    gdox_usb_bot_identity identity,
    uint8_t *bus,
    uint8_t *address
);
static bool linux_block_location_present(uint8_t bus, uint8_t address);
#endif

static void put_le_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xffU);
    output[1] = (uint8_t)((value >> 8U) & 0xffU);
    output[2] = (uint8_t)((value >> 16U) & 0xffU);
    output[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_le_u32(const uint8_t *input)
{
    return (uint32_t)input[0]
        | (uint32_t)input[1] << 8U
        | (uint32_t)input[2] << 16U
        | (uint32_t)input[3] << 24U;
}

static void set_usb_error(gdox_error *error, const char *operation, int code)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    (void)snprintf(
        message,
        sizeof(message),
        "%.300s: %.70s",
        operation,
        libusb_error_name(code)
    );
    gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
}

#if defined(__linux__) && !defined(__ANDROID__)
static void set_handoff_error(
    gdox_error *error,
    const gdox_libusb_handoff_result *result
)
{
    const char *operation = "restore USB mass-storage kernel driver";

    if (result->phase == GDOX_LIBUSB_HANDOFF_QUERY_DRIVER) {
        operation = "query USB mass-storage kernel driver";
    } else if (result->phase
        == GDOX_LIBUSB_HANDOFF_ENABLE_AUTO_DETACH) {
        operation = "enable automatic USB driver handoff";
    } else if (result->phase == GDOX_LIBUSB_HANDOFF_DETACH_DRIVER) {
        operation = "detach USB mass-storage kernel driver";
    } else if (result->phase == GDOX_LIBUSB_HANDOFF_CLAIM_INTERFACE) {
        operation = "claim USB mass-storage interface";
    } else if (result->phase == GDOX_LIBUSB_HANDOFF_RELEASE_INTERFACE) {
        operation = "release USB mass-storage interface";
    }
    set_usb_error(error, operation, result->code);
}
#endif

static bool transport_is_claimed(
    const gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    if (usb->handle != NULL && usb->claimed) {
        return true;
    }
    if (usb->handle == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "USB optical drive is no longer available"
        );
        return false;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_TRANSPORT,
        "USB mass-storage interface is not claimed"
    );
    return false;
}

static bool reset_claimed_bot(
    gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    const uint8_t request_type =
        (uint8_t)LIBUSB_ENDPOINT_OUT
        | (uint8_t)LIBUSB_REQUEST_TYPE_CLASS
        | (uint8_t)LIBUSB_RECIPIENT_INTERFACE;
    int result;

    if (!transport_is_claimed(usb, error)) {
        return false;
    }
    result = libusb_control_transfer(
        usb->handle,
        request_type,
        0xffU,
        0U,
        GDOX_USB_INTERFACE,
        NULL,
        0U,
        5000U
    );
    if (result < 0) {
        set_usb_error(error, "USB Bulk-Only reset", result);
        return false;
    }
    result = libusb_clear_halt(usb->handle, GDOX_USB_BULK_IN);
    if (result != LIBUSB_SUCCESS) {
        set_usb_error(error, "clear USB bulk-in halt", result);
        return false;
    }
    result = libusb_clear_halt(usb->handle, GDOX_USB_BULK_OUT);
    if (result != LIBUSB_SUCCESS) {
        set_usb_error(error, "clear USB bulk-out halt", result);
        return false;
    }
    return true;
}

#if !defined(__linux__) || defined(__ANDROID__)
static bool claim_bot_interface(
    gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    int result;

    result = libusb_set_auto_detach_kernel_driver(usb->handle, 1);
    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_NOT_SUPPORTED) {
        set_usb_error(error, "enable automatic USB driver handoff", result);
        return false;
    }
    result = libusb_claim_interface(usb->handle, GDOX_USB_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        set_usb_error(error, "claim USB mass-storage interface", result);
        return false;
    }
    usb->claimed = true;
    if (!reset_claimed_bot(usb, error)) {
        (void)libusb_release_interface(usb->handle, GDOX_USB_INTERFACE);
        usb->claimed = false;
        return false;
    }
    usb->tag = 1U;
    return true;
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
static bool claim_bot_interface_for_identity(
    gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    gdox_libusb_handoff_result result;

    if (!gdox_libusb_handoff_claim(
            usb->handle,
            GDOX_USB_INTERFACE,
            &usb->handoff,
            &result
        )) {
        set_handoff_error(error, &result);
        return false;
    }
    usb->claimed = true;
    usb->tag = 1U;
    return true;
}

static bool discard_usb_handle(
    gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    gdox_libusb_handoff_result result;
    bool restored = true;

    if (usb->handle == NULL) {
        usb->claimed = false;
        return true;
    }
    usb->handoff.interface_claimed = usb->claimed;
    restored = gdox_libusb_handoff_discard(
        usb->handle,
        GDOX_USB_INTERFACE,
        &usb->handoff,
        &result
    );
    usb->claimed = usb->handoff.interface_claimed;
    if (!restored && error != NULL) {
        set_handoff_error(error, &result);
    }
    if (!restored) {
        return false;
    }
    usb->handle = NULL;
    usb->claimed = false;
    memset(&usb->handoff, 0, sizeof(usb->handoff));
    return restored;
}

static bool reopen_bot_interface(
    gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    gdox_error last;
    unsigned int attempt;

    gdox_error_clear(&last);
    if (!discard_usb_handle(usb, &last)) {
        *error = last;
        return false;
    }
    for (attempt = 0U; attempt < GDOX_USB_REOPEN_ATTEMPTS; ++attempt) {
        if (open_matching_identity(usb, usb->identity, &last)) {
            (void)fprintf(
                stderr,
                "GDOX: reclaimed USB optical interface after device reset\n"
            );
            (void)fflush(stderr);
            return true;
        }
        if (attempt + 1U < GDOX_USB_REOPEN_ATTEMPTS) {
            gdox_sleep_ms(GDOX_USB_REOPEN_DELAY_MS);
        }
    }
    *error = last;
    return false;
}
#endif

static bool reset_bot(void *context, gdox_error *error)
{
    gdox_usb_bot_context *usb = context;

#if defined(__linux__) && !defined(__ANDROID__)
    if (usb->handle != NULL && usb->claimed) {
        const int kernel_driver = libusb_kernel_driver_active(
            usb->handle,
            GDOX_USB_INTERFACE
        );
        if (kernel_driver == 1 || kernel_driver == LIBUSB_ERROR_NO_DEVICE) {
            return reopen_bot_interface(usb, error);
        }
    }
#endif
    if (reset_claimed_bot(usb, error)) {
        return true;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    return reopen_bot_interface(usb, error);
#else
    return false;
#endif
}

static uint32_t next_tag(gdox_usb_bot_context *usb)
{
    const uint32_t current = usb->tag;
    ++usb->tag;
    if (usb->tag == 0U) {
        usb->tag = 1U;
    }
    return current;
}

static bool send_cbw(
    gdox_usb_bot_context *usb,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t transfer_bytes,
    bool data_in,
    uint32_t timeout_ms,
    uint32_t *tag,
    gdox_error *error
)
{
    uint8_t cbw[31] = {0};
    int sent = 0;
    int result;
    char operation[GDOX_ERROR_MESSAGE_CAPACITY];

    if (!transport_is_claimed(usb, error)) {
        return false;
    }
    *tag = next_tag(usb);
    put_le_u32(cbw, GDOX_USB_CBW_SIGNATURE);
    put_le_u32(cbw + 4U, *tag);
    put_le_u32(cbw + 8U, transfer_bytes);
    cbw[12] = data_in ? 0x80U : 0U;
    cbw[13] = 0U;
    cbw[14] = (uint8_t)cdb_bytes;
    memcpy(cbw + 15U, cdb, cdb_bytes);
    result = libusb_bulk_transfer(
        usb->handle,
        GDOX_USB_BULK_OUT,
        cbw,
        (int)sizeof(cbw),
        &sent,
        timeout_ms
    );
    if (result != LIBUSB_SUCCESS) {
        (void)snprintf(operation, sizeof(operation), "send %s command wrapper", name);
        set_usb_error(error, operation, result);
        return false;
    }
    if (sent != (int)sizeof(cbw)) {
        (void)snprintf(
            operation,
            sizeof(operation),
            "%s command wrapper was short (%d of %zu bytes)",
            name,
            sent,
            sizeof(cbw)
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, operation);
        return false;
    }
    return true;
}

static bool validate_csw(
    const char *name,
    const uint8_t csw[13],
    uint32_t expected_tag,
    uint32_t *residue,
    gdox_error *error
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    const uint32_t signature = read_le_u32(csw);
    const uint32_t tag = read_le_u32(csw + 4U);

    *residue = read_le_u32(csw + 8U);
    if (signature != GDOX_USB_CSW_SIGNATURE || tag != expected_tag) {
        (void)snprintf(
            message,
            sizeof(message),
            "%s returned an invalid USB status signature or tag",
            name
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
        return false;
    }
    if (csw[12] != 0U) {
        (void)snprintf(
            message,
            sizeof(message),
            "%s returned USB command status 0x%02x with %u bytes residual",
            name,
            (unsigned int)csw[12],
            *residue
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
        return false;
    }
    return true;
}

static bool receive_csw(
    gdox_usb_bot_context *usb,
    const char *name,
    uint32_t expected_tag,
    uint32_t timeout_ms,
    uint32_t *residue,
    gdox_error *error
)
{
    uint8_t csw[13];
    int received = 0;
    int result;
    char operation[GDOX_ERROR_MESSAGE_CAPACITY];

    result = libusb_bulk_transfer(
        usb->handle,
        GDOX_USB_BULK_IN,
        csw,
        (int)sizeof(csw),
        &received,
        timeout_ms
    );
    if (result != LIBUSB_SUCCESS) {
        (void)reset_bot(usb, error);
        (void)snprintf(operation, sizeof(operation), "receive %s command status", name);
        set_usb_error(error, operation, result);
        return false;
    }
    if (received != (int)sizeof(csw)) {
        (void)snprintf(
            operation,
            sizeof(operation),
            "%s returned a short USB status (%d of %zu bytes)",
            name,
            received,
            sizeof(csw)
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, operation);
        return false;
    }
    return validate_csw(name, csw, expected_tag, residue, error);
}

static bool usb_command_in(
    void *context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    gdox_usb_bot_context *usb = context;
    uint32_t tag;
    int received = 0;
    int result;
    uint8_t csw[13];
    bool csw_in_data = false;
    uint32_t residue = 0U;
    char operation[GDOX_ERROR_MESSAGE_CAPACITY];

    if (output_bytes > GDOX_USB_MAX_DATA_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "USB data-in transfer exceeds 262,144 bytes"
        );
        return false;
    }
    if (!send_cbw(
            usb,
            name,
            cdb,
            cdb_bytes,
            (uint32_t)output_bytes,
            true,
            timeout_ms,
            &tag,
            error
        )) {
        return false;
    }
    result = libusb_bulk_transfer(
        usb->handle,
        GDOX_USB_BULK_IN,
        output,
        (int)output_bytes,
        &received,
        timeout_ms
    );
    if (result == LIBUSB_ERROR_PIPE) {
        result = libusb_clear_halt(usb->handle, GDOX_USB_BULK_IN);
        if (result != LIBUSB_SUCCESS) {
            set_usb_error(error, "clear failed USB data-in phase", result);
            return false;
        }
        received = 0;
    } else if (result != LIBUSB_SUCCESS) {
        gdox_error ignored;
        (void)reset_bot(usb, &ignored);
        (void)snprintf(operation, sizeof(operation), "receive %s data", name);
        set_usb_error(error, operation, result);
        return false;
    }
    if (received == (int)sizeof(csw) && output_bytes >= sizeof(csw)
        && read_le_u32(output) == GDOX_USB_CSW_SIGNATURE
        && read_le_u32(output + 4U) == tag) {
        memcpy(csw, output, sizeof(csw));
        csw_in_data = true;
    } else if (!receive_csw(usb, name, tag, timeout_ms, &residue, error)) {
        return false;
    }
    if (csw_in_data && !validate_csw(name, csw, tag, &residue, error)) {
        return false;
    }
    if (residue > output_bytes) {
        *transferred = 0U;
    } else {
        const size_t available = output_bytes - residue;
        *transferred = (size_t)received < available ? (size_t)received : available;
    }
    if (csw_in_data) {
        *transferred = 0U;
    }
    return true;
}

static bool usb_command_out(
    void *context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    const uint8_t *input,
    size_t input_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    gdox_usb_bot_context *usb = context;
    uint32_t tag;
    int sent = 0;
    int result;
    uint32_t residue = 0U;
    char operation[GDOX_ERROR_MESSAGE_CAPACITY];

    if (input_bytes > GDOX_USB_MAX_DATA_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "USB data-out transfer exceeds 262,144 bytes"
        );
        return false;
    }
    if (!send_cbw(
            usb,
            name,
            cdb,
            cdb_bytes,
            (uint32_t)input_bytes,
            false,
            timeout_ms,
            &tag,
            error
        )) {
        return false;
    }
    result = libusb_bulk_transfer(
        usb->handle,
        GDOX_USB_BULK_OUT,
        (uint8_t *)input,
        (int)input_bytes,
        &sent,
        timeout_ms
    );
    if (result == LIBUSB_ERROR_PIPE) {
        result = libusb_clear_halt(usb->handle, GDOX_USB_BULK_OUT);
        if (result != LIBUSB_SUCCESS) {
            set_usb_error(error, "clear failed USB data-out phase", result);
            return false;
        }
        sent = 0;
    } else if (result != LIBUSB_SUCCESS) {
        gdox_error ignored;
        (void)reset_bot(usb, &ignored);
        (void)snprintf(operation, sizeof(operation), "send %s data", name);
        set_usb_error(error, operation, result);
        return false;
    }
    if (!receive_csw(usb, name, tag, timeout_ms, &residue, error)) {
        return false;
    }
    if (residue > input_bytes) {
        *transferred = 0U;
    } else {
        const size_t accepted = input_bytes - residue;
        *transferred = (size_t)sent < accepted ? (size_t)sent : accepted;
    }
    return true;
}

static bool usb_command_none(
    void *context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    gdox_usb_bot_context *usb = context;
    uint32_t tag;
    uint32_t residue = 0U;

    if (!send_cbw(
            usb,
            name,
            cdb,
            cdb_bytes,
            0U,
            false,
            timeout_ms,
            &tag,
            error
        )) {
        return false;
    }
    if (!receive_csw(usb, name, tag, timeout_ms, &residue, error)) {
        return false;
    }
    if (residue != 0U) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "no-data command returned residual bytes");
        return false;
    }
    return true;
}

static bool usb_prepare_close(void *context, gdox_error *error)
{
    gdox_usb_bot_context *usb = context;
#if defined(__linux__) && !defined(__ANDROID__)
    gdox_libusb_handoff_result handoff_result;

    if (usb->handle == NULL) {
        return true;
    }
    usb->handoff.interface_claimed = usb->claimed;
    if (!gdox_libusb_handoff_restore(
            usb->handle,
            GDOX_USB_INTERFACE,
            &usb->handoff,
            &handoff_result
        )) {
        usb->claimed = usb->handoff.interface_claimed;
        set_handoff_error(error, &handoff_result);
        return false;
    }
    usb->claimed = false;
#else
    (void)usb;
    (void)error;
#endif
    return true;
}

static bool usb_close(void *context, gdox_error *error)
{
    gdox_usb_bot_context *usb = context;
#if !defined(__linux__) || defined(__ANDROID__)
    int release_result = LIBUSB_SUCCESS;
#else
    (void)error;
#endif
#if defined(__ANDROID__)
    int auto_detach_result;
    int reset_result;
#endif

#if defined(__ANDROID__)
    auto_detach_result = LIBUSB_SUCCESS;
    if (usb->claimed && !usb->reattach_on_close) {
        auto_detach_result =
            libusb_set_auto_detach_kernel_driver(usb->handle, 0);
        if (auto_detach_result == LIBUSB_ERROR_NOT_SUPPORTED) {
            auto_detach_result = LIBUSB_SUCCESS;
        }
    }
#endif
#if !defined(__linux__) || defined(__ANDROID__)
    if (usb->claimed) {
        release_result = libusb_release_interface(usb->handle, GDOX_USB_INTERFACE);
        usb->claimed = false;
    }
#endif
#if defined(__ANDROID__)
    /*
     * Live sessions reset the mechanism while the authorized descriptor is
     * still alive. Passive observers deliberately skip that reset so their
     * successor can claim the same enumerated device immediately.
     */
    reset_result = LIBUSB_SUCCESS;
    if (usb->reset_on_close) {
        reset_result = libusb_reset_device(usb->handle);
        if (reset_result == LIBUSB_ERROR_NOT_FOUND) {
            reset_result = LIBUSB_SUCCESS;
        }
    }
#endif
    if (usb->handle != NULL) {
        libusb_close(usb->handle);
    }
    libusb_exit(usb->library);
    free(usb);
#if defined(__ANDROID__)
    if (auto_detach_result != LIBUSB_SUCCESS) {
        set_usb_error(
            error,
            "disable automatic Android USB driver reattachment",
            auto_detach_result
        );
        return false;
    }
#endif
#if !defined(__linux__) || defined(__ANDROID__)
    if (release_result != LIBUSB_SUCCESS) {
        set_usb_error(error, "release USB mass-storage interface", release_result);
        return false;
    }
#endif
#if defined(__ANDROID__)
    if (reset_result != LIBUSB_SUCCESS) {
        set_usb_error(error, "reset Android USB optical drive", reset_result);
        return false;
    }
#endif
    return true;
}

static const gdox_scsi_transport_ops usb_ops = {
    usb_command_in,
    usb_command_out,
    usb_command_none,
    reset_bot,
    usb_close,
    usb_prepare_close,
    NULL,
};

#if defined(__linux__) && !defined(__ANDROID__)
static bool inquiry_field_copy(
    const uint8_t *field,
    size_t field_bytes,
    char *output,
    size_t output_bytes
)
{
    size_t begin = 0U;
    size_t end = field_bytes;

    while (begin < end && (field[begin] == 0U || field[begin] == ' ')) {
        ++begin;
    }
    while (end > begin
        && (field[end - 1U] == 0U || field[end - 1U] == ' ')) {
        --end;
    }
    if (end - begin >= output_bytes) {
        return false;
    }
    memcpy(output, field + begin, end - begin);
    output[end - begin] = '\0';
    return true;
}

static bool usb_device_location(
    libusb_device *device,
    gdox_usb_bot_location *location
)
{
    int port_count;

    memset(location, 0, sizeof(*location));
    location->bus = libusb_get_bus_number(device);
    location->address = libusb_get_device_address(device);
    port_count = libusb_get_port_numbers(
        device,
        location->ports,
        sizeof(location->ports)
    );
    if (port_count < 0) {
        return location->bus != 0U && location->address != 0U;
    }
    location->port_count = (size_t)port_count;
    return location->bus != 0U
        && (location->address != 0U || location->port_count != 0U);
}

static bool claimed_candidate_matches(
    gdox_usb_bot_context *usb,
    gdox_usb_bot_identity requested,
    const gdox_usb_bot_location *expected_location,
    const gdox_usb_bot_location *observed_location,
    gdox_error *error
)
{
    static const uint8_t cdb[6] = {0x12U, 0U, 0U, 0U, 96U, 0U};
    uint8_t response[96] = {0};
    size_t transferred = 0U;
    struct libusb_device_descriptor descriptor;
    char vendor[9];
    char model[17];
    char revision[5];
    gdox_usb_bot_observed_identity observed;

    if (libusb_get_device_descriptor(
            libusb_get_device(usb->handle),
            &descriptor
        ) != LIBUSB_SUCCESS
        || !usb_command_in(
            usb,
            "INQUIRY",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            UINT32_C(5000),
            &transferred,
            error
        )
        || transferred < 36U
        || !inquiry_field_copy(response + 8U, 8U, vendor, sizeof(vendor))
        || !inquiry_field_copy(response + 16U, 16U, model, sizeof(model))
        || !inquiry_field_copy(
            response + 32U,
            4U,
            revision,
            sizeof(revision)
        )) {
        return false;
    }
    observed = (gdox_usb_bot_observed_identity){
        descriptor.idVendor,
        descriptor.idProduct,
        vendor,
        model,
        revision,
    };
    return gdox_usb_bot_candidate_matches(
        requested,
        expected_location,
        &observed,
        observed_location
    );
}

static bool open_matching_identity(
    gdox_usb_bot_context *usb,
    gdox_usb_bot_identity requested,
    gdox_error *error
)
{
    const gdox_usb_bot_identity_spec *identity =
        gdox_usb_bot_identity_get(requested);
    libusb_device **devices = NULL;
    ssize_t device_count;
    ssize_t index;
    gdox_usb_bot_location expected_location;
    gdox_error candidate_failure;
    bool expected_location_valid;

    if (identity == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "libusb transport does not support this optical mechanism"
        );
        return false;
    }
    gdox_error_clear(&candidate_failure);
    if (usb->location_valid) {
        expected_location = usb->location;
        expected_location_valid = true;
    } else {
        memset(&expected_location, 0, sizeof(expected_location));
        expected_location_valid = linux_identity_location(
            requested,
            &expected_location.bus,
            &expected_location.address
        );
    }
    device_count = libusb_get_device_list(usb->library, &devices);
    if (device_count < 0) {
        set_usb_error(error, "enumerate USB optical drives", (int)device_count);
        return false;
    }
    for (index = 0; index < device_count; ++index) {
        struct libusb_device_descriptor descriptor;
        gdox_usb_bot_location observed_location;
        gdox_error candidate_error;
        int open_result;

        gdox_error_clear(&candidate_error);
        if (!usb_device_location(devices[index], &observed_location)
            || (expected_location_valid
                && !gdox_usb_bot_location_matches(
                    &expected_location,
                    &observed_location
                ))
            || (!expected_location_valid
                && linux_block_location_present(
                    observed_location.bus,
                    observed_location.address
                ))
            || libusb_get_device_descriptor(devices[index], &descriptor)
                != LIBUSB_SUCCESS
            || descriptor.idVendor != identity->vendor_id
            || descriptor.idProduct != identity->product_id) {
            continue;
        }
        open_result = libusb_open(devices[index], &usb->handle);
        if (open_result != LIBUSB_SUCCESS) {
            set_usb_error(
                &candidate_failure,
                "open selected USB optical drive",
                open_result
            );
            break;
        }
        if (claim_bot_interface_for_identity(usb, &candidate_error)) {
            /*
             * A supported USB descriptor may already be unbound after an
             * interrupted prior session. Restore usb-storage even when SCSI
             * identity rejects a tentative shared-ID recovery candidate.
             */
            usb->handoff.reattach_required = true;
            if (claimed_candidate_matches(
                    usb,
                    requested,
                    expected_location_valid ? &expected_location : NULL,
                    &observed_location,
                    &candidate_error
                )
                && reset_claimed_bot(usb, &candidate_error)) {
                usb->identity = requested;
                usb->location = observed_location;
                usb->location_valid = true;
                libusb_free_device_list(devices, 1);
                return true;
            }
        }
        if (gdox_error_is_set(&candidate_error)) {
            candidate_failure = candidate_error;
        } else {
            gdox_error_set(
                &candidate_failure,
                GDOX_ERROR_UNSUPPORTED,
                "selected USB optical drive failed exact identity validation"
            );
        }
        if (!discard_usb_handle(usb, &candidate_error)) {
            candidate_failure = candidate_error;
        }
        break;
    }
    libusb_free_device_list(devices, 1);
    if (gdox_error_is_set(&candidate_failure)) {
        *error = candidate_failure;
        return false;
    }
    gdox_error_set(
        error,
        GDOX_ERROR_NOT_FOUND,
        "selected USB optical drive was not found"
    );
    return false;
}

static bool read_hex_identifier(const char *path, unsigned int *value)
{
    FILE *input = fopen(path, "r");
    char text[32];
    char *end = NULL;
    unsigned long parsed = 0U;
    bool read = false;

    if (input != NULL) {
        if (fgets(text, sizeof(text), input) != NULL) {
            errno = 0;
            parsed = strtoul(text, &end, 16);
            read = end != text
                && errno == 0
                && parsed <= UINT_MAX
                && (*end == '\0' || (*end == '\n' && end[1] == '\0'));
            if (read) {
                *value = (unsigned int)parsed;
            }
        }
        (void)fclose(input);
    }
    return read;
}

static bool read_decimal_identifier(const char *path, unsigned int *value)
{
    FILE *input = fopen(path, "r");
    char text[32];
    char *end = NULL;
    unsigned long parsed = 0U;
    bool read = false;

    if (input != NULL) {
        if (fgets(text, sizeof(text), input) != NULL) {
            errno = 0;
            parsed = strtoul(text, &end, 10);
            read = end != text
                && errno == 0
                && parsed <= UINT_MAX
                && (*end == '\0' || (*end == '\n' && end[1] == '\0'));
            if (read) {
                *value = (unsigned int)parsed;
            }
        }
        (void)fclose(input);
    }
    return read;
}

static bool append_path(
    char *output,
    size_t capacity,
    const char *directory,
    const char *leaf
)
{
    int bytes;

    if (output == NULL || capacity == 0U || directory == NULL
        || leaf == NULL) {
        return false;
    }
    bytes = snprintf(output, capacity, "%s%s", directory, leaf);
    return bytes >= 0 && (size_t)bytes < capacity;
}

static bool read_text(const char *path, char *output, size_t capacity)
{
    FILE *input = fopen(path, "r");
    size_t length;

    if (input == NULL || capacity == 0U
        || fgets(output, (int)capacity, input) == NULL) {
        if (input != NULL) {
            (void)fclose(input);
        }
        return false;
    }
    (void)fclose(input);
    length = strlen(output);
    while (length > 0U
        && (output[length - 1U] == '\n'
            || output[length - 1U] == '\r'
            || output[length - 1U] == ' ')) {
        output[--length] = '\0';
    }
    return true;
}

static bool block_device_supported_identity(
    const char *block_name,
    gdox_usb_bot_identity *identity
)
{
    char link_path[PATH_MAX];
    char device_path[PATH_MAX];
    char vendor_path[PATH_MAX];
    char product_path[PATH_MAX];
    char scsi_vendor_path[PATH_MAX];
    char model_path[PATH_MAX];
    char revision_path[PATH_MAX];
    char scsi_vendor[32];
    char model[32];
    char revision[32];
    unsigned int observed_vendor;
    unsigned int observed_product;
    char *separator;
    size_t identity_index;

    if (identity == NULL) {
        return false;
    }
    (void)snprintf(
        link_path,
        sizeof(link_path),
        "/sys/class/block/%s/device",
        block_name
    );
    if (realpath(link_path, device_path) == NULL) {
        return false;
    }
    if (!append_path(
            scsi_vendor_path,
            sizeof(scsi_vendor_path),
            device_path,
            "/vendor"
        )
        || !append_path(
            model_path,
            sizeof(model_path),
            device_path,
            "/model"
        )
        || !append_path(
            revision_path,
            sizeof(revision_path),
            device_path,
            "/rev"
        )
        || !read_text(scsi_vendor_path, scsi_vendor, sizeof(scsi_vendor))
        || !read_text(model_path, model, sizeof(model))
        || !read_text(revision_path, revision, sizeof(revision))) {
        return false;
    }
    do {
        if (append_path(
                vendor_path,
                sizeof(vendor_path),
                device_path,
                "/idVendor"
            )
            && append_path(
                product_path,
                sizeof(product_path),
                device_path,
                "/idProduct"
            )
            && read_hex_identifier(vendor_path, &observed_vendor)
            && read_hex_identifier(product_path, &observed_product)) {
            const gdox_usb_bot_observed_identity observed = {
                (uint16_t)observed_vendor,
                (uint16_t)observed_product,
                scsi_vendor,
                model,
                revision,
            };
            if (observed_vendor > UINT16_MAX
                || observed_product > UINT16_MAX) {
                return false;
            }
            for (identity_index = 0U;
                 identity_index < GDOX_USB_BOT_IDENTITY_COUNT;
                 ++identity_index) {
                const gdox_usb_bot_identity candidate =
                    (gdox_usb_bot_identity)identity_index;
                if (gdox_usb_bot_identity_matches(candidate, &observed)) {
                    *identity = candidate;
                    return true;
                }
            }
            return false;
        }
        separator = strrchr(device_path, '/');
        if (separator == NULL || separator == device_path) {
            break;
        }
        *separator = '\0';
    } while (device_path[0] != '\0');
    return false;
}

static bool block_device_matches_identity(
    const char *block_name,
    gdox_usb_bot_identity requested
)
{
    gdox_usb_bot_identity observed;
    return block_device_supported_identity(block_name, &observed)
        && observed == requested;
}

static bool observe_unbound_usb_candidates(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
);

static bool observe_linux_devices(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    bool query_media,
    gdox_error *error
)
{
    DIR *blocks = opendir("/sys/class/block");
    struct dirent *entry;

    if (blocks == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "could not enumerate Linux optical drives"
        );
        return false;
    }
    while ((entry = readdir(blocks)) != NULL) {
        gdox_usb_bot_identity identity;
        gdox_usb_bot_observation *observation;
        char device_path[PATH_MAX];
        int device;
        int status;

        if (strncmp(entry->d_name, "sr", 2U) != 0
            || !block_device_supported_identity(entry->d_name, &identity)) {
            continue;
        }
        observation = &observations[(size_t)identity];
        observation->drive_present = true;
        if (!query_media) {
            continue;
        }
        (void)snprintf(
            device_path,
            sizeof(device_path),
            "/dev/%s",
            entry->d_name
        );
        device = open(
            device_path,
            O_RDONLY | O_NONBLOCK | O_CLOEXEC
        );
        if (device < 0) {
            continue;
        }
        status = ioctl(device, CDROM_DRIVE_STATUS, CDSL_CURRENT);
        (void)close(device);
        if (status == CDS_DISC_OK) {
            observation->media_status_known = true;
            observation->media_present = true;
        } else if (status == CDS_NO_DISC
            || status == CDS_TRAY_OPEN
            || status == CDS_DRIVE_NOT_READY) {
            observation->media_status_known = true;
        }
    }
    (void)closedir(blocks);
    return observe_unbound_usb_candidates(observations, error);
}

static bool block_device_usb_location(
    const char *block_name,
    uint8_t *bus,
    uint8_t *address
)
{
    char link_path[PATH_MAX];
    char device_path[PATH_MAX];

    if (block_name == NULL || bus == NULL || address == NULL) {
        return false;
    }
    (void)snprintf(
        link_path,
        sizeof(link_path),
        "/sys/class/block/%s/device",
        block_name
    );
    if (realpath(link_path, device_path) == NULL) {
        return false;
    }
    do {
        char bus_path[PATH_MAX];
        char address_path[PATH_MAX];
        unsigned int observed_bus;
        unsigned int observed_address;
        char *separator;

        if (append_path(
                bus_path, sizeof(bus_path), device_path, "/busnum"
            )
            && append_path(
                address_path,
                sizeof(address_path),
                device_path,
                "/devnum"
            )
            && read_decimal_identifier(bus_path, &observed_bus)
            && read_decimal_identifier(address_path, &observed_address)
            && observed_bus <= UINT8_MAX
            && observed_address <= UINT8_MAX) {
            *bus = (uint8_t)observed_bus;
            *address = (uint8_t)observed_address;
            return true;
        }
        separator = strrchr(device_path, '/');
        if (separator == NULL || separator == device_path) {
            break;
        }
        *separator = '\0';
    } while (device_path[0] != '\0');
    return false;
}

static bool linux_block_location_present(uint8_t bus, uint8_t address)
{
    DIR *blocks = opendir("/sys/class/block");
    struct dirent *entry;
    bool found = false;

    if (blocks == NULL) {
        return false;
    }
    while ((entry = readdir(blocks)) != NULL) {
        uint8_t observed_bus;
        uint8_t observed_address;

        if (strncmp(entry->d_name, "sr", 2U) == 0
            && block_device_usb_location(
                entry->d_name, &observed_bus, &observed_address
            )
            && observed_bus == bus
            && observed_address == address) {
            found = true;
            break;
        }
    }
    (void)closedir(blocks);
    return found;
}

static bool linux_identity_location(
    gdox_usb_bot_identity identity,
    uint8_t *bus,
    uint8_t *address
)
{
    DIR *blocks = opendir("/sys/class/block");
    struct dirent *entry;
    bool found = false;

    if (blocks == NULL || bus == NULL || address == NULL) {
        if (blocks != NULL) {
            (void)closedir(blocks);
        }
        return false;
    }
    while ((entry = readdir(blocks)) != NULL) {
        if (strncmp(entry->d_name, "sr", 2U) == 0
            && block_device_matches_identity(entry->d_name, identity)
            && block_device_usb_location(entry->d_name, bus, address)) {
            found = true;
            break;
        }
    }
    (void)closedir(blocks);
    return found;
}

static bool observe_unbound_usb_candidates(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    libusb_context *library = NULL;
    libusb_device **devices = NULL;
    ssize_t device_count;
    ssize_t index;
    int result;

    result = libusb_init(&library);
    if (result != LIBUSB_SUCCESS) {
        set_usb_error(error, "initialize USB recovery observation", result);
        return false;
    }
    device_count = libusb_get_device_list(library, &devices);
    if (device_count < 0) {
        libusb_exit(library);
        set_usb_error(
            error,
            "enumerate USB recovery candidates",
            (int)device_count
        );
        return false;
    }
    for (index = 0; index < device_count; ++index) {
        struct libusb_device_descriptor descriptor;
        gdox_usb_bot_identity identity;
        uint8_t bus;
        uint8_t address;

        if (libusb_get_device_descriptor(devices[index], &descriptor)
                != LIBUSB_SUCCESS
            || !gdox_usb_bot_recovery_identity(
                descriptor.idVendor, descriptor.idProduct, &identity
            )) {
            continue;
        }
        bus = libusb_get_bus_number(devices[index]);
        address = libusb_get_device_address(devices[index]);
        if (bus == 0U || address == 0U
            || linux_block_location_present(bus, address)) {
            continue;
        }
        observations[(size_t)identity].drive_present = true;
    }
    libusb_free_device_list(devices, 1);
    libusb_exit(library);
    return true;
}

#endif

bool gdox_usb_bot_observe_all(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (observations == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical observations output is required"
        );
        return false;
    }
    memset(
        observations,
        0,
        sizeof(*observations) * GDOX_USB_BOT_IDENTITY_COUNT
    );
#if defined(__linux__) && !defined(__ANDROID__)
    return observe_linux_devices(observations, true, error);
#else
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "passive exact-identity observation is unavailable on this libusb platform"
    );
    return false;
#endif
}

bool gdox_usb_bot_present_all(
    bool drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    gdox_usb_bot_observation
        observations[GDOX_USB_BOT_IDENTITY_COUNT] = {0};
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
    memset(
        drive_present,
        0,
        sizeof(*drive_present) * GDOX_USB_BOT_IDENTITY_COUNT
    );
#if defined(__linux__) && !defined(__ANDROID__)
    if (!observe_linux_devices(observations, false, error)) {
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        drive_present[index] = observations[index].drive_present;
    }
    return true;
#else
    (void)observations;
    (void)index;
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "passive exact-identity observation is unavailable on this libusb platform"
    );
    return false;
#endif
}

bool gdox_usb_bot_open(
    gdox_usb_bot_identity identity,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_usb_bot_context *usb;
    const gdox_usb_bot_identity_spec *selected =
        gdox_usb_bot_identity_get(identity);
    int result;

    gdox_error_clear(error);
    if (transport == NULL || gdox_scsi_transport_is_valid(transport)
        || selected == NULL) {
        gdox_error_set(
            error,
            transport == NULL || gdox_scsi_transport_is_valid(transport)
                ? GDOX_ERROR_INVALID_ARGUMENT
                : GDOX_ERROR_UNSUPPORTED,
            transport == NULL || gdox_scsi_transport_is_valid(transport)
                ? "an empty transport output is required"
                : "libusb transport does not support this optical mechanism"
        );
        return false;
    }
    usb = calloc(1U, sizeof(*usb));
    if (usb == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate USB transport");
        return false;
    }
    result = libusb_init(&usb->library);
    if (result != LIBUSB_SUCCESS) {
        free(usb);
        set_usb_error(error, "initialize libusb", result);
        return false;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    if (!open_matching_identity(usb, identity, error)) {
        if (usb->handle != NULL) {
            transport->context = usb;
            transport->ops = &usb_ops;
            return false;
        }
        libusb_exit(usb->library);
        free(usb);
        return false;
    }
#else
    usb->handle = libusb_open_device_with_vid_pid(
        usb->library,
        selected->vendor_id,
        selected->product_id
    );
    if (usb->handle == NULL) {
        libusb_exit(usb->library);
        free(usb);
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "supported USB optical drive was not found");
        return false;
    }
    if (!claim_bot_interface(usb, error)) {
        libusb_close(usb->handle);
        libusb_exit(usb->library);
        free(usb);
        return false;
    }
#endif
    transport->context = usb;
    transport->ops = &usb_ops;
    return true;
}

#if defined(__ANDROID__)
static bool open_android_file_descriptor(
    int file_descriptor,
    uint16_t vendor_id,
    uint16_t product_id,
    bool reset_on_close,
    bool reattach_on_close,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    const struct libusb_init_option no_discovery = {
        .option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY,
        .value.ival = 1,
    };
    gdox_usb_bot_context *usb;
    struct libusb_device_descriptor descriptor;
    int result;

    gdox_error_clear(error);
    if (file_descriptor < 0 || transport == NULL
        || gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an Android USB file descriptor and empty transport are required"
        );
        return false;
    }
    usb = calloc(1U, sizeof(*usb));
    if (usb == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Android USB transport"
        );
        return false;
    }
    result = libusb_init_context(&usb->library, &no_discovery, 1);
    if (result != LIBUSB_SUCCESS) {
        free(usb);
        set_usb_error(error, "initialize Android libusb context", result);
        return false;
    }
    result = libusb_wrap_sys_device(
        usb->library,
        (intptr_t)file_descriptor,
        &usb->handle
    );
    if (result != LIBUSB_SUCCESS) {
        libusb_exit(usb->library);
        free(usb);
        set_usb_error(error, "wrap Android USB device", result);
        return false;
    }
    result = libusb_get_device_descriptor(
        libusb_get_device(usb->handle),
        &descriptor
    );
    if (result != LIBUSB_SUCCESS) {
        libusb_close(usb->handle);
        libusb_exit(usb->library);
        free(usb);
        set_usb_error(error, "read Android USB device identity", result);
        return false;
    }
    if (descriptor.idVendor != vendor_id
        || descriptor.idProduct != product_id) {
        libusb_close(usb->handle);
        libusb_exit(usb->library);
        free(usb);
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Android USB descriptor does not identify the supported drive"
        );
        return false;
    }
    usb->reset_on_close = reset_on_close;
    usb->reattach_on_close = reattach_on_close;
    if (!claim_bot_interface(usb, error)) {
        libusb_close(usb->handle);
        libusb_exit(usb->library);
        free(usb);
        return false;
    }
    transport->context = usb;
    transport->ops = &usb_ops;
    return true;
}

bool gdox_usb_bot_open_file_descriptor(
    int file_descriptor,
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    return open_android_file_descriptor(
        file_descriptor,
        vendor_id,
        product_id,
        false,
        false,
        transport,
        error
    );
}

bool gdox_usb_bot_open_observer_file_descriptor(
    int file_descriptor,
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    return open_android_file_descriptor(
        file_descriptor,
        vendor_id,
        product_id,
        false,
        true,
        transport,
        error
    );
}

bool gdox_usb_bot_prepare_handoff(
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_usb_bot_context *usb;

    gdox_error_clear(error);
    if (!gdox_scsi_transport_is_valid(transport)
        || transport->ops != &usb_ops) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open Android USB transport is required for handoff"
        );
        return false;
    }
    usb = transport->context;
    usb->reset_on_close = false;
    usb->reattach_on_close = false;
    return true;
}
#endif
