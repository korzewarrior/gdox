#include "ui/window_frame_native.h"

/* Kept apart from the raylib sources: Xlib.h and raylib.h both define Font. */
#include <X11/Xlib.h>

#include <stddef.h>
#include <string.h>

/* raylib embeds GLFW without exposing its headers. */
typedef struct GLFWwindow GLFWwindow;
extern int glfwGetPlatform(void);
extern GLFWwindow *glfwGetCurrentContext(void);
extern Display *glfwGetX11Display(void);
extern Window glfwGetX11Window(GLFWwindow *window);

#define GDOX_GLFW_PLATFORM_X11 0x00060004

bool gdox_window_frame_native_ready(void)
{
    return glfwGetPlatform() == GDOX_GLFW_PLATFORM_X11
        && glfwGetCurrentContext() != NULL;
}

void gdox_window_frame_native_update(void)
{
}

void gdox_window_frame_native_drag(int direction)
{
    Display *display = glfwGetX11Display();
    const Window window = glfwGetX11Window(glfwGetCurrentContext());
    Window root;
    Window child;
    int root_x;
    int root_y;
    int x;
    int y;
    unsigned int mask;
    XEvent event;

    if (!XQueryPointer(
            display, window, &root, &child, &root_x, &root_y, &x, &y, &mask
        )) {
        return;
    }
    XUngrabPointer(display, CurrentTime);
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(
        display, "_NET_WM_MOVERESIZE", False
    );
    event.xclient.format = 32;
    event.xclient.data.l[0] = root_x;
    event.xclient.data.l[1] = root_y;
    event.xclient.data.l[2] = direction;
    event.xclient.data.l[3] = Button1;
    event.xclient.data.l[4] = 1;
    XSendEvent(
        display,
        root,
        False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &event
    );

    /* The window manager keeps the button release, so GLFW would go on
     * believing the button is held and drop the next click. */
    memset(&event, 0, sizeof(event));
    event.xbutton.type = ButtonRelease;
    event.xbutton.window = window;
    event.xbutton.root = root;
    event.xbutton.time = CurrentTime;
    event.xbutton.x = x;
    event.xbutton.y = y;
    event.xbutton.x_root = root_x;
    event.xbutton.y_root = root_y;
    event.xbutton.state = mask;
    event.xbutton.button = Button1;
    event.xbutton.same_screen = True;
    XSendEvent(display, window, False, ButtonReleaseMask, &event);
    XFlush(display);
}
