#define WIN32_LEAN_AND_MEAN

#include "platform/usb_bot.h"
#include "platform/usb_bot_identity.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GDOX_WINDOWS_SENSE_BYTES 32U
#define GDOX_WINDOWS_SERIAL_BYTES 128U

typedef struct gdox_windows_scsi_context {
    HANDLE device;
    gdox_usb_bot_identity identity;
    char device_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    DWORD last_windows_error;
    char serial[GDOX_WINDOWS_SERIAL_BYTES];
    uint8_t last_sense[GDOX_WINDOWS_SENSE_BYTES];
    size_t last_sense_bytes;
} gdox_windows_scsi_context;

typedef struct gdox_windows_scsi_packet {
    SCSI_PASS_THROUGH_DIRECT command;
    UCHAR sense[GDOX_WINDOWS_SENSE_BYTES];
} gdox_windows_scsi_packet;

static const GUID gdox_cdrom_interface = {
    0x53f56308U,
    0xb6bfU,
    0x11d0U,
    {0x94U, 0xf2U, 0x00U, 0xa0U, 0xc9U, 0x1eU, 0xfbU, 0x8bU},
};

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
    DWORD *windows_error,
    gdox_error *error
)
{
    gdox_windows_scsi_packet packet = {0};
    DWORD returned = 0U;

    if (windows_error != NULL) {
        *windows_error = ERROR_SUCCESS;
    }
    if (transferred != NULL) {
        *transferred = 0U;
    }
    if (cdb == NULL || cdb_bytes == 0U
        || cdb_bytes > sizeof(packet.command.Cdb) || data_bytes > ULONG_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Windows optical command or transfer is outside its bounds"
        );
        return false;
    }
    packet.command.Length = sizeof(packet.command);
    packet.command.CdbLength = (UCHAR)cdb_bytes;
    packet.command.SenseInfoLength = sizeof(packet.sense);
    packet.command.DataIn = data_direction;
    packet.command.DataTransferLength = (ULONG)data_bytes;
    packet.command.TimeOutValue =
        (ULONG)(timeout_ms / UINT32_C(1000)
            + (timeout_ms % UINT32_C(1000) != 0U ? 1U : 0U));
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
        const DWORD code = GetLastError();
        if (windows_error != NULL) {
            *windows_error = code;
        }
        set_windows_transport_error(error, name, code);
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

static void cache_command_sense(
    gdox_windows_scsi_context *context,
    bool succeeded,
    const gdox_windows_scsi_packet *packet
)
{
    const uint8_t response_code = packet != NULL
        ? (uint8_t)(packet->sense[0] & 0x7fU)
        : 0U;

    context->last_sense_bytes = 0U;
    if (!succeeded
        && (response_code == 0x70U || response_code == 0x71U
            || response_code == 0x72U || response_code == 0x73U)) {
        memcpy(
            context->last_sense,
            packet->sense,
            sizeof(context->last_sense)
        );
        context->last_sense_bytes = sizeof(context->last_sense);
    }
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
    gdox_windows_scsi_context *context = raw_context;
    gdox_windows_scsi_packet packet = {0};
    const bool succeeded = execute_command(
        context->device,
        name,
        cdb,
        cdb_bytes,
        SCSI_IOCTL_DATA_IN,
        output,
        output_bytes,
        timeout_ms,
        transferred,
        &packet,
        &context->last_windows_error,
        error
    );
    cache_command_sense(context, succeeded, &packet);
    return succeeded;
}

static bool windows_command_out(
    void *raw_context,
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
    gdox_windows_scsi_context *context = raw_context;
    gdox_windows_scsi_packet packet = {0};
    const bool succeeded = execute_command(
        context->device,
        name,
        cdb,
        cdb_bytes,
        SCSI_IOCTL_DATA_OUT,
        (uint8_t *)input,
        input_bytes,
        timeout_ms,
        transferred,
        &packet,
        &context->last_windows_error,
        error
    );
    cache_command_sense(context, succeeded, &packet);
    return succeeded;
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
    gdox_windows_scsi_context *context = raw_context;
    gdox_windows_scsi_packet packet = {0};
    const bool succeeded = execute_command(
        context->device,
        name,
        cdb,
        cdb_bytes,
        SCSI_IOCTL_DATA_UNSPECIFIED,
        NULL,
        0U,
        timeout_ms,
        NULL,
        &packet,
        &context->last_windows_error,
        error
    );
    cache_command_sense(context, succeeded, &packet);
    return succeeded;
}

