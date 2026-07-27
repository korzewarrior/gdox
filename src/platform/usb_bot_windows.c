#define WIN32_LEAN_AND_MEAN

#include "platform/usb_bot.h"

#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_CDROM_LIMIT 32U
#define GDOX_WINDOWS_SENSE_BYTES 32U

typedef struct gdox_windows_scsi_context {
    HANDLE device;
} gdox_windows_scsi_context;

typedef struct gdox_windows_scsi_packet {
    SCSI_PASS_THROUGH_DIRECT command;
    UCHAR sense[GDOX_WINDOWS_SENSE_BYTES];
} gdox_windows_scsi_packet;

static void set_windows_transport_error(
    gdox_error *error,
    const char *operation,
    DWORD code
)
{
    char detail[160] = {0};
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        0U,
        detail,
        (DWORD)sizeof(detail),
        NULL
    );

    while (length > 0U
        && (detail[length - 1U] == '\r'
            || detail[length - 1U] == '\n'
            || detail[length - 1U] == ' ')) {
        detail[--length] = '\0';
    }
    (void)snprintf(
        message,
        sizeof(message),
        "%s: %s (Windows error %lu)",
        operation,
        length != 0U ? detail : "optical command failed",
        (unsigned long)code
    );
    gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
}

static void set_scsi_error(
    gdox_error *error,
    const char *name,
    const gdox_windows_scsi_packet *packet
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    const unsigned int sense_key =
        (unsigned int)(packet->sense[2] & 0x0fU);
    const unsigned int additional_code = packet->sense[12];
    const unsigned int qualifier = packet->sense[13];

    (void)snprintf(
        message,
        sizeof(message),
        "%s returned SCSI status 0x%02x (%02x/%02x/%02x)",
        name,
        (unsigned int)packet->command.ScsiStatus,
        sense_key,
        additional_code,
        qualifier
    );
    gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
}

static bool execute_command(
    HANDLE device,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint8_t data_direction,
    uint8_t *data,
    size_t data_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_windows_scsi_packet *result,
    gdox_error *error
)
{
    gdox_windows_scsi_packet packet = {0};
    DWORD returned = 0U;

    if (data_bytes > ULONG_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Windows optical transfer is too large"
        );
        return false;
    }
    packet.command.Length = sizeof(packet.command);
    packet.command.CdbLength = (UCHAR)cdb_bytes;
    packet.command.SenseInfoLength = sizeof(packet.sense);
    packet.command.DataIn = data_direction;
    packet.command.DataTransferLength = (ULONG)data_bytes;
    packet.command.TimeOutValue =
        (ULONG)((timeout_ms + UINT32_C(999)) / UINT32_C(1000));
    if (packet.command.TimeOutValue == 0U) {
        packet.command.TimeOutValue = 1U;
    }
    packet.command.DataBuffer = data;
    packet.command.SenseInfoOffset =
        (ULONG)offsetof(gdox_windows_scsi_packet, sense);
    memcpy(packet.command.Cdb, cdb, cdb_bytes);
    if (!DeviceIoControl(
            device,
            IOCTL_SCSI_PASS_THROUGH_DIRECT,
            &packet,
            sizeof(packet),
            &packet,
            sizeof(packet),
            &returned,
            NULL
        )) {
        set_windows_transport_error(error, name, GetLastError());
        return false;
    }
    if (result != NULL) {
        *result = packet;
    }
    if (packet.command.ScsiStatus != 0U) {
        set_scsi_error(error, name, &packet);
        return false;
    }
    if (transferred != NULL) {
        *transferred = packet.command.DataTransferLength;
    }
    return true;
}

static bool windows_command_in(
    void *raw_context,
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
    const gdox_windows_scsi_context *context = raw_context;
    return execute_command(
        context->device,
        name,
        cdb,
        cdb_bytes,
        SCSI_IOCTL_DATA_IN,
        output,
        output_bytes,
        timeout_ms,
        transferred,
        NULL,
        error
    );
}

static bool windows_command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    const gdox_windows_scsi_context *context = raw_context;
    return execute_command(
        context->device,
        name,
        cdb,
        cdb_bytes,
        SCSI_IOCTL_DATA_UNSPECIFIED,
        NULL,
        0U,
        timeout_ms,
        NULL,
        NULL,
        error
    );
}

static bool windows_reset(void *raw_context, gdox_error *error)
{
    (void)raw_context;
    gdox_error_clear(error);
    /*
     * Windows' class driver owns USB recovery. The bounded caller sequence
     * clears sense and restarts the optical unit without resetting the USB
     * device out from under the operating system.
     */
    return true;
}

