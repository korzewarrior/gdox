#include "platform/background_dbus_linux.h"

#include <stddef.h>

bool gdox_background_dbus_append_variant(
    DBusMessageIter *output,
    int type,
    const char *signature,
    const void *value
)
{
    DBusMessageIter variant;
    return dbus_message_iter_open_container(
            output, DBUS_TYPE_VARIANT, signature, &variant
        )
        && dbus_message_iter_append_basic(&variant, type, value)
        && dbus_message_iter_close_container(output, &variant);
}

bool gdox_background_dbus_append_property(
    DBusMessageIter *properties,
    const char *name,
    int type,
    const char *signature,
    const void *value
)
{
    DBusMessageIter entry;
    return dbus_message_iter_open_container(
            properties, DBUS_TYPE_DICT_ENTRY, NULL, &entry
        )
        && dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name)
        && gdox_background_dbus_append_variant(
            &entry, type, signature, value
        )
        && dbus_message_iter_close_container(properties, &entry);
}

DBusHandlerResult gdox_background_dbus_send_reply(
    DBusConnection *connection,
    DBusMessage *reply
)
{
    if (reply == NULL) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    (void)dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}
