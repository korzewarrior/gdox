#ifndef GDOX_UI_WINDOW_FRAME_NATIVE_H
#define GDOX_UI_WINDOW_FRAME_NATIVE_H

/* The platform half of the self-drawn frame: window_frame_x11.c on Linux,
 * window_frame_win32.c on Windows. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* _NET_WM_MOVERESIZE directions, also used on Windows. */
enum {
    GDOX_FRAME_TOP_LEFT = 0,
    GDOX_FRAME_TOP = 1,
    GDOX_FRAME_TOP_RIGHT = 2,
    GDOX_FRAME_RIGHT = 3,
    GDOX_FRAME_BOTTOM_RIGHT = 4,
    GDOX_FRAME_BOTTOM = 5,
    GDOX_FRAME_BOTTOM_LEFT = 6,
    GDOX_FRAME_LEFT = 7,
    GDOX_FRAME_MOVE = 8
};

/* True when the open window can be moved and resized from the client area:
 * an X11 window (also under XWayland) or a Win32 window. */
bool gdox_window_frame_native_ready(void);

/* Moves or resizes the window with the pressed pointer until the button is
 * released. */
void gdox_window_frame_native_drag(int direction);

/* Follows the pointer while a drag is under way; call once a frame, before
 * drawing. The X11 window manager needs no help. */
void gdox_window_frame_native_update(void);

#ifdef __cplusplus
}
#endif

#endif
