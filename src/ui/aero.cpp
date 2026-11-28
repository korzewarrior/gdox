#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/aero.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace gdox::ui::aero {
namespace {

struct skin {
    ImU32 border;
    ImU32 stops[4];
};

/* Indexed by look. The values are those of Aerode's Windows 7 theme. */
constexpr skin button_skins[] = {
    {rgb(0x707070),
        {rgb(0xF2F2F2), rgb(0xEBEBEB), rgb(0xDDDDDD), rgb(0xCFCFCF)}},
    {rgb(0x3C7FB1),
        {rgb(0xEAF6FD), rgb(0xD9F0FC), rgb(0xBEE6FD), rgb(0xA7D9F5)}},
    {rgb(0x2C628B),
        {rgb(0xE5F4FC), rgb(0xC4E5F6), rgb(0x98D1EF), rgb(0x68B3DB)}},
    {rgb(0xADB2B5),
        {rgb(0xF4F4F4), rgb(0xF4F4F4), rgb(0xF4F4F4), rgb(0xF4F4F4)}},
};

/* Check box and radio button wells, indexed by look: border, top, bottom. */
constexpr ImU32 well_skins[][3] = {
    {rgb(0x8E8F8F), rgb(0xDCDCDC), rgb(0xFFFFFF)},
    {rgb(0x3C7FB1), rgb(0xB1DFFD), rgb(0xE9F7FE)},
    {rgb(0x2C628B), rgb(0x7FC5F2), rgb(0xD6EEFC)},
    {rgb(0xBCBCBC), rgb(0xF4F4F4), rgb(0xF4F4F4)},
};
constexpr ImU32 mark = rgb(0x4D6185);
constexpr ImU32 disabled_mark = rgb(0xA0A0A0);
constexpr float well = 13.0F;
constexpr float well_gap = 6.0F;

/* An open drop-down list highlights its current row until the pointer picks
 * another one, as Windows does. */
bool list_row_hot = false;
bool list_row_was_hot = false;

const char *visible_end(const char *label)
{
    const char *end = std::strstr(label, "##");

    return end != nullptr ? end : label + std::strlen(label);
}

look item_look()
{
    if ((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0) {
        return look::disabled;
    }
    if (ImGui::IsItemActive()) {
        return ImGui::IsItemHovered() ? look::pressed : look::hot;
    }
    return ImGui::IsItemHovered() ? look::hot : look::normal;
}

bool item_has_keyboard()
{
    return ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
}

ImVec2 snapped(float x, float y)
{
    return ImVec2(std::floor(x), std::floor(y));
}

/* Edit box rim of the last item: darker on top, blue under the pointer. */
void edit_rim()
{
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImU32 top = rgb(0xABADB3);
    ImU32 side = rgb(0xE2E3EA);

    if (ImGui::IsItemActive()) {
        top = rgb(0x3D7BAD);
        side = rgb(0xA4C9E3);
    } else if (ImGui::IsItemHovered()) {
        top = rgb(0x5794BF);
        side = rgb(0xB7D5EA);
    }
    draw->AddRect(min, max, side);
    draw->AddRectFilled(min, ImVec2(max.x, min.y + 1.0F), top);
}

/* Shared by check boxes and radio buttons: an item with a 13 px well and a
 * label. Returns the well's top-left corner. */
ImVec2 well_item(const char *label, bool &pressed, look &state)
{
    const char *end = visible_end(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, end);
    const ImVec2 size(
        well + well_gap + label_size.x,
        std::max(well, label_size.y) + 4.0F
    );

    pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
    state = item_look();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(
        snapped(
            min.x + well + well_gap,
            min.y + (size.y - label_size.y) * 0.5F
        ),
        state == look::disabled ? color.disabled_text : color.text,
        label,
        end
    );
    return snapped(min.x, min.y + (size.y - well) * 0.5F);
}

}

ImU32 mix(ImU32 from, ImU32 to, float amount)
{
    ImU32 result = 0U;

    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        const float a = static_cast<float>((from >> shift) & 255U);
        const float b = static_cast<float>((to >> shift) & 255U);
        result |= static_cast<ImU32>(a + (b - a) * amount + 0.5F) << shift;
    }
    return result;
}

