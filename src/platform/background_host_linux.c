#include "platform/background_host.h"
#include "platform/background_dbus_linux.h"
#include "platform/background_menu_linux.h"

#include <dbus/dbus.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_SNI_PATH "/StatusNotifierItem"
#define GDOX_SNI_INTERFACE "org.kde.StatusNotifierItem"

static const char watcher_match[] =
    "type='signal',sender='org.freedesktop.DBus',"
    "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
    "arg0='org.kde.StatusNotifierWatcher'";

struct gdox_background_host {
    DBusConnection *connection;
    gdox_background_menu_linux *menu;
    gdox_background_host_event pending;
    bool watcher_registered;
    bool watcher_reconnect_pending;
    char title[96];
};

static DBusHandlerResult watch_watcher_owner(
    DBusConnection *connection,
    DBusMessage *message,
    void *context
)
{
    gdox_background_host *host = context;
    DBusError error;
    const char *name;
    const char *old_owner;
    const char *new_owner;

    (void)connection;
    if (!dbus_message_is_signal(
            message, "org.freedesktop.DBus", "NameOwnerChanged"
        )) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    dbus_error_init(&error);
    if (!dbus_message_get_args(
            message,
            &error,
            DBUS_TYPE_STRING,
            &name,
            DBUS_TYPE_STRING,
            &old_owner,
            DBUS_TYPE_STRING,
            &new_owner,
            DBUS_TYPE_INVALID
        )
        || strcmp(name, "org.kde.StatusNotifierWatcher") != 0) {
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    (void)old_owner;
    if (new_owner[0] == '\0') {
        host->watcher_registered = false;
        host->watcher_reconnect_pending = false;
        host->pending = GDOX_BACKGROUND_HOST_UNAVAILABLE;
    } else if (!host->watcher_registered) {
        host->watcher_reconnect_pending = true;
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static bool append_sni_property(
    DBusMessageIter *output,
    const gdox_background_host *host,
    const char *property
)
{
    const char *text;
    dbus_bool_t flag;

    if (strcmp(property, "Category") == 0) {
        text = "ApplicationStatus";
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_STRING, "s", &text
        );
    }
    if (strcmp(property, "Id") == 0) {
        text = "gdox";
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_STRING, "s", &text
        );
    }
    if (strcmp(property, "Title") == 0) {
        text = host->title;
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_STRING, "s", &text
        );
    }
    if (strcmp(property, "Status") == 0) {
        text = "Active";
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_STRING, "s", &text
        );
    }
    if (strcmp(property, "IconName") == 0) {
        text = "gdox";
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_STRING, "s", &text
        );
    }
    if (strcmp(property, "Menu") == 0) {
        text = GDOX_BACKGROUND_MENU_PATH;
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_OBJECT_PATH, "o", &text
        );
    }
    if (strcmp(property, "ItemIsMenu") == 0) {
        flag = false;
        return gdox_background_dbus_append_variant(
            output, DBUS_TYPE_BOOLEAN, "b", &flag
        );
    }
    return false;
}

static bool append_sni_property_entry(
    DBusMessageIter *properties,
    const gdox_background_host *host,
    const char *name
)
{
    DBusMessageIter entry;
    return dbus_message_iter_open_container(
            properties, DBUS_TYPE_DICT_ENTRY, NULL, &entry
        )
        && dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name)
        && append_sni_property(&entry, host, name)
        && dbus_message_iter_close_container(properties, &entry);
}

static DBusMessage *sni_get_property(
    gdox_background_host *host,
    DBusMessage *message
)
{
    DBusError error;
    const char *interface_name;
    const char *property;
    DBusMessage *reply;
    DBusMessageIter output;

    dbus_error_init(&error);
    if (!dbus_message_get_args(
            message,
            &error,
            DBUS_TYPE_STRING,
            &interface_name,
            DBUS_TYPE_STRING,
            &property,
            DBUS_TYPE_INVALID
        )
        || strcmp(interface_name, GDOX_SNI_INTERFACE) != 0) {
        dbus_error_free(&error);
        return dbus_message_new_error(
            message, DBUS_ERROR_INVALID_ARGS, "Unknown property"
        );
    }
    reply = dbus_message_new_method_return(message);
    if (reply == NULL) {
        return NULL;
    }
    dbus_message_iter_init_append(reply, &output);
    if (!append_sni_property(&output, host, property)) {
        dbus_message_unref(reply);
        return dbus_message_new_error(
            message, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property"
        );
    }
    return reply;
}

