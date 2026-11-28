#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/window_frame.hpp"

#include "ui/aero.hpp"
#include "ui/window_frame_native.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gdox::ui::frame {
namespace {

using aero::rgb;

/* Windows 7 frame metrics at 96 dpi, the ones Aerode uses for its frames. */
constexpr float border = 8.0F;
constexpr float caption = 22.0F;
constexpr float top_radius = 6.0F;
constexpr float bottom_radius = 2.0F;
constexpr float resize_strip = 4.0F;
constexpr float resize_corner = 16.0F;
constexpr float pi = 3.14159265F;

enum caption_button { close_button, maximize_button, minimize_button };
/* Right to left. */
constexpr float button_widths[] = {47.0F, 27.0F, 26.0F};
constexpr int button_count = 3;

constexpr ImU32 glass_stops[4] = {
    rgb(0xFFFFFF, 150U), rgb(0xFFFFFF, 90U),
    rgb(0xFFFFFF, 30U), rgb(0xFFFFFF, 80U),
};
constexpr ImU32 dim_stops[4] = {
    rgb(0xFFFFFF, 100U), rgb(0xFFFFFF, 60U),
    rgb(0xFFFFFF, 20U), rgb(0xFFFFFF, 50U),
};
constexpr ImU32 hot_stops[4] = {
    rgb(0xB9DDF7), rgb(0x8CC2EC), rgb(0x3F97E0), rgb(0x64BCF3),
};
constexpr ImU32 pressed_stops[4] = {
    rgb(0x86A8C6), rgb(0x6689AC), rgb(0x1F5F96), rgb(0x3582BD),
};
constexpr ImU32 close_stops[4] = {
    rgb(0xE9A99B), rgb(0xD9836F), rgb(0xC3432B), rgb(0xD2674C),
};
constexpr ImU32 close_hot_stops[4] = {
    rgb(0xFCC3B5), rgb(0xF59A84), rgb(0xE1401F), rgb(0xF27D55),
};
constexpr ImU32 close_pressed_stops[4] = {
    rgb(0xC98A7E), rgb(0xB4604F), rgb(0x8E1D0A), rgb(0xA8432E),
};

bool requested = false;
bool attached = false;
bool shown = true;
bool caption_held = false;
int pressed_button = -1;

[[maybe_unused]] bool desktop_frame_wanted()
{
    const char *keep = std::getenv("GDOX_NATIVE_FRAME");
    const char *desktop = std::getenv("XDG_CURRENT_DESKTOP");

    return (keep != nullptr && std::strcmp(keep, "1") == 0)
        || (desktop != nullptr && std::strstr(desktop, "Aerode") != nullptr);
}

#if defined(__linux__) || defined(_WIN32)
#define GDOX_SELF_DRAWN_FRAME 1
#endif

bool platform_ready()
{
#ifdef GDOX_SELF_DRAWN_FRAME
    return gdox_window_frame_native_ready();
#else
    return false;
#endif
}

void platform_drag(int direction)
{
#ifdef GDOX_SELF_DRAWN_FRAME
    gdox_window_frame_native_drag(direction);
#else
    (void)direction;
#endif
}

void platform_update()
{
#ifdef GDOX_SELF_DRAWN_FRAME
    gdox_window_frame_native_update();
#endif
}

bool contains(const ImVec2 &min, const ImVec2 &max, const ImVec2 &point)
{
    return point.x >= min.x && point.x < max.x
        && point.y >= min.y && point.y < max.y;
}

/* The frame outline: rounder on top than at the bottom. */
void outline(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    float top,
    float bottom
)
{
    if (top <= 0.0F) {
        draw->PathRect(min, max);
        return;
    }
    draw->PathArcTo(min + ImVec2(top, top), top, pi, pi * 1.5F);
    draw->PathArcTo(
        ImVec2(max.x - top, min.y + top), top, pi * 1.5F, pi * 2.0F
    );
    draw->PathArcTo(
        max - ImVec2(bottom, bottom), bottom, 0.0F, pi * 0.5F
    );
    draw->PathArcTo(
        ImVec2(min.x + bottom, max.y - bottom), bottom, pi * 0.5F, pi
    );
}

void paint_glass(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    float top_height,
    bool focused,
    bool maximized
)
{
    /* Each streak: left edge as a fraction of the width, then its width. */
    static constexpr float streaks[][2] = {
        {0.10F, 70.0F}, {0.27F, 22.0F}, {0.48F, 120.0F}, {0.79F, 44.0F},
    };
    const float top = maximized ? 0.0F : top_radius;
    const float bottom = maximized ? 0.0F : bottom_radius;
    const ImVec2 caption_max(max.x, min.y + top_height);

    outline(draw, min, max, top, bottom);
    draw->PathFillConvex(focused ? rgb(0xA6C3E3) : rgb(0xCBD8E8));
    aero::gradient(
        draw,
        min,
        caption_max,
        top,
        ImDrawFlags_RoundCornersTop,
        rgb(0xFFFFFF, focused ? 125U : 95U),
        rgb(0xFFFFFF, 0U)
    );

    /* The diagonal reflections of Aero glass. */
    draw->PushClipRect(
        min + ImVec2(top + 2.0F, 1.0F),
        caption_max - ImVec2(top + 2.0F, 1.0F),
        true
    );
    for (const auto &streak : streaks) {
        const float x = min.x + (max.x - min.x) * streak[0];
        draw->AddQuadFilled(
            ImVec2(x, min.y),
            ImVec2(x + streak[1], min.y),
            ImVec2(x + streak[1] - top_height, caption_max.y),
            ImVec2(x - top_height, caption_max.y),
            rgb(0xFFFFFF, focused ? 44U : 28U)
        );
    }
    draw->PopClipRect();

    if (maximized) {
        draw->AddRectFilled(
            ImVec2(min.x, caption_max.y - 1.0F),
            caption_max,
            rgb(0x000000, 120U)
        );
        return;
    }
    outline(
        draw,
        min + ImVec2(0.5F, 0.5F),
        max - ImVec2(0.5F, 0.5F),
        top,
        bottom
    );
    draw->PathStroke(
        rgb(0x000000, focused ? 175U : 110U), ImDrawFlags_Closed, 1.0F
    );
    outline(
        draw,
        min + ImVec2(1.5F, 1.5F),
        max - ImVec2(1.5F, 1.5F),
        top - 1.0F,
        bottom - 1.0F
    );
    draw->PathStroke(rgb(0xFFFFFF, 140U), ImDrawFlags_Closed, 1.0F);

    /* The edge around the client area: a highlight, then a dark line. */
    const ImVec2 client_min = min + ImVec2(border, top_height);
    const ImVec2 client_max = max - ImVec2(border, border);
    draw->AddRect(
        client_min - ImVec2(2.0F, 2.0F),
        client_max + ImVec2(2.0F, 2.0F),
        rgb(0xFFFFFF, 140U)
    );
    draw->AddRect(
        client_min - ImVec2(1.0F, 1.0F),
        client_max + ImVec2(1.0F, 1.0F),
        rgb(0x000000, focused ? 140U : 100U)
    );
}

/* A small disc for the caption, drawn so that no image file is needed. */
void paint_icon(ImDrawList *draw, const ImVec2 &center)
{
    draw->AddCircleFilled(center, 7.5F, rgb(0x55657A), 32);
    draw->AddCircleFilled(center, 6.5F, rgb(0xE6ECF3), 32);
    draw->PathArcTo(center, 4.4F, -2.5F, -1.1F, 12);
    draw->PathStroke(rgb(0xFFFFFF), 0, 3.0F);
    draw->PathArcTo(center, 4.4F, 0.65F, 2.05F, 12);
    draw->PathStroke(rgb(0xB4C3D4), 0, 3.0F);
    draw->AddCircleFilled(center, 2.6F, rgb(0x2E8F2E), 16);
    draw->AddCircleFilled(center, 1.3F, rgb(0xE6ECF3), 12);
}

void paint_title(
    ImDrawList *draw,
    const char *title,
    const ImVec2 &position,
    bool focused
)
{
    const ImVec2 size = ImGui::CalcTextSize(title);

    /* The soft white glow Aero puts behind caption text. */
    for (int layer = 0; layer < 5; ++layer) {
        const float grow = 2.0F + static_cast<float>(layer) * 3.0F;
        draw->AddRectFilled(
            position - ImVec2(grow + 3.0F, grow * 0.45F - 1.0F),
            position + size + ImVec2(grow + 3.0F, grow * 0.45F - 1.0F),
            rgb(0xFFFFFF, focused ? 30U : 20U),
            size.y * 0.5F + grow
        );
    }
    draw->AddText(position, rgb(0x000000, focused ? 255U : 140U), title);
}

void paint_box_glyph(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    ImU32 ink,
    ImU32 edge
)
{
    /* A hollow window: thicker along the top, like a title bar. */
    draw->AddRect(min, max, edge);
    draw->AddRectFilled(
        min + ImVec2(1.0F, 1.0F), ImVec2(max.x - 1.0F, min.y + 3.0F), ink
    );
    draw->AddRect(min + ImVec2(1.0F, 1.0F), max - ImVec2(1.0F, 1.0F), ink);
    draw->AddRect(min + ImVec2(2.0F, 3.0F), max - ImVec2(2.0F, 2.0F), edge);
}

void paint_glyph(
    ImDrawList *draw,
    caption_button button,
    const ImVec2 &center,
    bool maximized,
    ImU32 ink
)
{
    const ImU32 edge = rgb(0x1B2738, 215U);

    switch (button) {
        case minimize_button:
            draw->AddRectFilled(
                center + ImVec2(-5.0F, 1.0F),
                center + ImVec2(5.0F, 6.0F),
                edge
            );
            draw->AddRectFilled(
                center + ImVec2(-4.0F, 2.0F),
                center + ImVec2(4.0F, 5.0F),
                ink
            );
            break;
        case maximize_button:
            if (maximized) {
                /* Two windows; the back one shows only above and to the
                 * right of the front one. */
                const ImVec2 back_min = center + ImVec2(-3.0F, -6.0F);
                const ImVec2 back_max = center + ImVec2(6.0F, 2.0F);
                const ImVec2 front_min = center + ImVec2(-6.0F, -3.0F);
                const ImVec2 front_max = center + ImVec2(3.0F, 5.0F);

                draw->PushClipRect(
                    back_min, ImVec2(back_max.x, front_min.y), true
                );
                paint_box_glyph(draw, back_min, back_max, ink, edge);
                draw->PopClipRect();
                draw->PushClipRect(
                    ImVec2(front_max.x, front_min.y), back_max, true
                );
                paint_box_glyph(draw, back_min, back_max, ink, edge);
                draw->PopClipRect();
                paint_box_glyph(draw, front_min, front_max, ink, edge);
            } else {
                paint_box_glyph(
                    draw,
                    center + ImVec2(-6.0F, -5.0F),
                    center + ImVec2(6.0F, 5.0F),
                    ink,
                    edge
                );
            }
            break;
        case close_button:
            for (int pass = 0; pass < 2; ++pass) {
                const ImU32 color = pass == 0 ? edge : ink;
                const float thickness = pass == 0 ? 4.6F : 2.3F;
                draw->AddLine(
                    center + ImVec2(-4.5F, -4.0F),
                    center + ImVec2(4.5F, 4.0F),
                    color,
                    thickness
                );
                draw->AddLine(
                    center + ImVec2(4.5F, -4.0F),
                    center + ImVec2(-4.5F, 4.0F),
                    color,
                    thickness
                );
            }
            break;
    }
}

void paint_button(
    ImDrawList *draw,
    caption_button button,
    const ImVec2 &min,
    const ImVec2 &max,
    aero::look state,
    bool focused,
    bool maximized
)
{
    const bool is_close = button == close_button;
    const ImDrawFlags corners = is_close
        ? ImDrawFlags_RoundCornersBottomRight
        : button == minimize_button
            ? ImDrawFlags_RoundCornersBottomLeft
            : ImDrawFlags_RoundCornersNone;
    const ImU32 *stops = !focused ? dim_stops
        : is_close ? close_stops : glass_stops;

    if (state == aero::look::hot) {
        stops = is_close ? close_hot_stops : hot_stops;
    } else if (state == aero::look::pressed) {
        stops = is_close ? close_pressed_stops : pressed_stops;
    }
    if (state == aero::look::hot) {
        /* Hot buttons light the glass around them. */
        for (int layer = 1; layer <= 5; ++layer) {
            const float grow = static_cast<float>(layer);
            draw->AddRect(
                min - ImVec2(grow, grow),
                max + ImVec2(grow, grow),
                rgb(
                    is_close ? 0xFF5A32 : 0x56B9F5,
                    static_cast<unsigned int>(96 - layer * 17)
                ),
                4.0F + grow
            );
        }
    }
    aero::glossy(
        draw,
        min + ImVec2(1.0F, 0.0F),
        max - ImVec2(1.0F, 1.0F),
        3.0F,
        corners,
        stops
    );
    draw->AddRect(
        min + ImVec2(1.0F, 0.0F),
        max - ImVec2(1.0F, 1.0F),
        rgb(0xFFFFFF, 105U),
        3.0F,
        corners
    );
    /* One row higher, so that only the sides and the bottom are outlined. */
    draw->AddRect(
        min - ImVec2(0.0F, 1.0F),
        max,
        rgb(0x000000, focused ? 150U : 95U),
        4.0F,
        corners
    );
    paint_glyph(
        draw,
        button,
        ImVec2(
            std::floor((min.x + max.x) * 0.5F),
            std::floor((min.y + max.y) * 0.5F)
        ),
        maximized,
        rgb(0xFFFFFF, focused || state != aero::look::normal ? 255U : 190U)
    );
}

/* The _NET_WM_MOVERESIZE direction of the border under the pointer, or -1. */
int resize_direction(const ImVec2 &min, const ImVec2 &max, const ImVec2 &point)
{
    const bool left = point.x < min.x + border;
    const bool right = point.x >= max.x - border;
    const bool top = point.y < min.y + resize_strip;
    const bool bottom = point.y >= max.y - border;
    const bool near_left = point.x < min.x + resize_corner;
    const bool near_right = point.x >= max.x - resize_corner;
    const bool near_top = point.y < min.y + resize_corner;
    const bool near_bottom = point.y >= max.y - resize_corner;

    if ((top && near_left) || (left && near_top)) {
        return GDOX_FRAME_TOP_LEFT;
    }
    if ((top && near_right) || (right && near_top)) {
        return GDOX_FRAME_TOP_RIGHT;
    }
    if ((bottom && near_left) || (left && near_bottom)) {
        return GDOX_FRAME_BOTTOM_LEFT;
    }
    if ((bottom && near_right) || (right && near_bottom)) {
        return GDOX_FRAME_BOTTOM_RIGHT;
    }
    return top ? GDOX_FRAME_TOP
        : bottom ? GDOX_FRAME_BOTTOM
        : left ? GDOX_FRAME_LEFT
        : right ? GDOX_FRAME_RIGHT : -1;
}

ImGuiMouseCursor resize_cursor(int direction)
{
    switch (direction) {
        case GDOX_FRAME_TOP:
        case GDOX_FRAME_BOTTOM:
            return ImGuiMouseCursor_ResizeNS;
        case GDOX_FRAME_LEFT:
        case GDOX_FRAME_RIGHT:
            return ImGuiMouseCursor_ResizeEW;
        case GDOX_FRAME_TOP_LEFT:
        case GDOX_FRAME_BOTTOM_RIGHT:
            return ImGuiMouseCursor_ResizeNWSE;
        default:
            return ImGuiMouseCursor_ResizeNESW;
    }
}

}