/* Recolours the vertices added since `first`, which must be opaque white,
 * with a gradient running from one point to the other. Their alpha keeps the
 * anti-aliased edges. */
void shade(
    ImDrawList *draw,
    int first,
    const ImVec2 &from,
    const ImVec2 &to,
    ImU32 from_color,
    ImU32 to_color
)
{
    const ImVec2 axis = to - from;
    const float length = std::max(1.0F, axis.x * axis.x + axis.y * axis.y);

    for (int index = first; index < draw->VtxBuffer.Size; ++index) {
        ImDrawVert &vertex = draw->VtxBuffer[index];
        const ImVec2 offset = vertex.pos - from;
        const ImU32 shaded = mix(
            from_color,
            to_color,
            std::clamp(
                (offset.x * axis.x + offset.y * axis.y) / length, 0.0F, 1.0F
            )
        );
        const ImU32 alpha = ((vertex.col >> IM_COL32_A_SHIFT) & 255U)
            * ((shaded >> IM_COL32_A_SHIFT) & 255U) / 255U;
        vertex.col = (shaded & ~IM_COL32_A_MASK)
            | (alpha << IM_COL32_A_SHIFT);
    }
}

void gradient(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    float rounding,
    ImDrawFlags corners,
    ImU32 top,
    ImU32 bottom
)
{
    const int first = draw->VtxBuffer.Size;

    draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, corners);
    shade(draw, first, min, ImVec2(min.x, max.y), top, bottom);
}

void glossy(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    float rounding,
    ImDrawFlags corners,
    const ImU32 stops[4],
    bool sideways
)
{
    /* Each band is the whole shape clipped to its half, so the rounded
     * corners stay intact and the break between the bands is sharp. */
    const ImVec2 middle = sideways
        ? ImVec2(std::floor((min.x + max.x) * 0.5F), max.y)
        : ImVec2(max.x, std::floor((min.y + max.y) * 0.5F));
    const ImVec2 start = sideways
        ? ImVec2(middle.x, min.y) : ImVec2(min.x, middle.y);
    const ImVec2 along = sideways ? ImVec2(1.0F, 0.0F) : ImVec2(0.0F, 1.0F);
    const float half = sideways ? middle.x - min.x : middle.y - min.y;
    const float rest = sideways ? max.x - middle.x : max.y - middle.y;
    int first;

    draw->PushClipRect(min, middle, true);
    first = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, corners);
    shade(draw, first, min, min + along * half, stops[0], stops[1]);
    draw->PopClipRect();
    draw->PushClipRect(start, max, true);
    first = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, corners);
    shade(draw, first, start, start + along * rest, stops[2], stops[3]);
    draw->PopClipRect();
}

void button_face(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    look state,
    bool is_default
)
{
    const skin &face = button_skins[static_cast<int>(state)];
    const ImVec2 inset(1.0F, 1.0F);
    const bool ring = is_default && state == look::normal;

    draw->AddRectFilled(
        min, max, ring ? color.highlight : face.border, 3.0F
    );
    glossy(draw, min + inset, max - inset, 2.0F, 0, face.stops);
    if (state == look::normal || state == look::hot) {
        draw->AddRect(
            min + inset,
            max - inset,
            ring ? rgb(0x4CC6FF) : rgb(0xFFFFFF, 190U),
            2.0F
        );
    }
    if (ring) {
        /* The default button's glow breathes, one second each way. */
        const float phase = std::fmod(
            static_cast<float>(ImGui::GetTime()), 2.0F
        );
        const float level = phase < 1.0F ? phase : 2.0F - phase;
        draw->AddRect(
            min + inset * 2.0F,
            max - inset * 2.0F,
            rgb(0x4CC6FF, static_cast<unsigned int>(level * 150.0F)),
            1.0F
        );
    }
}

