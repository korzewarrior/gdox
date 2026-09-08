#include "platform/usb_bot_linux_devices.h"

#include <libusb.h>

#include <stdio.h>
#include <string.h>

struct libusb_device { uint8_t port; };
static struct libusb_device first = {1U}, second = {2U};
static libusb_device *fake_list[] = {&first, &second, NULL};
static ssize_t device_count = 2;
static unsigned int failures, opened_port, open_calls, enumerations;

static void check(bool condition, const char *message)
{ if (!condition) { (void)fprintf(stderr, "%s\n", message); ++failures; } }

int libusb_init(libusb_context **context) { *context = NULL; return LIBUSB_SUCCESS; }
void libusb_exit(libusb_context *context) { (void)context; }
ssize_t libusb_get_device_list(libusb_context *context, libusb_device ***devices)
{ (void)context; ++enumerations; *devices = fake_list; return device_count; }
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
uint8_t libusb_get_device_address(libusb_device *device) { return device->port; }
int libusb_get_port_numbers(libusb_device *device, uint8_t *ports, int length)
{ if (length < 1) return LIBUSB_ERROR_OVERFLOW; ports[0] = device->port; return 1; }
bool gdox_usb_bot_linux_device_id(libusb_device *device,
    char output[GDOX_OPTICAL_DEVICE_ID_CAPACITY])
{
    (void)snprintf(output, GDOX_OPTICAL_DEVICE_ID_CAPACITY, "linux-usb:1:%u:serial", device->port);
    return true;
}
int libusb_open(libusb_device *device, libusb_device_handle **handle)
{
    *handle = NULL;
    opened_port = device->port;
    ++open_calls;
    return LIBUSB_ERROR_ACCESS;
}

int main(void)
{
    gdox_scsi_transport transport = {0};
    gdox_error error;
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP63,
        "linux-usb:1:2:serial", &transport, &error), "return selected device permission failure");
    check(open_calls == 1U && opened_port == 2U,
           "attempt only the selected second physical device, never the first model match");
    check(!gdox_scsi_transport_is_valid(&transport), "permission failure retains no transport");
    open_calls = 0U;
    device_count = 1;
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP63,
        "linux-usb:1:2:serial", &transport, &error), "selected device is absent");
    check(error.code == GDOX_ERROR_NOT_FOUND && open_calls == 0U,
           "disconnect must not fall back to another same-model device");
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP63,
        "linux-usb:1:1:replacement", &transport, &error), "same port with another serial is absent");
    check(error.code == GDOX_ERROR_NOT_FOUND && open_calls == 0U,
           "serial replacement cannot claim a different physical unit");
    enumerations = 0U;
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP57,
        "linux-usb:1:1:serial", &transport, &error), "GP57 is Windows-only");
    check(error.code == GDOX_ERROR_UNSUPPORTED && enumerations == 0U,
           "unsupported profile is rejected before USB access");
    return failures == 0U ? 0 : 1;
}
