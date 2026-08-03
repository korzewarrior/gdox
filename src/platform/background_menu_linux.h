#ifndef GDOX_PLATFORM_BACKGROUND_MENU_LINUX_H
#define GDOX_PLATFORM_BACKGROUND_MENU_LINUX_H

#include "platform/background_host.h"

#include <dbus/dbus.h>

#define GDOX_BACKGROUND_MENU_PATH "/MenuBar"

typedef struct gdox_background_menu_linux gdox_background_menu_linux;

gdox_background_menu_linux *gdox_background_menu_linux_create(
    DBusConnection *connection
);
gdox_background_host_event gdox_background_menu_linux_take_event(
    gdox_background_menu_linux *menu
);
void gdox_background_menu_linux_destroy(gdox_background_menu_linux *menu);

#endif
