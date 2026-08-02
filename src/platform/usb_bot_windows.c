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

typedef struct gdox_windows_scsi_context {
    HANDLE device;
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
    const gdox_windows_scsi_context *context = raw_context;
    return execute_command(
        context->device,
        name,
        cdb,
        cdb_bytes,
        SCSI_IOCTL_DATA_OUT,
        (uint8_t *)input,
        input_bytes,
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
    windows_command_out,
    windows_command_none,
    windows_reset,
    windows_close,
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

static bool device_identity_matches(
    HANDLE device,
    gdox_usb_bot_identity requested
)
{
    const gdox_usb_bot_identity_spec *expected =
        gdox_usb_bot_identity_get(requested);
    STORAGE_PROPERTY_QUERY query = {
        StorageDeviceProperty,
        PropertyStandardQuery,
        {0},
    };
    uint8_t buffer[1024] = {0};
    DWORD returned = 0U;
    const STORAGE_DEVICE_DESCRIPTOR *descriptor =
        (const STORAGE_DEVICE_DESCRIPTOR *)buffer;
    char vendor[32];
    char model[32];
    char revision[32];
    gdox_usb_bot_observed_identity observed;

    if (expected == NULL
        || !DeviceIoControl(
            device,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            buffer,
            sizeof(buffer),
            &returned,
            NULL
        )
        || returned < sizeof(*descriptor)
        || !descriptor_string_copy(
            buffer,
            returned,
            descriptor->VendorIdOffset,
            vendor,
            sizeof(vendor)
        )
        || !descriptor_string_copy(
            buffer,
            returned,
            descriptor->ProductIdOffset,
            model,
            sizeof(model)
        )
        || !descriptor_string_copy(
            buffer,
            returned,
            descriptor->ProductRevisionOffset,
            revision,
            sizeof(revision)
        )) {
        return false;
    }
    observed = (gdox_usb_bot_observed_identity){
        expected->vendor_id,
        expected->product_id,
        vendor,
        model,
        revision,
    };
    return gdox_usb_bot_identity_matches(requested, &observed);
}

static bool device_usb_identity_matches(
    DEVINST device,
    gdox_usb_bot_identity requested
)
{
    const gdox_usb_bot_identity_spec *identity =
        gdox_usb_bot_identity_get(requested);
    wchar_t prefix[32];
    int written;
    size_t prefix_length;

    if (identity == NULL) {
        return false;
    }
    written = swprintf(
        prefix,
        sizeof(prefix) / sizeof(prefix[0]),
        L"USB\\VID_%04X&PID_%04X",
        (unsigned int)identity->vendor_id,
        (unsigned int)identity->product_id
    );

    if (written < 0) {
        return false;
    }
    prefix_length = (size_t)written;
    for (;;) {
        wchar_t instance[MAX_DEVICE_ID_LEN];
        DEVINST parent;

        if (CM_Get_Device_IDW(
                device,
                instance,
                MAX_DEVICE_ID_LEN,
                0U
            ) == CR_SUCCESS
            && _wcsnicmp(instance, prefix, prefix_length) == 0
            && (instance[prefix_length] == L'\\'
                || instance[prefix_length] == L'&')) {
            return true;
        }
        if (CM_Get_Parent(&parent, device, 0U) != CR_SUCCESS) {
            return false;
        }
        device = parent;
    }
}

static HANDLE open_validated_device(
    gdox_usb_bot_identity requested,
    DWORD access
)
{
    HDEVINFO devices = SetupDiGetClassDevsW(
        &gdox_cdrom_interface,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    HANDLE result = INVALID_HANDLE_VALUE;
    DWORD index;

    if (devices == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    for (index = 0U;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data = {0};
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
        SP_DEVINFO_DATA device_info = {0};
        DWORD required = 0U;
        HANDLE device;

        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(
                devices,
                NULL,
                &gdox_cdrom_interface,
                index,
                &interface_data
            )) {
            break;
        }
        (void)SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interface_data,
            NULL,
            0U,
            &required,
            NULL
        );
        if (required < sizeof(*detail)) {
            continue;
        }
        detail = malloc((size_t)required);
        if (detail == NULL) {
            break;
        }
        detail->cbSize = sizeof(*detail);
        device_info.cbSize = sizeof(device_info);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devices,
                &interface_data,
                detail,
                required,
                NULL,
                &device_info
            )
            || !device_usb_identity_matches(
                device_info.DevInst,
                requested
            )) {
            free(detail);
            continue;
        }
        device = CreateFileW(
            detail->DevicePath,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        free(detail);
        if (device == INVALID_HANDLE_VALUE) {
            continue;
        }
        if (device_identity_matches(device, requested)) {
            result = device;
            break;
        }
        (void)CloseHandle(device);
    }
    (void)SetupDiDestroyDeviceInfoList(devices);
    return result;
}

bool gdox_usb_bot_open(
    gdox_usb_bot_identity requested_identity,
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
    if (gdox_usb_bot_identity_get(requested_identity) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Windows transport does not support this USB optical mechanism"
        );
        return false;
    }
    device = open_validated_device(
        requested_identity,
        GENERIC_READ | GENERIC_WRITE
    );
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

static bool observed_device_identity(
    HANDLE device,
    DEVINST device_instance,
    gdox_usb_bot_identity *observed_identity
)
{
    size_t identity_index;

    for (identity_index = 0U;
         identity_index < GDOX_USB_BOT_IDENTITY_COUNT;
         ++identity_index) {
        const gdox_usb_bot_identity identity =
            (gdox_usb_bot_identity)identity_index;

        if (device_usb_identity_matches(device_instance, identity)
            && device_identity_matches(device, identity)) {
            *observed_identity = identity;
            return true;
        }
    }
    return false;
}

static void observe_windows_media(
    HANDLE device,
    gdox_usb_bot_observation *observation
)
{
    static const uint8_t ready_cdb[6] = {0};
    gdox_windows_scsi_packet result = {0};
    gdox_error ignored;

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
        observation->media_status_known = true;
        observation->media_present = true;
    } else if ((result.sense[2] & 0x0fU) == 0x02U
        && (result.sense[12] == 0x3aU
            || (result.sense[12] == 0x04U
                && result.sense[13] == 0x01U))) {
        observation->media_status_known = true;
    }
}