bool button(const char *label, const ImVec2 &size_request, bool is_default)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const char *end = visible_end(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, end);
    const float fitted = label_size.x + style.FramePadding.x * 2.0F + 8.0F;
    const ImVec2 size(
        size_request.x < 0.0F
            ? ImGui::GetContentRegionAvail().x
            : std::max(size_request.x > 0.0F ? size_request.x : 75.0F, fitted),
        std::max(size_request.y, ImGui::GetFrameHeight())
    );
    if (plain) {
        return ImGui::Button(label, size);
    }
    const bool pressed = ImGui::InvisibleButton(
        label, size, ImGuiButtonFlags_EnableNav
    );
    const look state = item_look();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList *draw = ImGui::GetWindowDrawList();

    button_face(
        draw,
        min,
        ImGui::GetItemRectMax(),
        state,
        is_default || item_has_keyboard()
    );
    draw->AddText(
        snapped(
            min.x + (size.x - label_size.x) * 0.5F,
            min.y + (size.y - label_size.y) * 0.5F
        ),
        state == look::disabled ? color.disabled_text : color.text,
        label,
        end
    );
    return pressed;
}

int tabs(const char *const *labels, int count, int selected)
{
    constexpr int capacity = 8;
    constexpr float raise = 2.0F;
    const float height = ImGui::GetFontSize() + 6.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float bottom = origin.y + raise + height;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float left[capacity + 1];
    int clicked = -1;
    int hot = -1;

    count = std::min(count, capacity);
    if (plain) {
        for (int index = 0; index < count; ++index) {
            if (index > 0) {
                ImGui::SameLine(0.0F, 4.0F);
            }
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImGui::GetColorU32(
                    index == selected ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg
                )
            );
            if (ImGui::Button(labels[index])) {
                clicked = index;
            }
            ImGui::PopStyleColor();
        }
        return clicked;
    }
    left[0] = origin.x + raise;
    for (int index = 0; index < count; ++index) {
        const float width = ImGui::CalcTextSize(labels[index]).x + 14.0F;

        ImGui::SetCursorScreenPos(ImVec2(left[index], origin.y));
        if (ImGui::InvisibleButton(
                labels[index],
                ImVec2(width, raise + height),
                ImGuiButtonFlags_EnableNav
            )) {
            clicked = index;
        }
        if (index != selected
            && (ImGui::IsItemHovered() || item_has_keyboard())) {
            hot = index;
        }
        left[index + 1] = left[index] + width;
    }

    /* Neighbours share a border column; the hot and the selected tab are
     * painted last so theirs wins. */
    for (int pass = 0; pass < 3; ++pass) {
        for (int index = 0; index < count; ++index) {
            const bool is_selected = index == selected;
            const bool is_hot = index == hot;
            const float grow = is_selected ? raise : 0.0F;
            const ImVec2 min(
                left[index] - grow, origin.y + raise - grow
            );
            const ImVec2 max(left[index + 1] + 1.0F + grow, bottom);
            const ImVec2 label_size = ImGui::CalcTextSize(labels[index]);

            if (pass != (is_selected ? 2 : is_hot ? 1 : 0)) {
                continue;
            }
            draw->AddRectFilled(
                min,
                max,
                is_hot ? color.hot_border : color.control_border,
                2.0F,
                ImDrawFlags_RoundCornersTop
            );
            if (is_selected) {
                /* One row lower: the tab opens into the page. */
                draw->AddRectFilled(
                    min + ImVec2(1.0F, 1.0F),
                    max + ImVec2(-1.0F, 1.0F),
                    color.window,
                    1.0F,
                    ImDrawFlags_RoundCornersTop
                );
            } else {
                glossy(
                    draw,
                    min + ImVec2(1.0F, 1.0F),
                    max - ImVec2(1.0F, 0.0F),
                    1.0F,
                    ImDrawFlags_RoundCornersTop,
                    button_skins[is_hot ? 1 : 0].stops
                );
            }
            draw->AddText(
                snapped(
                    (min.x + max.x - label_size.x) * 0.5F,
                    min.y + (bottom - min.y - label_size.y) * 0.5F
                ),
                color.text,
                labels[index]
            );
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
    return clicked;
}

bool link(const char *label)
{
    if (plain) {
        return ImGui::TextLink(label);
    }
    const char *end = visible_end(label);
    const ImVec2 size = ImGui::CalcTextSize(label, end);
    const bool pressed = ImGui::InvisibleButton(
        label, size, ImGuiButtonFlags_EnableNav
    );
    const bool hot = ImGui::IsItemHovered() || item_has_keyboard();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImU32 ink = hot ? color.highlight : color.link_text;
    ImDrawList *draw = ImGui::GetWindowDrawList();

    draw->AddText(min, ink, label, end);
    if (hot) {
        draw->AddRectFilled(
            ImVec2(min.x, min.y + size.y - 2.0F),
            ImVec2(min.x + size.x, min.y + size.y - 1.0F),
            ink
        );
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    return pressed;
}

bool checkbox(const char *label, bool *value)
{
    if (plain) {
        return ImGui::Checkbox(label, value);
    }
    bool pressed;
    look state;
    const ImVec2 min = well_item(label, pressed, state);
    const ImVec2 max = min + ImVec2(well, well);
    const ImU32 *colors = well_skins[static_cast<int>(state)];
    ImDrawList *draw = ImGui::GetWindowDrawList();

    if (pressed) {
        *value = !*value;
    }
    draw->AddRectFilled(min, max, colors[0]);
    gradient(
        draw,
        min + ImVec2(1.0F, 1.0F),
        max - ImVec2(1.0F, 1.0F),
        0.0F,
        0,
        colors[1],
        colors[2]
    );
    draw->AddRect(
        min + ImVec2(1.0F, 1.0F),
        max - ImVec2(1.0F, 1.0F),
        rgb(0xF4F4F4, 200U)
    );
    if (*value) {
        const ImVec2 points[3] = {
            min + ImVec2(3.0F, 6.5F),
            min + ImVec2(5.5F, 9.5F),
            min + ImVec2(10.0F, 3.5F),
        };
        draw->AddPolyline(
            points,
            3,
            state == look::disabled ? disabled_mark : mark,
            0,
            2.0F
        );
    }
    return pressed;
}

bool radio(const char *label, int *value, int option)
{
    if (plain) {
        return ImGui::RadioButton(label, value, option);
    }
    bool pressed;
    look state;
    const ImVec2 min = well_item(label, pressed, state);
    const ImVec2 center = min + ImVec2(well * 0.5F, well * 0.5F);
    const ImU32 *colors = well_skins[static_cast<int>(state)];
    ImDrawList *draw = ImGui::GetWindowDrawList();

    if (pressed) {
        *value = option;
    }
    draw->AddCircleFilled(center, well * 0.5F, colors[0], 24);
    const int first = draw->VtxBuffer.Size;
    draw->AddCircleFilled(center, well * 0.5F - 1.0F, IM_COL32_WHITE, 24);
    shade(
        draw, first, min, min + ImVec2(0.0F, well), colors[1], colors[2]
    );
    if (*value == option) {
        draw->AddCircleFilled(
            center,
            3.0F,
            state == look::disabled ? disabled_mark : mark,
            16
        );
    }
    return pressed;
}

bool begin_combo(const char *id, const char *preview)
{
    if (plain) {
        return ImGui::BeginCombo(id, preview);
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(
        min.x + ImGui::CalcItemWidth(), min.y + ImGui::GetFrameHeight()
    );
    look state = look::pressed;
    bool has_keyboard = false;

    /* ImGui draws the preview text; the button face goes underneath once the
     * item's state is known. */
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0U);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0U);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0U);
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(0x646464));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0F, 2.0F));
    const bool open = ImGui::BeginCombo(
        id, preview, ImGuiComboFlags_NoArrowButton
    );
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (!open) {
        state = item_look();
        has_keyboard = item_has_keyboard();
    }
    draw->ChannelsSetCurrent(0);
    button_face(draw, min, max, state, has_keyboard);
    const ImVec2 arrow = snapped(max.x - 12.0F, (min.y + max.y) * 0.5F);
    draw->AddTriangleFilled(
        arrow + ImVec2(-4.0F, -2.0F),
        arrow + ImVec2(4.0F, -2.0F),
        arrow + ImVec2(0.0F, 2.5F),
        state == look::disabled ? color.disabled_text : color.text
    );
    draw->ChannelsMerge();
    if (open) {
        list_row_was_hot = list_row_hot;
        list_row_hot = false;
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0F)
        );
    }
    return open;
}

