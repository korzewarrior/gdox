#include "platform/gp08_source.h"
#include "platform/mt1887_source.h"

#include <stdio.h>

static unsigned int opens;
static unsigned int failures;
static bool refused_open(void *context, gdox_scsi_transport *transport, gdox_error *error)
{
    (void)context; (void)transport;
    ++opens;
    gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "fake device is absent");
    return false;
}
static void check(bool condition, const char *message)
{
    if (!condition) { ++failures; (void)fprintf(stderr, "%s\n", message); }
}
int main(void)
{
    gdox_error error;
    check(!gdox_gp08_source_eject(NULL, NULL, &error)
        && error.code == GDOX_ERROR_INVALID_ARGUMENT, "GP08 eject rejects NULL opener");
    check(!gdox_mt1887_source_eject(NULL, NULL, GDOX_USB_BOT_GP63, &error)
        && error.code == GDOX_ERROR_INVALID_ARGUMENT, "MT eject rejects NULL opener");
    for (unsigned int identity = 0U; identity <= GDOX_USB_BOT_IDENTITY_COUNT; ++identity) {
        if (identity == GDOX_USB_BOT_GP63 || identity == GDOX_USB_BOT_GP65) continue;
        check(!gdox_mt1887_source_eject(refused_open, NULL,
            (gdox_usb_bot_identity)identity, &error) && error.code == GDOX_ERROR_UNSUPPORTED,
            "MT eject refuses profiles with unvalidated automatic tray commands");
    }
    check(opens == 0U, "invalid eject requests never invoke transport opener");
    check(!gdox_gp08_source_eject(refused_open, NULL, &error)
        && error.code == GDOX_ERROR_NOT_FOUND, "GP08 approved eject reaches supplied opener");
    check(!gdox_mt1887_source_eject(refused_open, NULL, GDOX_USB_BOT_GP63, &error)
        && error.code == GDOX_ERROR_NOT_FOUND, "GP63 approved eject reaches supplied opener");
    check(!gdox_mt1887_source_eject(refused_open, NULL, GDOX_USB_BOT_GP65, &error)
        && error.code == GDOX_ERROR_NOT_FOUND, "GP65 approved eject reaches supplied opener");
    check(opens == 3U, "each approved eject uses its opener exactly once");
    return failures == 0U ? 0 : 1;
}