static HANDLE open_validated_device(
    gdox_usb_bot_identity requested,
    const char *device_id,
    DWORD access,
    char actual_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY],
    const char *expected_serial,
    char actual_serial[GDOX_WINDOWS_SERIAL_BYTES],
    gdox_error *error
);

static bool windows_reset(void *raw_context, gdox_error *error)
{
    gdox_windows_scsi_context *context = raw_context;
    HANDLE reopened;
    char actual_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    char actual_serial[GDOX_WINDOWS_SERIAL_BYTES];

    gdox_error_clear(error);
    if (context->last_windows_error == ERROR_SUCCESS) {
        /* A SCSI check condition keeps its channel. The class driver owns
         * bus recovery; no USB or storage reset IOCTL is issued here. */
        return true;
    }
    /* A reconnected device can leave the old handle returning different
     * OS-level I/O errors. Reopen only the original validated instance. */
    reopened = open_validated_device(context->identity, context->device_id,
        GENERIC_READ | GENERIC_WRITE, actual_id, context->serial, actual_serial, error);
    if (reopened == INVALID_HANDLE_VALUE) {
        return false;
    }
    /* Reattach only the exact selected instance; another matching model is
     * never a restoration target. Identity was revalidated before reopening. */
    (void)CloseHandle(context->device);
    context->device = reopened;
    context->last_windows_error = ERROR_SUCCESS;
    context->last_sense_bytes = 0U;
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

static bool windows_last_sense(
    const void *raw_context,
    uint8_t *output,
    size_t output_bytes,
    size_t *sense_bytes
)
{
    const gdox_windows_scsi_context *context = raw_context;
    const size_t copied = context->last_sense_bytes < output_bytes
        ? context->last_sense_bytes
        : output_bytes;

    if (copied == 0U) {
        *sense_bytes = 0U;
        return false;
    }
    memcpy(output, context->last_sense, copied);
    *sense_bytes = copied;
    return true;
}

static bool windows_device_present(
    const void *raw_context,
    bool *present,
    gdox_error *error
);

static const gdox_scsi_transport_ops windows_ops = {
    windows_command_in,
    windows_command_out,
    windows_command_none,
    windows_reset,
    windows_close,
    NULL,
    windows_last_sense,
    windows_device_present,
};

static bool descriptor_string_copy(
    const uint8_t *descriptor,
    size_t descriptor_bytes,
    DWORD offset,
    char *output,
    size_t output_bytes
)
{
    const char *value;
    const char *terminator;
    size_t value_bytes;
    const size_t remaining =
        offset < descriptor_bytes ? descriptor_bytes - offset : 0U;

    if (offset == 0U || remaining == 0U || output_bytes == 0U) {
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
    if (value_bytes >= output_bytes) {
        return false;
    }
    memcpy(output, value, value_bytes);
    output[value_bytes] = '\0';
    return true;
}

typedef struct gdox_windows_device_identity {
    STORAGE_BUS_TYPE bus;
    char vendor[32];
    char model[64];
    char revision[32];
    char serial[GDOX_WINDOWS_SERIAL_BYTES];
    bool valid;
} gdox_windows_device_identity;

static bool query_device_identity(HANDLE device, gdox_windows_device_identity *identity)
{
    STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery, {0}};
    uint8_t buffer[1024] = {0};
    DWORD returned = 0U;
    const STORAGE_DEVICE_DESCRIPTOR *descriptor =
        (const STORAGE_DEVICE_DESCRIPTOR *)buffer;

    memset(identity, 0, sizeof(*identity));
    if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query,
            sizeof(query), buffer, sizeof(buffer), &returned, NULL)
        || returned < sizeof(*descriptor) || returned > sizeof(buffer)) {
        return false;
    }
    identity->bus = descriptor->BusType;
    (void)descriptor_string_copy(buffer, returned, descriptor->SerialNumberOffset,
        identity->serial, sizeof(identity->serial));
    identity->valid = descriptor_string_copy(buffer, returned,
            descriptor->VendorIdOffset, identity->vendor, sizeof(identity->vendor))
        && descriptor_string_copy(buffer, returned, descriptor->ProductIdOffset,
            identity->model, sizeof(identity->model))
        && descriptor_string_copy(buffer, returned, descriptor->ProductRevisionOffset,
            identity->revision, sizeof(identity->revision));
    return identity->valid;
}