bool combo_item(const char *label, bool selected)
{
    if (plain) {
        const bool chosen = ImGui::Selectable(label, selected);
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
        return chosen;
    }
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 window_max = window_min + ImGui::GetWindowSize();
    ImDrawList *draw = ImGui::GetWindowDrawList();

    /* Drawn here instead: Windows lists show the hot row white on blue. */
    ImGui::PushStyleColor(ImGuiCol_Text, 0U);
    ImGui::PushStyleColor(ImGuiCol_Header, 0U);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0U);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0U);
    const bool pressed = ImGui::Selectable(label, false);
    ImGui::PopStyleColor(4);
    const bool hot = ImGui::IsItemHovered() || item_has_keyboard();
    const bool lit = hot || (selected && !list_row_was_hot);

    list_row_hot = list_row_hot || hot;
    if (lit) {
        const float scrollbar = ImGui::GetScrollMaxY() > 0.0F
            ? ImGui::GetStyle().ScrollbarSize
            : 0.0F;
        draw->PushClipRect(window_min, window_max, false);
        draw->AddRectFilled(
            ImVec2(window_min.x + 1.0F, ImGui::GetItemRectMin().y),
            ImVec2(
                window_max.x - 1.0F - scrollbar,
                ImGui::GetItemRectMax().y
            ),
            color.highlight
        );
        draw->PopClipRect();
    }
    draw->AddText(
        position,
        lit ? color.window : color.text,
        label,
        visible_end(label)
    );
    if (selected) {
        ImGui::SetItemDefaultFocus();
    }
    return pressed;
}

