#define _XOPEN_SOURCE 700

#if defined(__linux__) && !defined(__ANDROID__)

#include "platform/usb_bot_linux_devices.h"

#include <libusb.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/cdrom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef GDOX_LINUX_DEVICES_TESTING
typedef struct gdox_linux_device_roots {
    const char *blocks;
    const char *usb;
    const char *devices;
} gdox_linux_device_roots;
#endif

static const gdox_linux_device_roots system_roots = {
    "/sys/class/block", "/sys/bus/usb/devices", "/dev",
};

static bool path_join(char output[PATH_MAX], const char *base, const char *name)
{
    const int bytes = snprintf(output, PATH_MAX, "%s/%s", base, name);
    return bytes >= 0 && (size_t)bytes < PATH_MAX;
}

static bool read_attribute(const char *directory, const char *key,
                           char *output, size_t capacity)
{
    char path[PATH_MAX];
    FILE *file;
    size_t bytes;

    output[0] = '\0';
    if (!path_join(path, directory, key) || (file = fopen(path, "r")) == NULL) {
        return false;
    }
    if (fgets(output, (int)capacity, file) == NULL) {
        (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    bytes = strlen(output);
    while (bytes != 0U && (output[bytes - 1U] == '\n'
            || output[bytes - 1U] == '\r' || output[bytes - 1U] == ' ')) {
        output[--bytes] = '\0';
    }
    return bytes != 0U;
}

static bool number_attribute(const char *directory, const char *key,
                             unsigned int base, unsigned int maximum,
                             unsigned int *value)
{
    char text[32];
    char *end;
    unsigned long parsed;

    if (!read_attribute(directory, key, text, sizeof(text))) return false;
    parsed = strtoul(text, &end, (int)base);
    if (end == text || *end != '\0' || parsed > maximum) return false;
    *value = (unsigned int)parsed;
    return true;
}

static bool usb_id(const char *directory, unsigned int bus, const char *ports,
                   char output[GDOX_OPTICAL_DEVICE_ID_CAPACITY])
{
    static const char hex[] = "0123456789abcdef";
    char serial[256];
    size_t used;
    int bytes = snprintf(output, GDOX_OPTICAL_DEVICE_ID_CAPACITY,
                         "linux-usb:%u:%s:", bus, ports);
    if (bytes < 0 || (size_t)bytes >= GDOX_OPTICAL_DEVICE_ID_CAPACITY) return false;
    used = (size_t)bytes;
    if (read_attribute(directory, "serial", serial, sizeof(serial))) {
        for (size_t index = 0U; serial[index] != '\0'; ++index) {
            const unsigned char value = (unsigned char)serial[index];
            if (used + 2U >= GDOX_OPTICAL_DEVICE_ID_CAPACITY) return false;
            output[used++] = hex[value >> 4U];
            output[used++] = hex[value & 0x0fU];
        }
    }
    output[used] = '\0';
    return true;
}

static bool libusb_id(const gdox_linux_device_roots *roots, libusb_device *device,
                      char output[GDOX_OPTICAL_DEVICE_ID_CAPACITY])
{
    uint8_t ports[GDOX_USB_BOT_MAX_PORT_DEPTH];
    char route[64];
    char name[80];
    char directory[PATH_MAX];
    size_t used = 0U;
    const unsigned int bus = libusb_get_bus_number(device);
    const int depth = libusb_get_port_numbers(device, ports, (int)sizeof(ports));

    if (bus == 0U || depth <= 0 || (size_t)depth > sizeof(ports)) return false;
    for (int index = 0; index < depth; ++index) {
        const int bytes = snprintf(route + used, sizeof(route) - used,
                                   "%s%u", index == 0 ? "" : ".", ports[index]);
        if (bytes < 0 || (size_t)bytes >= sizeof(route) - used) return false;
        used += (size_t)bytes;
    }
    (void)snprintf(name, sizeof(name), "%u-%s", bus, route);
    return path_join(directory, roots->usb, name)
        && usb_id(directory, bus, route, output);
}

bool gdox_usb_bot_linux_device_id(libusb_device *device,
    char output[GDOX_OPTICAL_DEVICE_ID_CAPACITY])
{
    return device != NULL && output != NULL && libusb_id(&system_roots, device, output);
}

static bool append_device(gdox_usb_bot_device *devices, size_t capacity,
                          size_t *count, const gdox_usb_bot_device *device,
                          gdox_error *error)
{
    for (size_t index = 0U; index < *count; ++index) {
        if (strcmp(devices[index].id, device->id) == 0) return true;
    }
    if (*count == capacity) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
                       "optical device inventory exceeds its capacity");
        return false;
    }
    devices[(*count)++] = *device;
    return true;
}

