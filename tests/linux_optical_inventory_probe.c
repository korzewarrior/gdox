#include "platform/usb_bot_linux_devices.h"

#include <libusb.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct libusb_device { uint8_t port; };
static struct libusb_device fake_usb[] = {{1U}, {2U}, {3U}};
static libusb_device *fake_list[] = {&fake_usb[0], &fake_usb[1], &fake_usb[2], NULL};
static unsigned int device_opens;
static char last_device_open[GDOX_OPTICAL_DEVICE_ID_CAPACITY];

int libusb_init(libusb_context **context) { *context = NULL; return LIBUSB_SUCCESS; }
void libusb_exit(libusb_context *context) { (void)context; }
ssize_t libusb_get_device_list(libusb_context *context, libusb_device ***devices)
{ (void)context; *devices = fake_list; return 3; }
void libusb_free_device_list(libusb_device **devices, int unref)
{ (void)devices; (void)unref; }
int libusb_get_device_descriptor(libusb_device *device, struct libusb_device_descriptor *descriptor)
{
    (void)device;
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->idVendor = GDOX_GP63_USB_VENDOR_ID;
    descriptor->idProduct = GDOX_GP63_USB_PRODUCT_ID;
    return LIBUSB_SUCCESS;
}
uint8_t libusb_get_bus_number(libusb_device *device) { (void)device; return 1U; }
uint8_t libusb_get_device_address(libusb_device *device) { return device->port + 1U; }
int libusb_get_port_numbers(libusb_device *device, uint8_t *ports, int length)
{ if (length < 1) return LIBUSB_ERROR_OVERFLOW; ports[0] = device->port; return 1; }

/* Direct device open is permitted only for query_media=true. */
int open(const char *path, int flags, ...)
{
    (void)flags;
    ++device_opens;
    (void)snprintf(last_device_open, sizeof(last_device_open), "%s", path);
    return -1;
}

int main(int argc, char **argv)
{
    gdox_usb_bot_device devices[8];
    gdox_error error;
    size_t count = 0U;
    if (argc < 6) return 2;
    const gdox_linux_device_roots roots = {argv[1], argv[2], argv[3]};
    const size_t capacity = (size_t)strtoul(argv[4], NULL, 10);
    const gdox_optical_media_query query = {
        .enabled = strcmp(argv[5], "1") == 0,
        .excluded_device_ids = (const char *const *)(argv + 6),
        .excluded_device_count = (size_t)(argc - 6),
    };
    if (capacity > 8U) return 2;
    const bool success = gdox_usb_bot_linux_list_filtered_fixture(
        &roots, devices, capacity, &count, &query, &error);
    (void)printf("status\t%d\t%zu\t%u\t%d\t%s\n", success, count, device_opens,
        error.code, last_device_open);
    for (size_t index = 0U; index < count; ++index) {
        (void)printf("device\t%u\t%s\t%s\t%d\t%d\t%d\n",
            (unsigned int)devices[index].identity, devices[index].id,
            devices[index].location, devices[index].connection,
            devices[index].accessible, devices[index].media_status_known);
    }
    if (!success) (void)fprintf(stderr, "%s\n", error.message);
    return success ? 0 : 1;
}