void end_combo()
{
    if (!plain) {
        ImGui::PopStyleVar();
    }
    ImGui::EndCombo();
}

bool combo(const char *id, int *current, const char *const *items, int count)
{
    bool changed = false;

    if (!begin_combo(
            id, *current >= 0 && *current < count ? items[*current] : ""
        )) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        ImGui::PushID(index);
        if (combo_item(items[index], index == *current)) {
            *current = index;
            changed = true;
        }
        ImGui::PopID();
    }
    end_combo();
    return changed;
}

bool edit(
    const char *id,
    char *buffer,
    std::size_t capacity,
    ImGuiInputTextFlags flags
)
{
    const bool changed = ImGui::InputText(id, buffer, capacity, flags);

    if (!plain) {
        edit_rim();
    }
    return changed;
}

void field(const char *id, const char *value, bool dim)
{
    /* ImGui wants a writable buffer even for a read-only box. */
    std::string copy(value);

    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        plain ? ImGui::GetColorU32(ImGuiCol_FrameBg) : color.face
    );
    ImGui::PushStyleColor(
        ImGuiCol_Text, dim ? color.gray_text : color.text
    );
    (void)ImGui::InputText(
        id, copy.data(), copy.size() + 1U, ImGuiInputTextFlags_ReadOnly
    );
    ImGui::PopStyleColor(2);
    if (!plain) {
        edit_rim();
    }
}

void progress(float fraction, float height)
{
    static constexpr ImU32 fill[4] = {
        rgb(0xCDF6CD), rgb(0x86E392), rgb(0x06B025), rgb(0x3BD457),
    };
    constexpr float glow = 90.0F;
    if (plain) {
        ImGui::ProgressBar(fraction, ImVec2(-1.0F, height), "");
        return;
    }
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + ImVec2(ImGui::GetContentRegionAvail().x, height);
    const ImVec2 inset(1.0F, 1.0F);
    const float width = std::floor(
        (max.x - min.x - 2.0F) * std::clamp(fraction, 0.0F, 1.0F)
    );
    ImDrawList *draw = ImGui::GetWindowDrawList();

    ImGui::Dummy(max - min);
    draw->AddRectFilled(min, max, rgb(0xB2B2B2), 3.0F);
    gradient(
        draw, min + inset, max - inset, 2.0F, 0,
        rgb(0xF3F3F3), rgb(0xD9D9D9)
    );
    if (width < 1.0F) {
        return;
    }
    const ImVec2 fill_max(min.x + 1.0F + width, max.y - 1.0F);
    glossy(draw, min + inset, fill_max, 2.0F, 0, fill);

    /* The light that travels along a Windows 7 progress bar. */
    const float travel = width + glow * 2.0F + 240.0F;
    const float x = min.x - glow + std::fmod(
        static_cast<float>(ImGui::GetTime()) * 220.0F, travel
    );
    const ImU32 clear = rgb(0xFFFFFF, 0U);
    const ImU32 bright = rgb(0xFFFFFF, 130U);
    draw->PushClipRect(min + inset, fill_max, true);
    draw->AddRectFilledMultiColor(
        ImVec2(x, min.y), ImVec2(x + glow * 0.5F, max.y),
        clear, bright, bright, clear
    );
    draw->AddRectFilledMultiColor(
        ImVec2(x + glow * 0.5F, min.y), ImVec2(x + glow, max.y),
        bright, clear, clear, bright
    );
    draw->PopClipRect();
}