static bool usb_ancestor(char directory[PATH_MAX], unsigned int *bus,
                         unsigned int *address, char ports[64])
{
    do {
        char *slash;
        if (number_attribute(directory, "busnum", 10U, UINT8_MAX, bus)
            && number_attribute(directory, "devnum", 10U, UINT8_MAX, address)
            && read_attribute(directory, "devpath", ports, 64U)) return true;
        slash = strrchr(directory, '/');
        if (slash == NULL || slash == directory) break;
        *slash = '\0';
    } while (directory[0] != '\0');
    return false;
}

static bool usb_accessible(const gdox_linux_device_roots *roots,
                           unsigned int bus, unsigned int address)
{
    char path[PATH_MAX];
    const int bytes = snprintf(path, sizeof(path), "%s/bus/usb/%03u/%03u",
                               roots->devices, bus, address);
    return bytes >= 0 && (size_t)bytes < sizeof(path)
        && access(path, R_OK | W_OK) == 0;
}

static bool describe_block(const gdox_linux_device_roots *roots,
                           const char *name, bool query_media,
                           gdox_usb_bot_device *device)
{
    char block[PATH_MAX];
    char link[PATH_MAX];
    char physical[PATH_MAX];
    char ancestor[PATH_MAX];
    char node[PATH_MAX];
    char vendor[32] = "";
    char model[48] = "";
    char revision[16] = "";
    char ports[64];
    unsigned int bus, address;
    int bytes;

    if (!path_join(block, roots->blocks, name) || !path_join(link, block, "device")
        || realpath(link, physical) == NULL
        || !path_join(node, roots->devices, name)) return false;
    memset(device, 0, sizeof(*device));
    device->identity = GDOX_USB_BOT_IDENTITY_COUNT;
    (void)read_attribute(physical, "vendor", vendor, sizeof(vendor));
    (void)read_attribute(physical, "model", model, sizeof(model));
    (void)read_attribute(physical, "rev", revision, sizeof(revision));
    (void)snprintf(device->name, sizeof(device->name), "%s %s %s", vendor, model, revision);
    (void)snprintf(device->location, sizeof(device->location), "/dev/%.80s", name);
    memcpy(ancestor, physical, strlen(physical) + 1U);
    if (usb_ancestor(ancestor, &bus, &address, ports)) {
        unsigned int vendor_id, product_id;
        if (!usb_id(ancestor, bus, ports, device->id)) return false;
        device->connection = GDOX_OPTICAL_CONNECTION_USB;
        device->accessible = usb_accessible(roots, bus, address);
        if (number_attribute(ancestor, "idVendor", 16U, UINT16_MAX, &vendor_id)
            && number_attribute(ancestor, "idProduct", 16U, UINT16_MAX, &product_id)) {
            const gdox_usb_bot_observed_identity observed = {
                (uint16_t)vendor_id, (uint16_t)product_id, vendor, model, revision,
            };
            for (size_t index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
                const gdox_usb_bot_identity identity = (gdox_usb_bot_identity)index;
                if (!gdox_optical_identity_requires_windows(identity)
                    && gdox_usb_bot_identity_matches(identity, &observed)) {
                    device->identity = identity;
                    break;
                }
            }
        }
    } else {
        bytes = snprintf(device->id, sizeof(device->id), "linux-sysfs:%s", physical);
        if (bytes < 0 || (size_t)bytes >= sizeof(device->id)) return false;
        device->connection = strstr(physical, "/ata") != NULL
            ? GDOX_OPTICAL_CONNECTION_SATA : GDOX_OPTICAL_CONNECTION_OTHER;
        device->accessible = access(node, R_OK) == 0;
    }
    if (query_media) {
        const int descriptor = open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor >= 0) {
            const int status = ioctl(descriptor, CDROM_DRIVE_STATUS, CDSL_CURRENT);
            (void)close(descriptor);
            device->media_present = status == CDS_DISC_OK;
            device->media_status_known = device->media_present || status == CDS_NO_DISC
                || status == CDS_TRAY_OPEN || status == CDS_DRIVE_NOT_READY;
        }
    }
    return true;
}