static DBusMessage *sni_get_all(
    gdox_background_host *host,
    DBusMessage *message
)
{
    static const char *const names[] = {
        "Category", "Id", "Title", "Status", "IconName", "Menu",
        "ItemIsMenu",
    };
    DBusMessage *reply = dbus_message_new_method_return(message);
    DBusMessageIter output;
    DBusMessageIter properties;
    size_t index;

    if (reply == NULL) {
        return NULL;
    }
    dbus_message_iter_init_append(reply, &output);
    if (!dbus_message_iter_open_container(
            &output, DBUS_TYPE_ARRAY, "{sv}", &properties
        )) {
        dbus_message_unref(reply);
        return NULL;
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (!append_sni_property_entry(&properties, host, names[index])) {
            dbus_message_unref(reply);
            return NULL;
        }
    }
    if (!dbus_message_iter_close_container(&output, &properties)) {
        dbus_message_unref(reply);
        return NULL;
    }
    return reply;
}

static DBusHandlerResult handle_sni(
    DBusConnection *connection,
    DBusMessage *message,
    void *context
)
{
    gdox_background_host *host = context;
    DBusMessage *reply = NULL;

    (void)connection;
    if (dbus_message_is_method_call(
            message, "org.freedesktop.DBus.Properties", "Get"
        )) {
        reply = sni_get_property(host, message);
    } else if (dbus_message_is_method_call(
            message, "org.freedesktop.DBus.Properties", "GetAll"
        )) {
        reply = sni_get_all(host, message);
    } else if (dbus_message_is_method_call(
            message, GDOX_SNI_INTERFACE, "Activate"
        ) || dbus_message_is_method_call(
            message, GDOX_SNI_INTERFACE, "SecondaryActivate"
        )) {
        host->pending = GDOX_BACKGROUND_HOST_OPEN;
        reply = dbus_message_new_method_return(message);
    } else if (dbus_message_is_method_call(
            message, GDOX_SNI_INTERFACE, "ContextMenu"
        ) || dbus_message_is_method_call(
            message, GDOX_SNI_INTERFACE, "Scroll"
        )) {
        reply = dbus_message_new_method_return(message);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return gdox_background_dbus_send_reply(connection, reply);
}

static const DBusObjectPathVTable sni_vtable = {
    .unregister_function = NULL,
    .message_function = handle_sni,
};

static bool register_with_watcher(DBusConnection *connection)
{
    DBusError error;
    DBusMessage *request;
    DBusMessage *reply;
    const char *service = dbus_bus_get_unique_name(connection);
    bool registered = false;

    dbus_error_init(&error);
    if (!dbus_bus_name_has_owner(
            connection, "org.kde.StatusNotifierWatcher", &error
        )) {
        dbus_error_free(&error);
        return false;
    }
    request = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem"
    );
    if (request == NULL || service == NULL
        || !dbus_message_append_args(
            request, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID
        )) {
        if (request != NULL) {
            dbus_message_unref(request);
        }
        return false;
    }
    reply = dbus_connection_send_with_reply_and_block(
        connection, request, 1000, &error
    );
    dbus_message_unref(request);
    if (reply != NULL) {
        registered = dbus_message_get_type(reply)
            == DBUS_MESSAGE_TYPE_METHOD_RETURN;
        dbus_message_unref(reply);
    }
    dbus_error_free(&error);
    return registered;
}