static bool device_usb_ids(DEVINST device, uint16_t *vendor, uint16_t *product)
{
    for (unsigned int depth = 0U; depth < 64U; ++depth) {
        wchar_t instance[MAX_DEVICE_ID_LEN];
        DEVINST parent;
        unsigned int parsed_vendor;
        unsigned int parsed_product;
        if (CM_Get_Device_IDW(device, instance, MAX_DEVICE_ID_LEN, 0U) == CR_SUCCESS
            && _wcsnicmp(instance, L"USB\\VID_", 8U) == 0
#if defined(_MSC_VER)
            && swscanf_s(instance + 8U, L"%4x&PID_%4x", &parsed_vendor, &parsed_product) == 2
#else
            && swscanf(instance + 8U, L"%4x&PID_%4x", &parsed_vendor, &parsed_product) == 2
#endif
            && parsed_vendor <= UINT16_MAX && parsed_product <= UINT16_MAX) {
            *vendor = (uint16_t)parsed_vendor;
            *product = (uint16_t)parsed_product;
            return true;
        }
        if (CM_Get_Parent(&parent, device, 0U) != CR_SUCCESS || parent == device) {
            break;
        }
        device = parent;
    }
    return false;
}

static bool native_sata_bus(STORAGE_BUS_TYPE bus)
{
    return bus == BusTypeSata || bus == BusTypeAtapi || bus == BusTypeAta;
}

static bool device_identity_matches(
    const gdox_windows_device_identity *observed,
    DEVINST device_instance,
    gdox_usb_bot_identity requested
)
{
    uint16_t vendor_id = 0U;
    uint16_t product_id = 0U;
    const bool usb = device_usb_ids(device_instance, &vendor_id, &product_id);
    const gdox_usb_bot_observed_identity usb_identity = {
        vendor_id, product_id, observed->vendor, observed->model, observed->revision,
    };
    if (!observed->valid) {
        return false;
    }
    if (gdox_optical_identity_requires_native_sata(requested)) {
        return !usb && native_sata_bus(observed->bus)
            && gdox_optical_native_sata_identity_matches(requested,
                observed->vendor, observed->model, observed->revision);
    }
    return usb && gdox_usb_bot_identity_matches(requested, &usb_identity);
}

static gdox_usb_bot_identity observed_device_identity(
    const gdox_windows_device_identity *observed,
    DEVINST device_instance
)
{
    for (size_t index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        const gdox_usb_bot_identity identity = (gdox_usb_bot_identity)index;
        if (device_identity_matches(observed, device_instance, identity)) {
            return identity;
        }
    }
    return GDOX_USB_BOT_IDENTITY_COUNT;
}

static bool copy_device_id(const wchar_t *path, char output[GDOX_OPTICAL_DEVICE_ID_CAPACITY])
{
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
        output, (int)GDOX_OPTICAL_DEVICE_ID_CAPACITY, NULL, NULL) > 0;
}

typedef bool (*windows_device_visitor)(HDEVINFO devices,
    SP_DEVINFO_DATA *device_info, const wchar_t *path, const char *id,
    void *context, bool *finished, gdox_error *error);