static bool append_unbound(const gdox_linux_device_roots *roots,
                           gdox_usb_bot_device *devices, size_t capacity,
                           size_t *count, gdox_error *error)
{
    libusb_context *library = NULL;
    libusb_device **usb_devices = NULL;
    ssize_t total;
    bool success = true;
    if (libusb_init(&library) != LIBUSB_SUCCESS) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "could not initialize USB inventory");
        return false;
    }
    total = libusb_get_device_list(library, &usb_devices);
    if (total < 0) {
        libusb_exit(library);
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "could not enumerate USB inventory");
        return false;
    }
    for (ssize_t index = 0; index < total; ++index) {
        struct libusb_device_descriptor descriptor;
        gdox_usb_bot_device device = {0};
        if (libusb_get_device_descriptor(usb_devices[index], &descriptor) != LIBUSB_SUCCESS
            || !gdox_usb_bot_recovery_identity(descriptor.idVendor, descriptor.idProduct,
                                               &device.identity)
            || gdox_optical_identity_requires_windows(device.identity)
            || !libusb_id(roots, usb_devices[index], device.id)) continue;
        device.connection = GDOX_OPTICAL_CONNECTION_USB;
        device.accessible = usb_accessible(roots, libusb_get_bus_number(usb_devices[index]),
                                           libusb_get_device_address(usb_devices[index]));
        (void)snprintf(device.name, sizeof(device.name), "USB optical recovery candidate (%04x:%04x)",
                       descriptor.idVendor, descriptor.idProduct);
        (void)snprintf(device.location, sizeof(device.location), "USB bus %u address %u",
                       libusb_get_bus_number(usb_devices[index]),
                       libusb_get_device_address(usb_devices[index]));
        if (!append_device(devices, capacity, count, &device, error)) {
            success = false;
            break;
        }
    }
    libusb_free_device_list(usb_devices, 1);
    libusb_exit(library);
    return success;
}

static bool list_devices(const gdox_linux_device_roots *roots,
                         gdox_usb_bot_device *devices, size_t capacity, size_t *count,
                         bool query_media, gdox_error *error)
{
    DIR *blocks;
    struct dirent *entry;
    bool success = true;
    gdox_error_clear(error);
    if (count != NULL) *count = 0U;
    if (devices == NULL || capacity == 0U || count == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "a nonempty optical inventory is required");
        return false;
    }
    blocks = opendir(roots->blocks);
    if (blocks == NULL) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "could not enumerate Linux optical drives");
        return false;
    }
    while ((entry = readdir(blocks)) != NULL) {
        gdox_usb_bot_device device;
        if (strncmp(entry->d_name, "sr", 2U) != 0
            || !describe_block(roots, entry->d_name, query_media, &device)) continue;
        if (!append_device(devices, capacity, count, &device, error)) {
            success = false;
            break;
        }
    }
    (void)closedir(blocks);
    return success && append_unbound(roots, devices, capacity, count, error);
}

bool gdox_usb_bot_list_devices(gdox_usb_bot_device *devices, size_t capacity,
    size_t *count, bool query_media, gdox_error *error)
{
    return list_devices(&system_roots, devices, capacity, count, query_media, error);
}

bool gdox_usb_bot_device_connected(gdox_usb_bot_identity identity,
    const char *device_id, bool *connected, gdox_error *error)
{
    gdox_usb_bot_device devices[GDOX_OPTICAL_MAX_DEVICES];
    size_t count;
    gdox_error_clear(error);
    if (connected != NULL) *connected = false;
    if (device_id == NULL || device_id[0] == '\0' || connected == NULL
        || (unsigned int)identity >= GDOX_USB_BOT_IDENTITY_COUNT) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "a physical optical device ID is required");
        return false;
    }
    if (!gdox_usb_bot_list_devices(devices, GDOX_OPTICAL_MAX_DEVICES, &count, false, error)) return false;
    for (size_t index = 0U; index < count; ++index) {
        /* A claimed USB interface can lose kernel SCSI metadata temporarily. */
        if (strcmp(device_id, devices[index].id) == 0) {
            *connected = true;
            break;
        }
    }
    return true;
}

#ifdef GDOX_LINUX_DEVICES_TESTING
bool gdox_usb_bot_linux_list_fixture(const gdox_linux_device_roots *roots,
    gdox_usb_bot_device *devices, size_t capacity, size_t *count,
    bool query_media, gdox_error *error)
{
    return list_devices(roots, devices, capacity, count, query_media, error);
}
#endif

#else
typedef int gdox_linux_inventory_unavailable;
#endif