gdox_background_host *gdox_background_host_create(void)
{
    gdox_background_host *host = calloc(1U, sizeof(*host));
    DBusError error;
    bool initialized;

    if (host == NULL || !dbus_threads_init_default()) {
        free(host);
        return NULL;
    }
    dbus_error_init(&error);
    host->connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    dbus_error_free(&error);
    if (host->connection == NULL) {
        free(host);
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect(host->connection, false);
    (void)snprintf(host->title, sizeof(host->title), "%s", "GDOX");
    dbus_bus_add_match(
        host->connection,
        watcher_match,
        &error
    );
    initialized = !dbus_error_is_set(&error)
        && dbus_connection_add_filter(
            host->connection, watch_watcher_owner, host, NULL
        )
        && dbus_connection_register_object_path(
            host->connection, GDOX_SNI_PATH, &sni_vtable, host
        );
    if (initialized) {
        host->menu = gdox_background_menu_linux_create(host->connection);
    }
    if (!initialized || host->menu == NULL
        || !register_with_watcher(host->connection)) {
        dbus_error_free(&error);
        gdox_background_menu_linux_destroy(host->menu);
        (void)dbus_connection_unregister_object_path(
            host->connection, GDOX_SNI_PATH
        );
        dbus_connection_close(host->connection);
        dbus_connection_unref(host->connection);
        free(host);
        return NULL;
    }
    dbus_error_free(&error);
    host->watcher_registered = true;
    return host;
}

gdox_background_host_event gdox_background_host_poll(
    gdox_background_host *host,
    bool background_only
)
{
    gdox_background_host_event event;
    gdox_background_host_event menu_event;

    if (host == NULL) {
        return GDOX_BACKGROUND_HOST_NONE;
    }
    (void)background_only;
    if (!dbus_connection_get_is_connected(host->connection)) {
        host->watcher_registered = false;
        return GDOX_BACKGROUND_HOST_UNAVAILABLE;
    }
    (void)dbus_connection_read_write(host->connection, 0);
    while (dbus_connection_get_dispatch_status(host->connection)
        == DBUS_DISPATCH_DATA_REMAINS) {
        (void)dbus_connection_dispatch(host->connection);
    }
    if (host->watcher_reconnect_pending
        && register_with_watcher(host->connection)) {
        host->watcher_registered = true;
        host->watcher_reconnect_pending = false;
        host->pending = GDOX_BACKGROUND_HOST_AVAILABLE;
    }
    menu_event = gdox_background_menu_linux_take_event(host->menu);
    if (menu_event != GDOX_BACKGROUND_HOST_NONE) {
        return menu_event;
    }
    event = host->pending;
    host->pending = GDOX_BACKGROUND_HOST_NONE;
    return event;
}

void gdox_background_host_set_status(
    gdox_background_host *host,
    const char *status
)
{
    char title[sizeof(host->title)];
    DBusMessage *signal;

    if (host == NULL) {
        return;
    }
    (void)snprintf(
        title,
        sizeof(title),
        "GDOX - %.85s",
        status != NULL && status[0] != '\0' ? status : "Ready"
    );
    if (strcmp(title, host->title) == 0) {
        return;
    }
    (void)snprintf(host->title, sizeof(host->title), "%s", title);
    signal = dbus_message_new_signal(
        GDOX_SNI_PATH, GDOX_SNI_INTERFACE, "NewTitle"
    );
    if (signal != NULL) {
        (void)dbus_connection_send(host->connection, signal, NULL);
        dbus_message_unref(signal);
    }
}

void gdox_background_host_set_window_visible(
    gdox_background_host *host,
    bool visible
)
{
    (void)host;
    (void)visible;
}

void gdox_background_host_complete_shutdown(gdox_background_host *host)
{
    (void)host;
}

void gdox_background_host_destroy(gdox_background_host *host)
{
    DBusError error;

    if (host == NULL) {
        return;
    }
    dbus_connection_remove_filter(
        host->connection, watch_watcher_owner, host
    );
    dbus_error_init(&error);
    dbus_bus_remove_match(host->connection, watcher_match, &error);
    dbus_error_free(&error);
    gdox_background_menu_linux_destroy(host->menu);
    (void)dbus_connection_unregister_object_path(
        host->connection, GDOX_SNI_PATH
    );
    dbus_connection_flush(host->connection);
    dbus_connection_close(host->connection);
    dbus_connection_unref(host->connection);
    free(host);
}