static bool enumerate_windows_devices(windows_device_visitor visitor,
    void *context, gdox_error *error)
{
    HDEVINFO devices = SetupDiGetClassDevsW(&gdox_cdrom_interface, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    bool success = true;
    if (devices == INVALID_HANDLE_VALUE) {
        set_windows_transport_error(error, "enumerate Windows optical drives", GetLastError());
        return false;
    }
    for (DWORD index = 0U;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data = {0};
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
        SP_DEVINFO_DATA device_info = {0};
        DWORD required = 0U;
        char id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
        bool finished = false;
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, &gdox_cdrom_interface,
                index, &interface_data)) {
            const DWORD code = GetLastError();
            if (code != ERROR_NO_MORE_ITEMS) {
                set_windows_transport_error(error, "enumerate Windows optical interface", code);
                success = false;
            }
            break;
        }
        (void)SetupDiGetDeviceInterfaceDetailW(devices, &interface_data,
            NULL, 0U, &required, NULL);
        if (required < sizeof(*detail)) {
            continue;
        }
        detail = malloc((size_t)required);
        if (detail == NULL) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL,
                "could not allocate Windows optical device detail");
            success = false;
            break;
        }
        detail->cbSize = sizeof(*detail);
        device_info.cbSize = sizeof(device_info);
        if (SetupDiGetDeviceInterfaceDetailW(devices, &interface_data,
                detail, required, NULL, &device_info)) {
            if (!copy_device_id(detail->DevicePath, id)) {
                gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
                    "Windows optical device identifier exceeds the supported size");
                success = false;
            } else {
                success = visitor(devices, &device_info, detail->DevicePath,
                    id, context, &finished, error);
            }
        }
        free(detail);
        if (!success || finished) {
            break;
        }
    }
    (void)SetupDiDestroyDeviceInfoList(devices);
    return success;
}

typedef struct windows_open_request {
    gdox_usb_bot_identity identity;
    const char *requested_id;
    DWORD access;
    HANDLE opened;
    char *actual_id;
    const char *expected_serial;
    char *actual_serial;
} windows_open_request;

static bool open_windows_candidate(HDEVINFO devices, SP_DEVINFO_DATA *device_info,
    const wchar_t *path, const char *id, void *raw_request,
    bool *finished, gdox_error *error)
{
    windows_open_request *request = raw_request;
    const bool exact = request->requested_id != NULL && request->requested_id[0] != '\0';
    gdox_windows_device_identity observed = {0};
    HANDLE device;
    (void)devices;
    if (exact && _stricmp(id, request->requested_id) != 0) {
        return true;
    }
    device = CreateFileW(path, request->access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        if (exact) {
            set_windows_transport_error(error, "open the selected Windows optical drive", GetLastError());
            return false;
        }
        return true;
    }
    if (!query_device_identity(device, &observed)
        || !device_identity_matches(&observed, device_info->DevInst, request->identity)) {
        (void)CloseHandle(device);
        if (exact) {
            gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
                "selected Windows optical drive no longer matches its exact firmware profile");
            return false;
        }
        return true;
    }
    if (request->expected_serial != NULL && request->expected_serial[0] != '\0'
        && strcmp(request->expected_serial, observed.serial) != 0) {
        (void)CloseHandle(device);
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND,
            "selected Windows optical drive has been replaced by another device");
        return false;
    }
    memcpy(request->actual_id, id, strlen(id) + 1U);
    memcpy(request->actual_serial, observed.serial, strlen(observed.serial) + 1U);
    request->opened = device;
    *finished = true;
    return true;
}

static HANDLE open_validated_device(gdox_usb_bot_identity requested,
    const char *device_id, DWORD access,
    char actual_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY],
    const char *expected_serial, char actual_serial[GDOX_WINDOWS_SERIAL_BYTES],
    gdox_error *error)
{
    windows_open_request request = {requested, device_id, access,
        INVALID_HANDLE_VALUE, actual_id, expected_serial, actual_serial};
    if (!enumerate_windows_devices(open_windows_candidate, &request, error)) {
        return INVALID_HANDLE_VALUE;
    }
    if (request.opened == INVALID_HANDLE_VALUE) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND,
            "selected Windows optical drive is disconnected or unavailable");
    }
    return request.opened;
}