static void observe_windows_device(
    HANDLE device,
    DEVINST device_instance,
    bool command_access,
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT]
)
{
    gdox_usb_bot_identity identity;
    gdox_usb_bot_observation *observation;

    if (!observed_device_identity(device, device_instance, &identity)) {
        return;
    }
    observation = &observations[(size_t)identity];
    observation->drive_present = true;
    if (command_access) {
        observe_windows_media(device, observation);
    }
}

static bool observe_windows_devices(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    bool query_media,
    gdox_error *error
)
{
    HDEVINFO devices;
    DWORD index;

    devices = SetupDiGetClassDevsW(
        &gdox_cdrom_interface,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    if (devices == INVALID_HANDLE_VALUE) {
        set_windows_transport_error(
            error,
            "enumerate Windows optical drives",
            GetLastError()
        );
        return false;
    }
    for (index = 0U;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data = {0};
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
        SP_DEVINFO_DATA device_info = {0};
        DWORD required = 0U;
        HANDLE device;
        bool command_access = query_media;

        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(
                devices,
                NULL,
                &gdox_cdrom_interface,
                index,
                &interface_data
            )) {
            break;
        }
        (void)SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interface_data,
            NULL,
            0U,
            &required,
            NULL
        );
        if (required < sizeof(*detail)) {
            continue;
        }
        detail = malloc((size_t)required);
        if (detail == NULL) {
            (void)SetupDiDestroyDeviceInfoList(devices);
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "could not allocate Windows optical device detail"
            );
            return false;
        }
        detail->cbSize = sizeof(*detail);
        device_info.cbSize = sizeof(device_info);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devices,
                &interface_data,
                detail,
                required,
                NULL,
                &device_info
            )) {
            free(detail);
            continue;
        }
        device = CreateFileW(
            detail->DevicePath,
            query_media ? GENERIC_READ | GENERIC_WRITE : 0U,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (device == INVALID_HANDLE_VALUE && query_media) {
            command_access = false;
            device = CreateFileW(
                detail->DevicePath,
                0U,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
        }
        free(detail);
        if (device == INVALID_HANDLE_VALUE) {
            continue;
        }
        observe_windows_device(
            device,
            device_info.DevInst,
            command_access,
            observations
        );
        (void)CloseHandle(device);
    }
    (void)SetupDiDestroyDeviceInfoList(devices);
    return true;
}

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
    return observe_windows_devices(observations, true, error);
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
    if (!observe_windows_devices(observations, false, error)) {
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        drive_present[index] = observations[index].drive_present;
    }
    return true;
}

bool gdox_usb_bot_restore_kernel_driver(
    gdox_usb_bot_identity identity,
    bool *reattached,
    gdox_error *error
)
{
    (void)identity;
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