static bool windows_close(void *raw_context, gdox_error *error)
{
    gdox_windows_scsi_context *context = raw_context;
    const BOOL closed = CloseHandle(context->device);
    const DWORD code = closed ? ERROR_SUCCESS : GetLastError();

    free(context);
    if (!closed) {
        set_windows_transport_error(
            error,
            "close the Windows optical command channel",
            code
        );
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static const gdox_scsi_transport_ops windows_ops = {
    windows_command_in,
    windows_command_none,
    windows_reset,
    windows_close,
};

static bool descriptor_string_matches(
    const uint8_t *descriptor,
    size_t descriptor_bytes,
    DWORD offset,
    const char *expected
)
{
    const char *value;
    const char *terminator;
    size_t value_bytes;
    const size_t expected_bytes = strlen(expected);
    const size_t remaining =
        offset < descriptor_bytes ? descriptor_bytes - offset : 0U;

    if (offset == 0U || remaining == 0U) {
        return false;
    }
    value = (const char *)descriptor + offset;
    terminator = memchr(value, '\0', remaining);
    if (terminator == NULL) {
        return false;
    }
    value_bytes = (size_t)(terminator - value);
    while (value_bytes > 0U && value[value_bytes - 1U] == ' ') {
        --value_bytes;
    }
    return value_bytes == expected_bytes
        && memcmp(value, expected, expected_bytes) == 0;
}

static bool device_identity_matches(HANDLE device)
{
    STORAGE_PROPERTY_QUERY query = {
        StorageDeviceProperty,
        PropertyStandardQuery,
        {0},
    };
    uint8_t buffer[1024] = {0};
    DWORD returned = 0U;
    const STORAGE_DEVICE_DESCRIPTOR *descriptor =
        (const STORAGE_DEVICE_DESCRIPTOR *)buffer;

    return DeviceIoControl(
            device,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            buffer,
            sizeof(buffer),
            &returned,
            NULL
        )
        && returned >= sizeof(*descriptor)
        && descriptor_string_matches(
            buffer,
            returned,
            descriptor->VendorIdOffset,
            "HL-DT-ST"
        )
        && descriptor_string_matches(
            buffer,
            returned,
            descriptor->ProductIdOffset,
            "DVDRAM GP63EX70"
        )
        && descriptor_string_matches(
            buffer,
            returned,
            descriptor->ProductRevisionOffset,
            "RF02"
        );
}

static HANDLE open_validated_device(DWORD access)
{
    unsigned int index;

    for (index = 0U; index < GDOX_WINDOWS_CDROM_LIMIT; ++index) {
        wchar_t path[32];
        HANDLE device;

        if (swprintf(
                path,
                sizeof(path) / sizeof(path[0]),
                L"\\\\.\\CdRom%u",
                index
            ) < 0) {
            continue;
        }
        device = CreateFileW(
            path,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (device == INVALID_HANDLE_VALUE) {
            continue;
        }
        if (device_identity_matches(device)) {
            return device;
        }
        (void)CloseHandle(device);
    }
    return INVALID_HANDLE_VALUE;
}

static bool supported_identifiers(uint16_t vendor_id, uint16_t product_id)
{
    return vendor_id == UINT16_C(0x0e8d)
        && product_id == UINT16_C(0x1887);
}

bool gdox_usb_bot_open(
    uint16_t vendor_id,
    uint16_t product_id,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_windows_scsi_context *context;
    HANDLE device;

    gdox_error_clear(error);
    if (transport == NULL || gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty transport output is required"
        );
        return false;
    }
    if (!supported_identifiers(vendor_id, product_id)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Windows transport only supports the validated GP63 optical mechanism"
        );
        return false;
    }
    device = open_validated_device(GENERIC_READ | GENERIC_WRITE);
    if (device == INVALID_HANDLE_VALUE) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "supported Windows optical drive was not found"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        (void)CloseHandle(device);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Windows optical transport"
        );
        return false;
    }
    context->device = device;
    transport->context = context;
    transport->ops = &windows_ops;
    return true;
}

bool gdox_usb_bot_present(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    gdox_error *error
)
{
    HANDLE device;

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
    if (!supported_identifiers(vendor_id, product_id)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Windows observer only supports the validated GP63 optical mechanism"
        );
        return false;
    }
    device = open_validated_device(0U);
    if (device != INVALID_HANDLE_VALUE) {
        *drive_present = true;
        (void)CloseHandle(device);
    }
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
    static const uint8_t ready_cdb[6] = {0};
    HANDLE device;
    gdox_windows_scsi_packet result = {0};
    gdox_error ignored;

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
    if (!supported_identifiers(vendor_id, product_id)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Windows observer only supports the validated GP63 optical mechanism"
        );
        return false;
    }
    device = open_validated_device(GENERIC_READ | GENERIC_WRITE);
    if (device == INVALID_HANDLE_VALUE) {
        return true;
    }
    *drive_present = true;
    gdox_error_clear(&ignored);
    if (execute_command(
            device,
            "TEST UNIT READY",
            ready_cdb,
            sizeof(ready_cdb),
            SCSI_IOCTL_DATA_UNSPECIFIED,
            NULL,
            0U,
            UINT32_C(5000),
            NULL,
            &result,
            &ignored
        )) {
        *media_status_known = true;
        *media_present = true;
    } else if ((result.sense[2] & 0x0fU) == 0x02U
        && (result.sense[12] == 0x3aU
            || (result.sense[12] == 0x04U
                && result.sense[13] == 0x01U))) {
        *media_status_known = true;
        *media_present = false;
    }
    (void)CloseHandle(device);
    return true;
}

bool gdox_usb_bot_restore_kernel_driver(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *reattached,
    gdox_error *error
)
{
    (void)vendor_id;
    (void)product_id;
    gdox_error_clear(error);
    if (reattached == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "reattachment output is required"
        );
        return false;
    }
    *reattached = false;
    return true;
}
