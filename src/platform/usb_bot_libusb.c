#if defined(__linux__) && !defined(__ANDROID__)
#define _XOPEN_SOURCE 700
#endif

#include "platform/usb_bot.h"

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
    uint16_t vendor_id;
    uint16_t product_id;
#endif
#if defined(__ANDROID__)
    bool reset_on_close;
    bool reattach_on_close;
#endif
} gdox_usb_bot_context;

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

#if defined(__linux__) && !defined(__ANDROID__)
static void discard_usb_handle(gdox_usb_bot_context *usb)
{
    if (usb->handle == NULL) {
        usb->claimed = false;
        return;
    }
    if (usb->claimed) {
        (void)libusb_release_interface(usb->handle, GDOX_USB_INTERFACE);
    }
    libusb_close(usb->handle);
    usb->handle = NULL;
    usb->claimed = false;
}

static bool reopen_bot_interface(
    gdox_usb_bot_context *usb,
    gdox_error *error
)
{
    gdox_error last;
    unsigned int attempt;

    gdox_error_clear(&last);
    discard_usb_handle(usb);
    for (attempt = 0U; attempt < GDOX_USB_REOPEN_ATTEMPTS; ++attempt) {
        usb->handle = libusb_open_device_with_vid_pid(
            usb->library,
            usb->vendor_id,
            usb->product_id
        );
        if (usb->handle != NULL) {
            if (claim_bot_interface(usb, &last)) {
                (void)fprintf(
                    stderr,
                    "GDOX: reclaimed USB optical interface after device reset\n"
                );
                (void)fflush(stderr);
                return true;
            }
            libusb_close(usb->handle);
            usb->handle = NULL;
            usb->claimed = false;
        } else {
            gdox_error_set(
                &last,
                GDOX_ERROR_NOT_FOUND,
                "supported USB optical drive is unavailable after device reset"
            );
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

static bool usb_close(void *context, gdox_error *error)
{
    gdox_usb_bot_context *usb = context;
    int release_result = LIBUSB_SUCCESS;
    int attach_result = LIBUSB_SUCCESS;
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
    if (usb->claimed) {
        release_result = libusb_release_interface(usb->handle, GDOX_USB_INTERFACE);
        usb->claimed = false;
    }
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
#if defined(__linux__) && !defined(__ANDROID__)
    if (usb->handle != NULL) {
        attach_result = libusb_attach_kernel_driver(
            usb->handle,
            GDOX_USB_INTERFACE
        );
        if (attach_result == LIBUSB_ERROR_NOT_FOUND
            || attach_result == LIBUSB_ERROR_NOT_SUPPORTED
            || attach_result == LIBUSB_ERROR_BUSY) {
            attach_result = LIBUSB_SUCCESS;
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
    if (release_result != LIBUSB_SUCCESS) {
        set_usb_error(error, "release USB mass-storage interface", release_result);
        return false;
    }
#if defined(__ANDROID__)
    if (reset_result != LIBUSB_SUCCESS) {
        set_usb_error(error, "reset Android USB optical drive", reset_result);
        return false;
    }
#endif
    if (attach_result != LIBUSB_SUCCESS) {
        set_usb_error(error, "restore USB mass-storage kernel driver", attach_result);
        return false;
    }
    return true;
}

static const gdox_scsi_transport_ops usb_ops = {
    usb_command_in,
    usb_command_none,
    reset_bot,
    usb_close,
};

#if defined(__linux__) && !defined(__ANDROID__)
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

static bool append_path(
    char *output,
    size_t capacity,
    const char *directory,
    const char *leaf
)
{
    const size_t directory_bytes = strlen(directory);
    const size_t leaf_bytes = strlen(leaf);

    if (directory_bytes + leaf_bytes >= capacity) {
        return false;
    }
    memcpy(output, directory, directory_bytes);
    memcpy(output + directory_bytes, leaf, leaf_bytes + 1U);
    return true;
}

static bool block_device_matches_usb(
    const char *block_name,
    uint16_t vendor_id,
    uint16_t product_id
)
{
    char link_path[PATH_MAX];
    char device_path[PATH_MAX];
    char vendor_path[PATH_MAX];
    char product_path[PATH_MAX];
    unsigned int observed_vendor;
    unsigned int observed_product;
    char *separator;

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
            return observed_vendor == vendor_id
                && observed_product == product_id;
        }
        separator = strrchr(device_path, '/');
        if (separator == NULL || separator == device_path) {
            break;
        }
        *separator = '\0';
    } while (device_path[0] != '\0');
    return false;
}

static void observe_linux_media(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *media_status_known,
    bool *media_present
)
{
    DIR *blocks = opendir("/sys/class/block");
    struct dirent *entry;

    if (blocks == NULL) {
        return;
    }
    while ((entry = readdir(blocks)) != NULL) {
        char device_path[PATH_MAX];
        int device;
        int status;

        if (strncmp(entry->d_name, "sr", 2U) != 0
            || !block_device_matches_usb(
                entry->d_name,
                vendor_id,
                product_id
            )) {
            continue;
        }
        (void)snprintf(
            device_path,
            sizeof(device_path),
            "/dev/%s",
            entry->d_name
        );
        device = open(device_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (device < 0) {
            break;
        }
        status = ioctl(device, CDROM_DRIVE_STATUS, CDSL_CURRENT);
        (void)close(device);
        if (status == CDS_DISC_OK) {
            *media_status_known = true;
            *media_present = true;
        } else if (status == CDS_NO_DISC
            || status == CDS_TRAY_OPEN
            || status == CDS_DRIVE_NOT_READY) {
            *media_status_known = true;
            *media_present = false;
        }
        break;
    }
    (void)closedir(blocks);
}
#endif

bool gdox_usb_bot_present(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    gdox_error *error
)
{
    libusb_context *library = NULL;
    libusb_device **devices = NULL;
    ssize_t device_count;
    ssize_t index;
    int result;

    gdox_error_clear(error);
    if (drive_present == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    *drive_present = false;
    result = libusb_init(&library);
    if (result != LIBUSB_SUCCESS) {
        set_usb_error(error, "initialize passive USB observer", result);
        return false;
    }
    device_count = libusb_get_device_list(library, &devices);
    if (device_count < 0) {
        libusb_exit(library);
        set_usb_error(
            error,
            "enumerate USB devices",
            (int)device_count
        );
        return false;
    }
    for (index = 0; index < device_count; ++index) {
        struct libusb_device_descriptor descriptor;

        result = libusb_get_device_descriptor(devices[index], &descriptor);
        if (result == LIBUSB_SUCCESS
            && descriptor.idVendor == vendor_id
            && descriptor.idProduct == product_id) {
            *drive_present = true;
            break;
        }
    }
    libusb_free_device_list(devices, 1);
    libusb_exit(library);
    return true;
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
    gdox_error_clear(error);
    if (drive_present == NULL || media_status_known == NULL
        || media_present == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical observation outputs are required"
        );
        return false;
    }
    *drive_present = false;
    *media_status_known = false;
    *media_present = false;
    if (!gdox_usb_bot_present(
            vendor_id,
            product_id,
            drive_present,
            error
        )) {
        return false;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    if (*drive_present) {
        observe_linux_media(
            vendor_id,
            product_id,
            media_status_known,
            media_present
        );
    }
#endif
    return true;
}

bool gdox_usb_bot_open(
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_usb_bot_context *usb;
    int result;

    gdox_error_clear(error);
    if (transport == NULL || gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty transport output is required"
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
    usb->vendor_id = vendor_id;
    usb->product_id = product_id;
#endif
    usb->handle = libusb_open_device_with_vid_pid(usb->library, vendor_id, product_id);
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

bool gdox_usb_bot_restore_kernel_driver(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *reattached,
    gdox_error *error
)
{
    libusb_context *library = NULL;
    libusb_device_handle *handle;
    int result;

    gdox_error_clear(error);
    if (reattached == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "reattachment output is required");
        return false;
    }
    *reattached = false;
    result = libusb_init(&library);
    if (result != LIBUSB_SUCCESS) {
        set_usb_error(error, "initialize libusb", result);
        return false;
    }
    handle = libusb_open_device_with_vid_pid(library, vendor_id, product_id);
    if (handle == NULL) {
        libusb_exit(library);
        return true;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    result = libusb_kernel_driver_active(handle, GDOX_USB_INTERFACE);
    if (result == 0) {
        result = libusb_attach_kernel_driver(handle, GDOX_USB_INTERFACE);
        if (result == LIBUSB_SUCCESS) {
            *reattached = true;
        } else if (result != LIBUSB_ERROR_NOT_FOUND
            && result != LIBUSB_ERROR_NOT_SUPPORTED
            && result != LIBUSB_ERROR_BUSY) {
            libusb_close(handle);
            libusb_exit(library);
            set_usb_error(error, "reattach USB mass-storage kernel driver", result);
            return false;
        }
    } else if (result < 0 && result != LIBUSB_ERROR_NOT_SUPPORTED) {
        libusb_close(handle);
        libusb_exit(library);
        set_usb_error(error, "query USB mass-storage kernel driver", result);
        return false;
    }
#else
    (void)result;
#endif
    libusb_close(handle);
    libusb_exit(library);
    return true;
}