void skin_scrollbar(bool always_shown)
{
    static constexpr ImU32 faces[3][4] = {
        {rgb(0xF4F4F4), rgb(0xE3E3E3), rgb(0xD7D7D7), rgb(0xE6E6E6)},
        {rgb(0xE3F4FC), rgb(0xB6E1F9), rgb(0xA4D4F2), rgb(0xC3E5F8)},
        {rgb(0xCAE9FA), rgb(0x9CD6F6), rgb(0x89C9EE), rgb(0xA8D8F4)},
    };
    static bool dragging = false;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float scroll_max = ImGui::GetScrollMaxY();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 window_max = window_min + ImGui::GetWindowSize();
    const ImVec2 min(window_max.x - style.ScrollbarSize, window_min.y);
    ImDrawList *draw = ImGui::GetWindowDrawList();

    if (plain || (scroll_max <= 0.0F && !always_shown)) {
        return;
    }
    draw->PushClipRect(window_min, window_max, false);
    draw->AddRectFilled(min, window_max, color.face);
    if (scroll_max > 0.0F) {
        /* The same arithmetic as ImGui's own grab, which lies underneath
         * and still takes the mouse. */
        const float padding = style.ScrollbarPadding;
        const float visible = window_max.y - window_min.y;
        const float track = visible - padding * 2.0F;
        const float length = std::clamp(
            track * visible / (visible + scroll_max),
            std::min(track, style.GrabMinSize),
            track
        );
        const float top = min.y + padding
            + (track - length) * std::clamp(
                ImGui::GetScrollY() / scroll_max, 0.0F, 1.0F
            );
        const ImVec2 grab_min(min.x + padding, std::floor(top));
        const ImVec2 grab_max(
            window_max.x - padding, std::floor(top + length)
        );
        const bool hot = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
            )
            && ImGui::IsMouseHoveringRect(grab_min, grab_max, false);
        const float center = std::floor((grab_min.y + grab_max.y) * 0.5F);

        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragging = true;
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragging = false;
        }
        draw->AddRectFilled(
            grab_min,
            grab_max,
            dragging ? rgb(0x18598A)
                : hot ? color.hot_border : rgb(0x959595),
            3.0F
        );
        glossy(
            draw,
            grab_min + ImVec2(1.0F, 1.0F),
            grab_max - ImVec2(1.0F, 1.0F),
            2.0F,
            0,
            faces[dragging ? 2 : hot ? 1 : 0],
            true
        );
        if (length >= 24.0F) {
            /* The three grip lines in the middle of the thumb. */
            for (int line = -1; line <= 1; ++line) {
                const float y = center + static_cast<float>(line) * 2.0F;
                draw->AddRectFilled(
                    ImVec2(grab_min.x + 4.0F, y),
                    ImVec2(grab_max.x - 4.0F, y + 1.0F),
                    dragging || hot ? rgb(0x4E7FA6) : rgb(0x8E8E8E)
                );
            }
        }
    }
    draw->PopClipRect();
}

void heading(const char *text)
{
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 4.0F / 3.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, color.heading_text);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void group(const char *text)
{
    const float right = ImGui::GetCursorScreenPos().x
        + ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleColor(ImGuiCol_Text, color.group_text);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    const float y = std::floor(
        (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F
    ) + 1.0F;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(ImGui::GetItemRectMax().x + 8.0F, y),
        ImVec2(right, y + 1.0F),
        ImGui::GetColorU32(ImGuiCol_Separator)
    );
}

void tooltip(const char *text)
{
    if (plain) {
        ImGui::SetTooltip("%s", text);
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color.window);
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(0x767676));
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(0x575757));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 3.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0F);
    ImGui::SetTooltip("%s", text);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

}
