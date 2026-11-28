#ifndef GDOX_UI_WINDOW_FRAME_HPP
#define GDOX_UI_WINDOW_FRAME_HPP

#include "imgui.h"

/* The Windows 7 Aero window frame GDOX draws for itself on Linux and Windows,
 * so that it looks the same everywhere. Aerode already frames its windows
 * that way, and GDOX_NATIVE_FRAME=1 keeps the desktop's own frame. */
namespace gdox::ui::frame {

/* raylib window flags to add before InitWindow(). */
unsigned int window_flags(bool gaming_mode);
/* Call once the window exists. */
void attach();
bool active();
/* Shows or hides the frame (the desktop's own frame takes over). */
void show(bool visible);
/* Moves or resizes the window during a frame drag; call before drawing. */
void update();
/* Width and height the frame adds around the client area. */
ImVec2 extent();
/* Paints the frame and narrows the rectangle to the client area. Returns true
 * when the Close button was clicked. */
bool draw(const char *title, ImVec2 &position, ImVec2 &size);

}

#endif
