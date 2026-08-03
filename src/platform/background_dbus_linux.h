#ifndef GDOX_PLATFORM_BACKGROUND_DBUS_LINUX_H
#define GDOX_PLATFORM_BACKGROUND_DBUS_LINUX_H

#include <dbus/dbus.h>

#include <stdbool.h>

bool gdox_background_dbus_append_variant(
    DBusMessageIter *output,
    int type,
    const char *signature,
    const void *value
);
bool gdox_background_dbus_append_property(
    DBusMessageIter *properties,
    const char *name,
    int type,
    const char *signature,
    const void *value
);
DBusHandlerResult gdox_background_dbus_send_reply(
    DBusConnection *connection,
    DBusMessage *reply
);

#endif
