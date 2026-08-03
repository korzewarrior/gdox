#include "platform/background_host.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GDOX_TRAY_MESSAGE = WM_APP + 1,
    GDOX_TRAY_OPEN = 1001,
    GDOX_TRAY_QUIT = 1002,
};

struct gdox_background_host {
    HWND window;
    NOTIFYICONDATAA icon;
    gdox_background_host_event pending;
    UINT taskbar_created;
    bool icon_installed;
    char status[96];
    gdox_background_host_shutdown_handler shutdown_handler;
    void *shutdown_context;
};

static const char gdox_tray_window_class[] = "GDOXBackgroundHost";

gdox_background_host_event gdox_background_host_windows_session_event(
    unsigned int message,
    uintptr_t parameter
)
{
    if (message == WM_ENDSESSION && parameter != 0U) {
        return GDOX_BACKGROUND_HOST_QUIT;
    }
    return GDOX_BACKGROUND_HOST_NONE;
}

void gdox_background_host_set_shutdown_handler(
    gdox_background_host *host,
    gdox_background_host_shutdown_handler handler,
    void *context
)
{
    if (host != NULL) {
        host->shutdown_handler = handler;
        host->shutdown_context = context;
    }
}

static bool install_tray_icon(gdox_background_host *host)
{
    host->icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    if (!Shell_NotifyIconA(NIM_ADD, &host->icon)) {
        host->icon_installed = false;
        return false;
    }
    host->icon.uVersion = NOTIFYICON_VERSION_4;
    (void)Shell_NotifyIconA(NIM_SETVERSION, &host->icon);
    host->icon_installed = true;
    return true;
}

static void show_tray_menu(gdox_background_host *host)
{
    POINT cursor;
    HMENU menu = CreatePopupMenu();
    int selected;

    if (menu == NULL || !GetCursorPos(&cursor)) {
        if (menu != NULL) {
            (void)DestroyMenu(menu);
        }
        return;
    }
    (void)AppendMenuA(menu, MF_STRING | MF_DEFAULT, GDOX_TRAY_OPEN, "Open GDOX");
    (void)AppendMenuA(menu, MF_SEPARATOR, 0U, NULL);
    (void)AppendMenuA(menu, MF_STRING, GDOX_TRAY_QUIT, "Quit");
    (void)SetForegroundWindow(host->window);
    selected = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        cursor.x,
        cursor.y,
        0,
        host->window,
        NULL
    );
    (void)PostMessageA(host->window, WM_NULL, 0U, 0);
    if (selected == GDOX_TRAY_OPEN) {
        host->pending = GDOX_BACKGROUND_HOST_OPEN;
    } else if (selected == GDOX_TRAY_QUIT) {
        host->pending = GDOX_BACKGROUND_HOST_QUIT;
    }
    (void)DestroyMenu(menu);
}

static LRESULT CALLBACK background_window_proc(
    HWND window,
    UINT message,
    WPARAM word,
    LPARAM parameter
)
{
    gdox_background_host *host = (gdox_background_host *)GetWindowLongPtrA(
        window, GWLP_USERDATA
    );
    gdox_background_host_event session_event;

    if (message == WM_NCCREATE) {
        const CREATESTRUCTA *create = (const CREATESTRUCTA *)parameter;
        host = (gdox_background_host *)create->lpCreateParams;
        (void)SetWindowLongPtrA(
            window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams
        );
    }
    session_event = gdox_background_host_windows_session_event(
        message, (uintptr_t)word
    );
    if (message == WM_QUERYENDSESSION) {
        return TRUE;
    }
    if (message == WM_ENDSESSION) {
        if (host != NULL && session_event != GDOX_BACKGROUND_HOST_NONE) {
            /* Windows may terminate the process as soon as this returns. */
            if (host->shutdown_handler != NULL) {
                host->shutdown_handler(host->shutdown_context);
            }
            host->pending = session_event;
        }
        return 0;
    }
    if (host != NULL && message == GDOX_TRAY_MESSAGE) {
        if (LOWORD(parameter) == WM_LBUTTONUP
            || LOWORD(parameter) == WM_LBUTTONDBLCLK
            || LOWORD(parameter) == NIN_SELECT
            || LOWORD(parameter) == NIN_KEYSELECT) {
            host->pending = GDOX_BACKGROUND_HOST_OPEN;
        } else if (LOWORD(parameter) == WM_CONTEXTMENU
            || LOWORD(parameter) == WM_RBUTTONUP) {
            show_tray_menu(host);
        }
        return 0;
    }
    if (host != NULL && host->taskbar_created != 0U
        && message == host->taskbar_created) {
        host->pending = install_tray_icon(host)
            ? GDOX_BACKGROUND_HOST_AVAILABLE
            : GDOX_BACKGROUND_HOST_UNAVAILABLE;
        return 0;
    }
    if (message == WM_DESTROY) {
        return 0;
    }
    (void)word;
    return DefWindowProcA(window, message, word, parameter);
}