bool gdox_usb_bot_open_device(gdox_usb_bot_identity requested_identity,
    const char *device_id, gdox_scsi_transport *transport, gdox_error *error)
{
    gdox_windows_scsi_context *context;
    HANDLE device;
    char actual_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
    char actual_serial[GDOX_WINDOWS_SERIAL_BYTES];
    gdox_error_clear(error);
    if (transport == NULL || gdox_scsi_transport_is_valid(transport)
        || (device_id != NULL && strlen(device_id) >= GDOX_OPTICAL_DEVICE_ID_CAPACITY)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "an empty transport and bounded device identifier are required");
        return false;
    }
    if (gdox_usb_bot_identity_get(requested_identity) == NULL) {
        gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
            "Windows transport does not support this optical firmware profile");
        return false;
    }
    device = open_validated_device(requested_identity, device_id,
        GENERIC_READ | GENERIC_WRITE, actual_id, NULL, actual_serial, error);
    if (device == INVALID_HANDLE_VALUE) {
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        (void)CloseHandle(device);
        gdox_error_set(error, GDOX_ERROR_INTERNAL,
            "could not allocate Windows optical transport");
        return false;
    }
    context->device = device;
    context->identity = requested_identity;
    memcpy(context->serial, actual_serial, strlen(actual_serial) + 1U);
    memcpy(context->device_id, actual_id, strlen(actual_id) + 1U);
    transport->context = context;
    transport->ops = &windows_ops;
    return true;
}

bool gdox_usb_bot_open(gdox_usb_bot_identity requested_identity,
    gdox_scsi_transport *transport, gdox_error *error)
{
    return gdox_usb_bot_open_device(requested_identity, NULL, transport, error);
}

static void observe_windows_media(HANDLE device, gdox_usb_bot_device *observation)
{
    static const uint8_t ready_cdb[6] = {0};
    gdox_windows_scsi_packet result = {0};
    gdox_error ignored;
    gdox_error_clear(&ignored);
    if (execute_command(device, "TEST UNIT READY", ready_cdb, sizeof(ready_cdb),
            SCSI_IOCTL_DATA_UNSPECIFIED, NULL, 0U, UINT32_C(5000), NULL,
            &result, NULL, &ignored)) {
        observation->media_status_known = true;
        observation->media_present = true;
    } else {
        const uint8_t code = (uint8_t)(result.sense[0] & 0x7fU);
        const uint8_t key = code == 0x72U || code == 0x73U
            ? (uint8_t)(result.sense[1] & 0x0fU)
            : (uint8_t)(result.sense[2] & 0x0fU);
        const uint8_t asc = code == 0x72U || code == 0x73U
            ? result.sense[2] : result.sense[12];
        if (key == 0x02U && asc == 0x3aU) {
            observation->media_status_known = true;
        }
    }
}

static bool registry_string(HDEVINFO devices, SP_DEVINFO_DATA *device_info,
    DWORD property, wchar_t *output, size_t capacity)
{
    DWORD type = 0U;
    DWORD returned = 0U;
    if (capacity == 0U || capacity > UINT32_MAX / sizeof(*output)) {
        return false;
    }
    memset(output, 0, capacity * sizeof(*output));
    return SetupDiGetDeviceRegistryPropertyW(devices, device_info, property,
            &type, (BYTE *)output, (DWORD)(capacity * sizeof(*output)), &returned)
        && type == REG_SZ && returned >= sizeof(*output)
        && returned <= capacity * sizeof(*output)
        && wmemchr(output, L'\0', capacity) != NULL;
}

static void registry_display_string(HDEVINFO devices, SP_DEVINFO_DATA *device_info,
    DWORD property, char *output, size_t capacity)
{
    wchar_t wide[256];
    if (capacity > INT_MAX
        || !registry_string(devices, device_info, property, wide,
            sizeof(wide) / sizeof(wide[0]))) {
        return;
    }
    /* Display text may be shortened; opaque device IDs are never truncated. */
    for (size_t length = wcslen(wide); length != 0U; --length) {
        wide[length] = L'\0';
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                output, (int)capacity, NULL, NULL) > 0) {
            return;
        }
    }
    output[0] = '\0';
}

