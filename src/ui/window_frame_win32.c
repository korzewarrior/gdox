#include "ui/window_frame_native.h"

/* Kept apart from the raylib sources: windows.h and raylib.h both define
 * Rectangle, CloseWindow and LoadImage. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>

/* raylib embeds GLFW without exposing its headers. */
typedef struct GLFWwindow GLFWwindow;
extern GLFWwindow *glfwGetCurrentContext(void);
extern HWND glfwGetWin32Window(GLFWwindow *window);

enum { edge_left = 1, edge_top = 2, edge_right = 4, edge_bottom = 8 };

/* The edges each resize direction moves, in GDOX_FRAME_* order. */
static const int moved_edges[GDOX_FRAME_MOVE] = {
    edge_left | edge_top, edge_top, edge_top | edge_right, edge_right,
    edge_right | edge_bottom, edge_bottom, edge_bottom | edge_left, edge_left,
};

/* Undecorated windows have no sizing border, so Windows cannot resize them
 * and GDOX follows the pointer itself, once a frame. */
static int dragging = -1;
static RECT start;
static POINT origin;

static HWND window_handle(void)
{
    return glfwGetWin32Window(glfwGetCurrentContext());
}

bool gdox_window_frame_native_ready(void)
{
    return glfwGetCurrentContext() != NULL;
}

void gdox_window_frame_native_drag(int direction)
{
    const HWND window = window_handle();

    GetCursorPos(&origin);
    GetWindowRect(window, &start);
    if (direction == GDOX_FRAME_MOVE && IsZoomed(window)) {
        /* Restore under the pointer, at the same place across the caption. */
        const double across = (double)(origin.x - start.left)
            / (double)(start.right - start.left);
        const LONG top = start.top;

        ShowWindow(window, SW_RESTORE);
        GetWindowRect(window, &start);
        OffsetRect(
            &start,
            origin.x - start.left
                - (LONG)(across * (double)(start.right - start.left)),
            top - start.top
        );
        SetWindowPos(
            window, NULL, start.left, start.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
    dragging = direction;
}

void gdox_window_frame_native_update(void)
{
    const HWND window = window_handle();
    MINMAXINFO limits;
    POINT pointer;
    RECT target = start;
    LONG dx;
    LONG dy;
    int edges;

    if (dragging < 0) {
        return;
    }
    if (GetKeyState(VK_LBUTTON) >= 0) {
        dragging = -1;
        return;
    }
    GetCursorPos(&pointer);
    dx = pointer.x - origin.x;
    dy = pointer.y - origin.y;
    if (dragging == GDOX_FRAME_MOVE) {
        OffsetRect(&target, dx, dy);
        SetWindowPos(
            window, NULL, target.left, target.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        return;
    }

    /* GLFW answers with the minimum size GDOX set. */
    ZeroMemory(&limits, sizeof(limits));
    SendMessageW(window, WM_GETMINMAXINFO, 0, (LPARAM)&limits);
    edges = moved_edges[dragging];
    if (edges & edge_left) {
        target.left = min(start.left + dx, start.right - limits.ptMinTrackSize.x);
    }
    if (edges & edge_right) {
        target.right = max(start.right + dx, start.left + limits.ptMinTrackSize.x);
    }
    if (edges & edge_top) {
        target.top = min(start.top + dy, start.bottom - limits.ptMinTrackSize.y);
    }
    if (edges & edge_bottom) {
        target.bottom = max(start.bottom + dy, start.top + limits.ptMinTrackSize.y);
    }
    SetWindowPos(
        window,
        NULL,
        target.left,
        target.top,
        target.right - target.left,
        target.bottom - target.top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}
