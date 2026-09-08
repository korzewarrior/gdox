#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* Compile the real transport against an isolated Win32 device inventory.
 * No API below can reach a physical drive. */
static HDEVINFO test_get_devices(const GUID *, PCWSTR, HWND, DWORD);
static BOOL test_enum_interfaces(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD,
    PSP_DEVICE_INTERFACE_DATA);
static BOOL test_get_detail(HDEVINFO, PSP_DEVICE_INTERFACE_DATA,
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PSP_DEVINFO_DATA);
static BOOL test_destroy_devices(HDEVINFO);
static CONFIGRET test_device_id(DEVINST, PWSTR, ULONG, ULONG);
static CONFIGRET test_parent(PDEVINST, DEVINST, ULONG);
static HANDLE test_open(PCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL test_close(HANDLE);
static BOOL test_ioctl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL test_registry(HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
static DWORD test_logical_drives(void);
static DWORD test_dos_device(PCWSTR, PWSTR, DWORD);

#define SetupDiGetClassDevsW test_get_devices
#define SetupDiEnumDeviceInterfaces test_enum_interfaces
#define SetupDiGetDeviceInterfaceDetailW test_get_detail
#define SetupDiDestroyDeviceInfoList test_destroy_devices
#define CM_Get_Device_IDW test_device_id
#define CM_Get_Parent test_parent
#define CreateFileW test_open
#define CloseHandle test_close
#define DeviceIoControl test_ioctl
#define SetupDiGetDeviceRegistryPropertyW test_registry
#define GetLogicalDrives test_logical_drives
#define QueryDosDeviceW test_dos_device
#include "platform/usb_bot_windows.c"

#define TEST_DEVICES 5U

typedef struct fake_device {
    bool present;
    bool command_access;
    bool query_access;
    bool usb;
    bool media;
    bool becoming_ready;
    unsigned int generation;
    STORAGE_BUS_TYPE bus;
    const char *vendor;
    const char *model;
    const char *revision;
    const char *serial;
} fake_device;

static fake_device fake[TEST_DEVICES];
static unsigned int scsi_commands;
static unsigned int last_scsi_device;
static unsigned int opened_count[TEST_DEVICES];
static unsigned int closed_count;
static unsigned int failures;

static void check(bool value, const char *message)
{
    if (!value) {
        ++failures;
        (void)fprintf(stderr, "%s\n", message);
    }
}

static void reset_inventory(void)
{
    memset(fake, 0, sizeof(fake));
    memset(opened_count, 0, sizeof(opened_count));
    scsi_commands = 0U;
    last_scsi_device = UINT_MAX;
    closed_count = 0U;
    fake[0] = (fake_device){true, true, true, true, true, false, 0U,
        BusTypeUsb, "HL-DT-ST", "DVDRAM GP57EB40", "PB00", "USB-A"};
    fake[1] = fake[0];
    fake[1].serial = "USB-B";
    fake[2] = (fake_device){true, true, true, false, true, false, 0U,
        BusTypeSata, "ASUS", "DRW-24D5MT", "2.00", "SATA-C"};
    fake[3] = (fake_device){true, true, true, false, false, false, 0U,
        BusTypeSata, "OTHER", "DVD-UNKNOWN", "1.00", "OTHER-D"};
    fake[4] = fake[0];
    fake[4].serial = "USB-E";
    fake[4].command_access = false;
}

static void device_path(unsigned int index, wchar_t output[64])
{
    (void)swprintf(output, 64U, L"\\\\?\\cdrom#drive-%u", index);
}

static void device_id_string(unsigned int index, char output[64])
{
    (void)snprintf(output, 64U, "\\\\?\\cdrom#drive-%u", index);
}

static HDEVINFO test_get_devices(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags)
{
    (void)guid; (void)enumerator; (void)parent; (void)flags;
    return (HDEVINFO)(uintptr_t)1U;
}

static BOOL test_enum_interfaces(HDEVINFO devices, PSP_DEVINFO_DATA information,
    const GUID *guid, DWORD ordinal, PSP_DEVICE_INTERFACE_DATA output)
{
    DWORD observed = 0U;
    (void)devices; (void)information; (void)guid;
    for (unsigned int index = 0U; index < TEST_DEVICES; ++index) {
        if (fake[index].present && observed++ == ordinal) {
            output->Reserved = (ULONG_PTR)index;
            return TRUE;
        }
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

static BOOL test_get_detail(HDEVINFO devices, PSP_DEVICE_INTERFACE_DATA interface_data,
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD capacity, PDWORD needed,
    PSP_DEVINFO_DATA info)
{
    wchar_t path[64];
    const unsigned int index = (unsigned int)interface_data->Reserved;
    DWORD bytes;
    (void)devices;
    device_path(index, path);
    bytes = (DWORD)(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)
        + (wcslen(path) + 1U) * sizeof(wchar_t));
    if (needed != NULL) *needed = bytes;
    if (detail == NULL || capacity < bytes) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(detail->DevicePath, path, (wcslen(path) + 1U) * sizeof(wchar_t));
    if (info != NULL) info->DevInst = (DEVINST)(index + 1U);
    return TRUE;
}

static BOOL test_destroy_devices(HDEVINFO devices)
{
    (void)devices;
    return TRUE;
}

static CONFIGRET test_device_id(DEVINST device, PWSTR output, ULONG capacity, ULONG flags)
{
    const wchar_t *value = device > 100U ? L"USB\\VID_0E8D&PID_1887\\UNIT"
        : L"SCSI\\CDROM\\UNIT";
    (void)flags;
    if (capacity <= wcslen(value)) return CR_BUFFER_SMALL;
    memcpy(output, value, (wcslen(value) + 1U) * sizeof(wchar_t));
    return CR_SUCCESS;
}

static CONFIGRET test_parent(PDEVINST parent, DEVINST device, ULONG flags)
{
    (void)flags;
    if (device != 0U && device <= TEST_DEVICES && fake[device - 1U].usb) {
        *parent = device + 100U;
        return CR_SUCCESS;
    }
    return CR_NO_SUCH_DEVNODE;
}

static HANDLE test_open(PCWSTR path, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD attributes, HANDLE model)
{
    (void)sharing; (void)security; (void)disposition; (void)attributes; (void)model;
    for (unsigned int index = 0U; index < TEST_DEVICES; ++index) {
        wchar_t candidate[64];
        device_path(index, candidate);
        if (fake[index].present && _wcsicmp(path, candidate) == 0) {
            if (!fake[index].query_access || (access != 0U && !fake[index].command_access)) {
                SetLastError(ERROR_ACCESS_DENIED);
                return INVALID_HANDLE_VALUE;
            }
            ++opened_count[index];
            return (HANDLE)(uintptr_t)(1000U + index * 10U + fake[index].generation);
        }
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
}

static BOOL test_close(HANDLE device)
{
    (void)device;
    ++closed_count;
    return TRUE;
}

static DWORD add_string(uint8_t *buffer, DWORD *position, const char *value)
{
    const DWORD start = *position;
    const size_t bytes = strlen(value) + 1U;
    memcpy(buffer + *position, value, bytes);
    *position += (DWORD)bytes;
    return start;
}

static BOOL test_ioctl(HANDLE handle, DWORD operation, LPVOID input, DWORD input_bytes,
    LPVOID output, DWORD output_bytes, LPDWORD returned, LPOVERLAPPED overlapped)
{
    const unsigned int encoded = (unsigned int)(uintptr_t)handle - 1000U;
    const unsigned int index = encoded / 10U;
    (void)input_bytes; (void)overlapped;
    if (index >= TEST_DEVICES || !fake[index].present
        || encoded % 10U != fake[index].generation) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    if (operation == IOCTL_STORAGE_QUERY_PROPERTY) {
        STORAGE_DEVICE_DESCRIPTOR *descriptor = output;
        DWORD position = (DWORD)sizeof(*descriptor);
        check(output_bytes >= 256U, "descriptor request fits fake response");
        memset(output, 0, output_bytes);
        descriptor->Version = sizeof(*descriptor);
        descriptor->DeviceType = 5U;
        descriptor->BusType = fake[index].bus;
        descriptor->VendorIdOffset = add_string(output, &position, fake[index].vendor);
        descriptor->ProductIdOffset = add_string(output, &position, fake[index].model);
        descriptor->ProductRevisionOffset = add_string(output, &position, fake[index].revision);
        descriptor->SerialNumberOffset = add_string(output, &position, fake[index].serial);
        descriptor->Size = position;
        *returned = position;
        return TRUE;
    }
    if (operation == IOCTL_SCSI_PASS_THROUGH_DIRECT) {
        gdox_windows_scsi_packet *packet = input;
        ++scsi_commands;
        last_scsi_device = index;
        check(input == output, "pass-through uses shared response packet");
        check(packet->command.CdbLength == 6U && packet->command.Cdb[0] == 0U,
            "test sends only TEST UNIT READY");
        if (!fake[index].media || fake[index].becoming_ready) {
            packet->command.ScsiStatus = 2U;
            packet->sense[0] = 0x70U;
            packet->sense[2] = 2U;
            packet->sense[12] = fake[index].becoming_ready ? 4U : 0x3aU;
            packet->sense[13] = fake[index].becoming_ready ? 1U : 0U;
        }
        *returned = (DWORD)sizeof(*packet);
        return TRUE;
    }
    check(false, "unexpected Windows ioctl");
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

static BOOL test_registry(HDEVINFO devices, PSP_DEVINFO_DATA info, DWORD property,
    PDWORD type, PBYTE output, DWORD capacity, PDWORD returned)
{
    wchar_t value[64];
    const unsigned int index = (unsigned int)info->DevInst - 1U;
    size_t bytes;
    (void)devices;
    if (property == SPDRP_PHYSICAL_DEVICE_OBJECT_NAME) {
        (void)swprintf(value, 64U, L"\\Device\\CdRom%u", index);
    } else if (property == SPDRP_LOCATION_INFORMATION) {
        (void)swprintf(value, 64U, L"Port %u", index);
    } else {
        (void)swprintf(value, 64U, L"Optical unit %u", index);
    }
    bytes = (wcslen(value) + 1U) * sizeof(wchar_t);
    if (returned != NULL) *returned = (DWORD)bytes;
    if (capacity < bytes) return FALSE;
    memcpy(output, value, bytes);
    *type = REG_SZ;
    return TRUE;
}

static DWORD test_logical_drives(void)
{
    return UINT32_C(0x1f0);
}

static DWORD test_dos_device(PCWSTR name, PWSTR output, DWORD capacity)
{
    if (capacity < 64U || name[0] < L'E' || name[0] > L'I') return 0U;
    (void)swprintf(output, capacity, L"\\Device\\CdRom%u", (unsigned int)(name[0] - L'E'));
    return (DWORD)wcslen(output) + 1U;
}

static void test_inventory(void)
{
    gdox_usb_bot_device devices[TEST_DEVICES];
    gdox_error error;
    size_t count = 0U;
    reset_inventory();
    check(gdox_usb_bot_list_devices(devices, TEST_DEVICES, &count, false, &error),
        "passive inventory succeeds");
    check(count == TEST_DEVICES, "all supported, unknown and inaccessible devices remain listed");
    check(scsi_commands == 0U, "passive inventory issues no SCSI commands");
    check(devices[0].identity == GDOX_USB_BOT_GP57
        && devices[1].identity == GDOX_USB_BOT_GP57
        && strcmp(devices[0].id, devices[1].id) != 0,
        "identical models retain separate opaque identifiers");
    check(strstr(devices[0].location, "E:") != NULL
        && strstr(devices[1].location, "F:") != NULL, "drive letters distinguish physical devices");
    check(devices[2].identity == GDOX_SATA_ASUS_MT1862
        && devices[2].connection == GDOX_OPTICAL_CONNECTION_SATA, "native SATA profile is listed");
    check(devices[3].identity == GDOX_USB_BOT_IDENTITY_COUNT, "unknown model is not a supported profile");
    check(!devices[4].accessible && devices[4].identity == GDOX_USB_BOT_GP57,
        "read-only property access retains inaccessible drive identity");
    check(gdox_usb_bot_list_devices(devices, TEST_DEVICES, &count, true, &error)
        && scsi_commands == 4U, "active inventory queries media only on accessible drives");
    check(devices[0].media_status_known && devices[0].media_present
        && devices[3].media_status_known && !devices[3].media_present,
        "media state remains associated with each drive");
    fake[0].becoming_ready = true;
    check(gdox_usb_bot_list_devices(devices, TEST_DEVICES, &count, true, &error)
        && !devices[0].media_status_known, "spinning up is not incorrectly classified as empty");
    check(!gdox_usb_bot_list_devices(devices, 1U, &count, false, &error)
        && count == 1U && error.code == GDOX_ERROR_OUT_OF_BOUNDS,
        "inventory capacity errors are explicit with bounded count");
    check(!gdox_usb_bot_list_devices(NULL, 0U, &count, false, &error)
        && error.code == GDOX_ERROR_INVALID_ARGUMENT, "empty inventory output is rejected");
}

static void test_pinned_open_and_recovery(void)
{
    gdox_scsi_transport transport = {0};
    gdox_error error;
    bool present = false;
    char id[64];
    const uint8_t ready[6] = {0};
    reset_inventory();
    device_id_string(1U, id);
    check(gdox_usb_bot_open_device(GDOX_USB_BOT_GP57, id, &transport, &error),
        "explicit open selects the second identical-model unit");
    check(opened_count[0] == 0U && opened_count[1] == 1U,
        "explicit open does not open the other identical model");
    check(gdox_scsi_command_none(&transport, "ready", ready, sizeof(ready), 1000U, &error)
        && last_scsi_device == 1U, "commands reach selected physical unit");
    fake[1].present = false;
    check(gdox_scsi_transport_device_present(&transport, &present, &error) && !present,
        "another identical model cannot keep disconnected selected unit present");
    check(!gdox_scsi_command_none(&transport, "ready", ready, sizeof(ready), 1000U, &error),
        "removed device handle fails");
    check(!gdox_scsi_transport_reset(&transport, &error)
        && opened_count[0] == 0U, "recovery never falls back to the other matching model");
    fake[1].present = true;
    ++fake[1].generation;
    check(gdox_scsi_transport_reset(&transport, &error), "reconnected exact device is reopened");
    check(gdox_scsi_command_none(&transport, "ready", ready, sizeof(ready), 1000U, &error)
        && last_scsi_device == 1U, "reopened handle reaches the same selected device");
    ++fake[1].generation;
    fake[1].serial = "REPLACEMENT";
    check(gdox_scsi_transport_device_present(&transport, &present, &error) && !present,
        "same-path replacement with another serial is not the owned physical unit");
    check(!gdox_scsi_command_none(&transport, "ready", ready, sizeof(ready), 1000U, &error)
        && !gdox_scsi_transport_reset(&transport, &error),
        "recovery refuses same-model replacement serial");
    check(gdox_scsi_transport_close(&transport, &error), "pinned transport closes");
    check(scsi_commands == 2U, "no command was sent to the absent or replacement drive");
}

static void test_exact_identity_validation(void)
{
    gdox_scsi_transport transport = {0};
    gdox_error error;
    char id[64];
    reset_inventory();
    device_id_string(1U, id);
    fake[1].revision = "PB01";
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP57, id, &transport, &error)
        && error.code == GDOX_ERROR_UNSUPPORTED && scsi_commands == 0U,
        "selected path never bypasses exact firmware validation");
    fake[1].present = false;
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP57, id, &transport, &error)
        && opened_count[0] == 0U, "missing selected ID cannot fall back by model");
    device_id_string(2U, id);
    fake[2].usb = true;
    check(!gdox_usb_bot_open_device(GDOX_SATA_ASUS_MT1862, id, &transport, &error),
        "SATA profile refuses a USB ancestor even if bridge reports an ATA bus");
    fake[2].usb = false;
    check(gdox_usb_bot_open_device(GDOX_SATA_ASUS_MT1862, id, &transport, &error),
        "native SATA exact identity opens");
    check(gdox_scsi_transport_close(&transport, &error), "SATA transport closes");
}

static void test_reconnect_identity_changed(void)
{
    gdox_scsi_transport transport = {0};
    gdox_error error;
    bool present = true;
    char id[64];
    const uint8_t ready[6] = {0};
    reset_inventory();
    device_id_string(1U, id);
    check(gdox_usb_bot_open_device(GDOX_USB_BOT_GP57, id, &transport, &error),
        "reconnect identity test opens the selected unit");
    ++fake[1].generation;
    fake[1].model = "DVDRAM GP65NB60";
    check(gdox_scsi_transport_device_present(&transport, &present, &error) && !present,
        "changed model at the selected path is not the owned physical drive");
    check(!gdox_scsi_command_none(&transport, "ready", ready, sizeof(ready), 1000U, &error)
        && !gdox_scsi_transport_reset(&transport, &error)
        && error.code == GDOX_ERROR_UNSUPPORTED,
        "stale-handle reopen rejects a different supported model at the same path");
    check(scsi_commands == 0U && opened_count[0] == 0U,
        "no commands or same-model fallback follow replacement rejection");
    check(gdox_scsi_transport_close(&transport, &error), "changed-model transport closes");
}

int main(void)
{
    test_inventory();
    test_pinned_open_and_recovery();
    test_exact_identity_validation();
    test_reconnect_identity_changed();
    return failures == 0U ? 0 : 1;
}