static void device_location(HDEVINFO devices, SP_DEVINFO_DATA *device_info,
    char output[GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY])
{
    wchar_t physical_name[256];
    char location[GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY - 5U] = {0};
    wchar_t letter = L'\0';
    registry_display_string(devices, device_info, SPDRP_LOCATION_INFORMATION,
        location, sizeof(location));
    if (registry_string(devices, device_info, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME,
            physical_name, sizeof(physical_name) / sizeof(physical_name[0]))) {
        const DWORD mask = GetLogicalDrives();
        for (unsigned int index = 0U; index < 26U; ++index) {
            wchar_t name[3] = {(wchar_t)(L'A' + index), L':', L'\0'};
            wchar_t target[512] = {0};
            if ((mask & (UINT32_C(1) << index)) != 0U
                && QueryDosDeviceW(name, target, (DWORD)(sizeof(target) / sizeof(target[0]))) != 0U
                && _wcsicmp(target, physical_name) == 0) {
                letter = name[0];
                break;
            }
        }
    }
    if (letter != L'\0') {
        (void)snprintf(output, GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY,
            "%c:%s%.*s", (int)letter, location[0] != '\0' ? " / " : "",
            (int)GDOX_OPTICAL_DEVICE_LOCATION_CAPACITY - 6, location);
    } else {
        memcpy(output, location, strlen(location) + 1U);
    }
}

typedef struct windows_inventory_request {
    gdox_usb_bot_device *devices;
    size_t capacity;
    size_t count;
    bool query_media;
} windows_inventory_request;

static bool inventory_windows_candidate(HDEVINFO devices, SP_DEVINFO_DATA *device_info,
    const wchar_t *path, const char *id, void *raw_request,
    bool *finished, gdox_error *error)
{
    windows_inventory_request *request = raw_request;
    gdox_usb_bot_device *entry;
    gdox_windows_device_identity observed = {0};
    uint16_t vendor = 0U;
    uint16_t product = 0U;
    HANDLE device;
    (void)finished;
    if (request->count >= request->capacity) {
        gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS,
            "Windows optical inventory exceeds the available device capacity");
        return false;
    }
    entry = &request->devices[request->count++];
    memset(entry, 0, sizeof(*entry));
    entry->identity = GDOX_USB_BOT_IDENTITY_COUNT;
    memcpy(entry->id, id, strlen(id) + 1U);
    registry_display_string(devices, device_info, SPDRP_FRIENDLYNAME,
        entry->name, sizeof(entry->name));
    if (entry->name[0] == '\0') {
        registry_display_string(devices, device_info, SPDRP_DEVICEDESC,
            entry->name, sizeof(entry->name));
    }
    if (entry->name[0] == '\0') {
        (void)snprintf(entry->name, sizeof(entry->name), "Optical drive");
    }
    device_location(devices, device_info, entry->location);
    if (device_usb_ids(device_info->DevInst, &vendor, &product)) {
        entry->connection = GDOX_OPTICAL_CONNECTION_USB;
    }
    device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    entry->accessible = device != INVALID_HANDLE_VALUE;
    if (device == INVALID_HANDLE_VALUE) {
        device = CreateFileW(path, 0U, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (device == INVALID_HANDLE_VALUE) {
        return true;
    }
    if (query_device_identity(device, &observed)) {
        entry->identity = observed_device_identity(&observed, device_info->DevInst);
        (void)snprintf(entry->name, sizeof(entry->name), "%s%s%s%s%s",
            observed.vendor, observed.vendor[0] != '\0' ? " " : "",
            observed.model, observed.revision[0] != '\0' ? " " : "", observed.revision);
    }
    if (entry->connection == GDOX_OPTICAL_CONNECTION_UNKNOWN) {
        entry->connection = observed.bus == BusTypeUsb ? GDOX_OPTICAL_CONNECTION_USB
            : native_sata_bus(observed.bus) ? GDOX_OPTICAL_CONNECTION_SATA
            : observed.bus != BusTypeUnknown ? GDOX_OPTICAL_CONNECTION_OTHER
            : GDOX_OPTICAL_CONNECTION_UNKNOWN;
    }
    if (request->query_media && entry->accessible) {
        observe_windows_media(device, entry);
    }
    (void)CloseHandle(device);
    return true;
}

bool gdox_usb_bot_list_devices(gdox_usb_bot_device *devices, size_t capacity,
    size_t *count, bool query_media, gdox_error *error)
{
    windows_inventory_request request = {devices, capacity, 0U, query_media};
    bool success;
    gdox_error_clear(error);
    if (count == NULL || capacity == 0U || devices == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "optical device inventory and count outputs are required");
        return false;
    }
    *count = 0U;
    success = enumerate_windows_devices(inventory_windows_candidate, &request, error);
    *count = request.count;
    return success;
}

