#include "platform/background_menu_linux.h"

#include "platform/background_dbus_linux.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_MENU_INTERFACE "com.canonical.dbusmenu"

struct gdox_background_menu_linux {
    DBusConnection *connection;
    gdox_background_host_event pending;
};

static bool append_menu_item(
    DBusMessageIter *children,
    int32_t identifier,
    const char *label
)
{
    DBusMessageIter variant;
    DBusMessageIter item;
    DBusMessageIter properties;
    DBusMessageIter grandchildren;
    dbus_bool_t true_value = true;

    return dbus_message_iter_open_container(
            children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant
        )
        && dbus_message_iter_open_container(
            &variant, DBUS_TYPE_STRUCT, NULL, &item
        )
        && dbus_message_iter_append_basic(
            &item, DBUS_TYPE_INT32, &identifier
        )
        && dbus_message_iter_open_container(
            &item, DBUS_TYPE_ARRAY, "{sv}", &properties
        )
        && gdox_background_dbus_append_property(
            &properties, "label", DBUS_TYPE_STRING, "s", &label
        )
        && gdox_background_dbus_append_property(
            &properties, "enabled", DBUS_TYPE_BOOLEAN, "b", &true_value
        )
        && gdox_background_dbus_append_property(
            &properties, "visible", DBUS_TYPE_BOOLEAN, "b", &true_value
        )
        && dbus_message_iter_close_container(&item, &properties)
        && dbus_message_iter_open_container(
            &item, DBUS_TYPE_ARRAY, "v", &grandchildren
        )
        && dbus_message_iter_close_container(&item, &grandchildren)
        && dbus_message_iter_close_container(&variant, &item)
        && dbus_message_iter_close_container(children, &variant);
}

static DBusMessage *menu_layout(DBusMessage *message)
{
    DBusMessage *reply = dbus_message_new_method_return(message);
    DBusMessageIter output;
    DBusMessageIter layout;
    DBusMessageIter properties;
    DBusMessageIter children;
    uint32_t revision = 1U;
    int32_t root = 0;

    if (reply == NULL) {
        return NULL;
    }
    dbus_message_iter_init_append(reply, &output);
    if (!dbus_message_iter_append_basic(
            &output, DBUS_TYPE_UINT32, &revision
        )
        || !dbus_message_iter_open_container(
            &output, DBUS_TYPE_STRUCT, NULL, &layout
        )
        || !dbus_message_iter_append_basic(&layout, DBUS_TYPE_INT32, &root)
        || !dbus_message_iter_open_container(
            &layout, DBUS_TYPE_ARRAY, "{sv}", &properties
        )
        || !dbus_message_iter_close_container(&layout, &properties)
        || !dbus_message_iter_open_container(
            &layout, DBUS_TYPE_ARRAY, "v", &children
        )
        || !append_menu_item(&children, 1, "Open GDOX")
        || !append_menu_item(&children, 2, "Quit")
        || !dbus_message_iter_close_container(&layout, &children)
        || !dbus_message_iter_close_container(&output, &layout)) {
        dbus_message_unref(reply);
        return NULL;
    }
    return reply;
}

static DBusMessage *menu_event(
    gdox_background_menu_linux *menu,
    DBusMessage *message
)
{
    DBusMessageIter input;
    int32_t identifier;
    const char *event_name;

    if (!dbus_message_iter_init(message, &input)
        || dbus_message_iter_get_arg_type(&input) != DBUS_TYPE_INT32) {
        return dbus_message_new_error(
            message, DBUS_ERROR_INVALID_ARGS, "Invalid menu event"
        );
    }
    dbus_message_iter_get_basic(&input, &identifier);
    if (!dbus_message_iter_next(&input)
        || dbus_message_iter_get_arg_type(&input) != DBUS_TYPE_STRING) {
        return dbus_message_new_error(
            message, DBUS_ERROR_INVALID_ARGS, "Invalid menu event"
        );
    }
    dbus_message_iter_get_basic(&input, &event_name);
    if (strcmp(event_name, "clicked") == 0) {
        if (identifier == 1) {
            menu->pending = GDOX_BACKGROUND_HOST_OPEN;
        } else if (identifier == 2) {
            menu->pending = GDOX_BACKGROUND_HOST_QUIT;
        }
    }
    return dbus_message_new_method_return(message);
}

static DBusHandlerResult handle_menu(
    DBusConnection *connection,
    DBusMessage *message,
    void *context
)
{
    gdox_background_menu_linux *menu = context;
    DBusMessage *reply = NULL;

    if (dbus_message_is_method_call(
            message, GDOX_MENU_INTERFACE, "GetLayout"
        )) {
        reply = menu_layout(message);
    } else if (dbus_message_is_method_call(
            message, GDOX_MENU_INTERFACE, "Event"
        )) {
        reply = menu_event(menu, message);
    } else if (dbus_message_is_method_call(
            message, GDOX_MENU_INTERFACE, "AboutToShow"
        )) {
        dbus_bool_t update_needed = false;
        reply = dbus_message_new_method_return(message);
        if (reply != NULL) {
            (void)dbus_message_append_args(
                reply,
                DBUS_TYPE_BOOLEAN,
                &update_needed,
                DBUS_TYPE_INVALID
            );
        }
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return gdox_background_dbus_send_reply(connection, reply);
}

static const DBusObjectPathVTable menu_vtable = {
    .unregister_function = NULL,
    .message_function = handle_menu,
};

gdox_background_menu_linux *gdox_background_menu_linux_create(
    DBusConnection *connection
)
{
    gdox_background_menu_linux *menu = calloc(1U, sizeof(*menu));

    if (menu == NULL || connection == NULL) {
        free(menu);
        return NULL;
    }
    menu->connection = connection;
    if (!dbus_connection_register_object_path(
            connection, GDOX_BACKGROUND_MENU_PATH, &menu_vtable, menu
        )) {
        free(menu);
        return NULL;
    }
    return menu;
}

gdox_background_host_event gdox_background_menu_linux_take_event(
    gdox_background_menu_linux *menu
)
{
    gdox_background_host_event event;

    if (menu == NULL) {
        return GDOX_BACKGROUND_HOST_NONE;
    }
    event = menu->pending;
    menu->pending = GDOX_BACKGROUND_HOST_NONE;
    return event;
}

void gdox_background_menu_linux_destroy(gdox_background_menu_linux *menu)
{
    if (menu == NULL) {
        return;
    }
    (void)dbus_connection_unregister_object_path(
        menu->connection, GDOX_BACKGROUND_MENU_PATH
    );
    free(menu);
}