static bool register_window_class(HINSTANCE instance)
{
    WNDCLASSEXA description = {0};

    description.cbSize = sizeof(description);
    description.lpfnWndProc = background_window_proc;
    description.hInstance = instance;
    description.lpszClassName = gdox_tray_window_class;
    return RegisterClassExA(&description) != 0
        || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

gdox_background_host *gdox_background_host_create(void)
{
    gdox_background_host *host = calloc(1U, sizeof(*host));
    const HINSTANCE instance = GetModuleHandleA(NULL);
    HICON icon;

    if (host == NULL || !register_window_class(instance)) {
        free(host);
        return NULL;
    }
    host->window = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        gdox_tray_window_class,
        "GDOX background host",
        0,
        0,
        0,
        0,
        0,
        NULL,
        NULL,
        instance,
        host
    );
    if (host->window == NULL) {
        free(host);
        return NULL;
    }
    icon = LoadIconA(instance, MAKEINTRESOURCEA(1));
    if (icon == NULL) {
        icon = LoadIconA(NULL, IDI_APPLICATION);
    }
    host->icon.cbSize = sizeof(host->icon);
    host->icon.hWnd = host->window;
    host->icon.uID = 1U;
    host->icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    host->icon.uCallbackMessage = GDOX_TRAY_MESSAGE;
    host->icon.hIcon = icon;
    host->taskbar_created = RegisterWindowMessageA("TaskbarCreated");
    (void)snprintf(host->status, sizeof(host->status), "%s", "GDOX");
    (void)snprintf(
        host->icon.szTip, sizeof(host->icon.szTip), "%s", host->status
    );
    if (!install_tray_icon(host)) {
        /* Keep the message window for end-session cleanup and tray recovery. */
        host->pending = GDOX_BACKGROUND_HOST_UNAVAILABLE;
    }
    return host;
}

gdox_background_host_event gdox_background_host_poll(
    gdox_background_host *host,
    bool background_only
)
{
    MSG message;
    gdox_background_host_event event;

    if (host == NULL) {
        return GDOX_BACKGROUND_HOST_NONE;
    }
    (void)background_only;
    while (PeekMessageA(&message, host->window, 0U, 0U, PM_REMOVE)) {
        (void)TranslateMessage(&message);
        (void)DispatchMessageA(&message);
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
    char tooltip[sizeof(host->status)];

    if (host == NULL) {
        return;
    }
    (void)snprintf(
        tooltip,
        sizeof(tooltip),
        "GDOX - %.85s",
        status != NULL && status[0] != '\0' ? status : "Ready"
    );
    if (strcmp(tooltip, host->status) == 0) {
        return;
    }
    (void)snprintf(host->status, sizeof(host->status), "%s", tooltip);
    (void)snprintf(
        host->icon.szTip, sizeof(host->icon.szTip), "%s", host->status
    );
    host->icon.uFlags = NIF_TIP;
    if (host->icon_installed
        && !Shell_NotifyIconA(NIM_MODIFY, &host->icon)) {
        host->icon_installed = false;
        host->pending = GDOX_BACKGROUND_HOST_UNAVAILABLE;
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
    if (host == NULL) {
        return;
    }
    if (host->icon_installed) {
        (void)Shell_NotifyIconA(NIM_DELETE, &host->icon);
    }
    (void)DestroyWindow(host->window);
    free(host);
}