typedef struct windows_presence_request {
    gdox_usb_bot_identity identity;
    const char *id;
    const char *serial;
    bool connected;
} windows_presence_request;

static bool connected_windows_candidate(HDEVINFO devices, SP_DEVINFO_DATA *device_info,
    const wchar_t *path, const char *id, void *raw_request,
    bool *finished, gdox_error *error)
{
    windows_presence_request *request = raw_request;
    gdox_windows_device_identity observed = {0};
    HANDLE device;
    (void)devices;
    if (_stricmp(id, request->id) != 0) {
        return true;
    }
    *finished = true;
    device = CreateFileW(path, 0U, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        set_windows_transport_error(error, "inspect the selected Windows optical drive", GetLastError());
        return false;
    }
    if (!query_device_identity(device, &observed)) {
        (void)CloseHandle(device);
        gdox_error_set(error, GDOX_ERROR_TRANSPORT,
            "selected Windows optical drive identity could not be inspected");
        return false;
    }
    request->connected = device_identity_matches(&observed, device_info->DevInst, request->identity)
        && (request->serial == NULL || request->serial[0] == '\0'
            || strcmp(request->serial, observed.serial) == 0);
    (void)CloseHandle(device);
    return true;
}

bool gdox_usb_bot_device_connected(gdox_usb_bot_identity identity,
    const char *device_id, bool *connected, gdox_error *error)
{
    windows_presence_request request = {identity, device_id, NULL, false};
    bool success;
    gdox_error_clear(error);
    if (connected == NULL || device_id == NULL || device_id[0] == '\0'
        || strlen(device_id) >= GDOX_OPTICAL_DEVICE_ID_CAPACITY
        || gdox_usb_bot_identity_get(identity) == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "an exact optical identity, device identifier and presence output are required");
        return false;
    }
    *connected = false;
    success = enumerate_windows_devices(connected_windows_candidate, &request, error);
    if (success) {
        *connected = request.connected;
    }
    return success;
}

bool gdox_usb_bot_observe_all(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error)
{
    gdox_usb_bot_device devices[GDOX_OPTICAL_MAX_DEVICES];
    size_t count = 0U;
    gdox_error_clear(error);
    if (observations == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "optical observations output is required");
        return false;
    }
    memset(observations, 0, sizeof(*observations) * GDOX_USB_BOT_IDENTITY_COUNT);
    if (!gdox_usb_bot_list_devices(devices, GDOX_OPTICAL_MAX_DEVICES, &count, true, error)) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        const gdox_usb_bot_device *device = &devices[index];
        if (device->identity != GDOX_USB_BOT_IDENTITY_COUNT) {
            gdox_usb_bot_observation *observation = &observations[(size_t)device->identity];
            observation->media_status_known = observation->drive_present
                ? observation->media_status_known && device->media_status_known
                : device->media_status_known;
            observation->drive_present = true;
            observation->media_present |= device->media_present;
            observation->media_status_known |= observation->media_present;
        }
    }
    return true;
}

bool gdox_usb_bot_present_all(bool drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error)
{
    gdox_usb_bot_device devices[GDOX_OPTICAL_MAX_DEVICES];
    size_t count = 0U;
    gdox_error_clear(error);
    if (drive_present == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "optical presence output is required");
        return false;
    }
    memset(drive_present, 0, sizeof(*drive_present) * GDOX_USB_BOT_IDENTITY_COUNT);
    if (!gdox_usb_bot_list_devices(devices, GDOX_OPTICAL_MAX_DEVICES, &count, false, error)) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        if (devices[index].identity != GDOX_USB_BOT_IDENTITY_COUNT) {
            drive_present[(size_t)devices[index].identity] = true;
        }
    }
    return true;
}

static bool windows_device_present(const void *raw_context,
    bool *present, gdox_error *error)
{
    const gdox_windows_scsi_context *context = raw_context;
    windows_presence_request request = {context->identity, context->device_id,
        context->serial, false};
    gdox_error_clear(error);
    if (present == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "optical presence output is required");
        return false;
    }
    *present = false;
    if (!enumerate_windows_devices(connected_windows_candidate, &request, error)) {
        return false;
    }
    *present = request.connected;
    return true;
}