unsigned int window_flags(bool gaming_mode)
{
#ifdef GDOX_SELF_DRAWN_FRAME
    requested = !gaming_mode && !desktop_frame_wanted();
#else
    (void)gaming_mode;
    requested = false;
#endif
    /* The transparent framebuffer is what rounds the corners. */
    return requested ? FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TRANSPARENT : 0U;
}

void attach()
{
    attached = requested && platform_ready();
    if (requested && !attached) {
        /* Not a window we can move ourselves (Wayland): keep the desktop's. */
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
    }
    caption_held = false;
    pressed_button = -1;
}

bool active()
{
    return attached && shown;
}

void show(bool visible)
{
    if (!attached || visible == shown) {
        return;
    }
    shown = visible;
    /* Keep the client area: the desktop's frame replaces ours. */
    const ImVec2 frame = extent();
    const int sign = visible ? 1 : -1;
    if (visible) {
        SetWindowState(FLAG_WINDOW_UNDECORATED);
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
    }
    if (!IsWindowMaximized()) {
        SetWindowSize(
            GetScreenWidth() + sign * static_cast<int>(frame.x),
            GetScreenHeight() + sign * static_cast<int>(frame.y)
        );
    }
}

void update()
{
    if (active()) {
        platform_update();
    }
}

ImVec2 extent()
{
    return requested
        ? ImVec2(border * 2.0F, border * 2.0F + caption)
        : ImVec2(0.0F, 0.0F);
}

