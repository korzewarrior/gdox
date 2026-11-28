#ifndef GDOX_UI_AERO_HPP
#define GDOX_UI_AERO_HPP

#include "imgui.h"

#include <cstddef>

/* Windows 7 (Aero) versions of the controls GDOX uses. Everything is drawn
 * with ImDrawList, so the look is the same on every desktop. */
namespace gdox::ui::aero {

constexpr ImU32 rgb(unsigned int value, unsigned int alpha = 255U)
{
    return IM_COL32(
        (value >> 16U) & 255U, (value >> 8U) & 255U, value & 255U, alpha
    );
}

struct palette {
    ImU32 face;
    ImU32 window;
    ImU32 text;
    ImU32 gray_text;
    ImU32 disabled_text;
    ImU32 heading_text;
    ImU32 group_text;
    ImU32 link_text;
    ImU32 highlight;
    ImU32 control_border;
    ImU32 hot_border;
};

inline constexpr palette windows_7_colors = {
    rgb(0xF0F0F0), rgb(0xFFFFFF), rgb(0x000000), rgb(0x6D6D6D),
    rgb(0x838383), rgb(0x003399), rgb(0x1E3287), rgb(0x0066CC),
    rgb(0x3399FF), rgb(0x898C95), rgb(0x3C7FB1),
};

/* The active theme's colours, and whether it uses ImGui's own widgets
 * instead of the Windows 7 ones. Both are set by apply_theme(). */
inline palette color = windows_7_colors;
inline bool plain = false;

enum class look { normal, hot, pressed, disabled };

ImU32 mix(ImU32 from, ImU32 to, float amount);

/* The two-band vertical gradient of Windows 7 buttons: top, above the
 * middle, below the middle, bottom. */
void glossy(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    float rounding,
    ImDrawFlags corners,
    const ImU32 stops[4],
    bool sideways = false
);
/* Recolours the vertices added since `first` (drawn opaque white) with a
 * gradient from one point to another, keeping anti-aliased edge alpha. */
void shade(
    ImDrawList *draw,
    int first,
    const ImVec2 &from,
    const ImVec2 &to,
    ImU32 from_color,
    ImU32 to_color
);
void gradient(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    float rounding,
    ImDrawFlags corners,
    ImU32 top,
    ImU32 bottom
);
/* Push button face without a label. */
void button_face(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    look state,
    bool is_default
);

/* A width of zero fits the label (at least a standard 75 px button), a
 * negative one fills the row. */
bool button(
    const char *label,
    const ImVec2 &size = ImVec2(0.0F, 0.0F),
    bool is_default = false
);
/* Tab row; returns the tab clicked or -1. The page border belongs under the
 * row's bottom edge and must be drawn first. */
int tabs(const char *const *labels, int count, int selected);
/* Hyperlink: underlined only under the pointer. */
bool link(const char *label);
bool checkbox(const char *label, bool *value);
bool radio(const char *label, int *value, int option);
/* Drop-down list; a true result is closed with end_combo(). */
bool begin_combo(const char *id, const char *preview);
bool combo_item(const char *label, bool selected);
void end_combo();
bool combo(const char *id, int *current, const char *const *items, int count);
bool edit(
    const char *id,
    char *buffer,
    std::size_t capacity,
    ImGuiInputTextFlags flags = 0
);
/* Read-only edit box: the text can be scrolled, selected and copied. */
void field(const char *id, const char *value, bool dim = false);
void progress(float fraction, float height = 15.0F);
/* Paints a Windows 7 scroll bar over the vertical one ImGui drew for the
 * current window; call it right after the window begins. */
void skin_scrollbar(bool always_shown);
/* 12 point "main instruction" text. */
void heading(const char *text);
/* Group title followed by a rule to the right edge. */
void group(const char *text);
void tooltip(const char *text);

}

#endif