bool draw(const char *title, ImVec2 &position, ImVec2 &size)
{
    if (!active()) {
        return false;
    }
    const bool maximized = IsWindowMaximized();
    const bool focused = IsWindowFocused();
    const float edge = maximized ? 0.0F : border;
    const float top_height = edge + caption;
    const ImVec2 min = position;
    const ImVec2 max = position + size;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    /* raylib keeps the last position once the pointer has left the window. */
    const bool pointer_free = IsCursorOnScreen()
        && contains(min, max, mouse)
        && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)
        && !ImGui::IsAnyItemActive();
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    ImVec2 button_min[button_count];
    ImVec2 button_max[button_count];
    float right = max.x - (maximized ? 2.0F : border - 2.0F);
    int hot = -1;
    bool close_clicked = false;

    for (int index = 0; index < button_count; ++index) {
        button_max[index] = ImVec2(
            right, min.y + (maximized ? 18.0F : 21.0F)
        );
        button_min[index] = ImVec2(
            right - button_widths[index], min.y + (maximized ? 0.0F : 1.0F)
        );
        /* Neighbours share a border column. */
        right = button_min[index].x + 1.0F;
        if (pointer_free
            && contains(button_min[index], button_max[index], mouse)) {
            hot = index;
        }
    }

    paint_glass(draw, min, max, top_height, focused, maximized);
    paint_icon(
        draw,
        min + (maximized ? ImVec2(12.0F, 11.0F) : ImVec2(16.0F, 14.0F))
    );
    draw->PushClipRect(
        min + ImVec2(1.0F, 1.0F),
        ImVec2(right - 6.0F, min.y + top_height - 1.0F),
        true
    );
    paint_title(
        draw,
        title,
        min + ImVec2(
            maximized ? 25.0F : 29.0F,
            std::floor(
                (caption - ImGui::GetFontSize()) * 0.5F
                    + (maximized ? 0.0F : 2.0F)
            )
        ),
        focused
    );
    draw->PopClipRect();
    draw->PushClipRect(min + ImVec2(1.0F, 1.0F), max, true);
    for (int pass = 0; pass < 2; ++pass) {
        /* The hot button last: its glow lies over its neighbours. */
        for (int index = 0; index < button_count; ++index) {
            if ((index == hot) != (pass == 1)) {
                continue;
            }
            paint_button(
                draw,
                static_cast<caption_button>(index),
                button_min[index],
                button_max[index],
                index != hot || (pressed_button >= 0 && pressed_button != hot)
                    ? aero::look::normal
                    : pressed_button == hot
                        ? aero::look::pressed : aero::look::hot,
                focused,
                maximized
            );
        }
    }
    draw->PopClipRect();

    const int direction = pointer_free && hot < 0 && !maximized
        ? resize_direction(min, max, mouse) : -1;
    if (direction >= 0) {
        ImGui::SetMouseCursor(resize_cursor(direction));
    }
    if (pointer_free && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hot >= 0) {
            pressed_button = hot;
        } else if (direction >= 0) {
            platform_drag(direction);
        } else if (mouse.y < min.y + top_height) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (maximized) {
                    RestoreWindow();
                } else {
                    MaximizeWindow();
                }
            } else {
                caption_held = true;
            }
        }
    }
    if (caption_held
        && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0F)) {
        caption_held = false;
        platform_drag(GDOX_FRAME_MOVE);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (pressed_button >= 0 && pressed_button == hot) {
            switch (static_cast<caption_button>(hot)) {
                case close_button:
                    close_clicked = true;
                    break;
                case maximize_button:
                    if (maximized) {
                        RestoreWindow();
                    } else {
                        MaximizeWindow();
                    }
                    break;
                case minimize_button:
                    MinimizeWindow();
                    break;
            }
        }
        pressed_button = -1;
        caption_held = false;
    }

    position = min + ImVec2(edge, top_height);
    size = size - ImVec2(edge * 2.0F, top_height + edge);
    return close_clicked;
}

}
