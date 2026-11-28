#include "ui/theme.hpp"

#include "ui/aero.hpp"
#include "ui/presentation_internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gdox::ui {
namespace {

/* Segoe UI where the system has it, else the closest installed face. */
std::string interface_font()
{
    std::string path;
#if defined(_WIN32)
    const char *windows = std::getenv("WINDIR");

    if (windows != nullptr) {
        path = std::string(windows) + "\\Fonts\\segoeui.ttf";
    }
#elif !defined(__APPLE__)
    /* fontconfig knows every distribution's font directories; without it
     * ImGui's built-in font stays. */
    FILE *match = popen(
        "fc-match -f '%{file}' 'Segoe UI,Selawik,Tahoma,Open Sans'"
        " 2>/dev/null",
        "r"
    );

    if (match != nullptr) {
        char buffer[1024];
        const std::size_t length = std::fread(
            buffer, 1U, sizeof(buffer) - 1U, match
        );
        (void)pclose(match);
        path.assign(buffer, length);
    }
#endif
    FILE *file = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");

    if (file == nullptr) {
        return {};
    }
    (void)std::fclose(file);
    return path;
}

ImFont *interface_font_face = nullptr;
float interface_font_size = 0.0F;

void windows_7(ImGuiStyle &style)
{
    using aero::rgb;
    const aero::palette &color = aero::windows_7_colors;
    ImGuiIO &io = ImGui::GetIO();

    style.WindowPadding = ImVec2(11.0F, 11.0F);
    style.FramePadding = ImVec2(8.0F, 4.0F);
    style.ItemSpacing = ImVec2(7.0F, 7.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 0.0F;
    style.PopupRounding = 0.0F;
    style.ScrollbarRounding = 0.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.ScrollbarSize = 17.0F;
    style.ScrollbarPadding = 1.0F;
    /* Dims the text ImGui draws in disabled controls to Windows' grey. */
    style.DisabledAlpha = 0.45F;
    if (interface_font_face != nullptr) {
        io.FontDefault = interface_font_face;
        style.FontSizeBase = interface_font_size;
    }

    const auto set = [&style](ImGuiCol index, ImU32 value) {
        style.Colors[index] = ImGui::ColorConvertU32ToFloat4(value);
    };
    set(ImGuiCol_Text, color.text);
    set(ImGuiCol_TextDisabled, color.disabled_text);
    set(ImGuiCol_WindowBg, color.face);
    set(ImGuiCol_ChildBg, rgb(0xFFFFFF, 0U));
    set(ImGuiCol_PopupBg, color.window);
    set(ImGuiCol_Border, color.control_border);
    set(ImGuiCol_FrameBg, color.window);
    set(ImGuiCol_FrameBgHovered, color.window);
    set(ImGuiCol_FrameBgActive, color.window);
    set(ImGuiCol_Button, rgb(0xE1E1E1));
    set(ImGuiCol_ButtonHovered, rgb(0xD9F0FC));
    set(ImGuiCol_ButtonActive, rgb(0xC4E5F6));
    set(ImGuiCol_Header, color.highlight);
    set(ImGuiCol_HeaderHovered, color.highlight);
    set(ImGuiCol_HeaderActive, color.highlight);
    set(ImGuiCol_Separator, rgb(0xDADADA));
    set(ImGuiCol_ScrollbarBg, color.face);
    set(ImGuiCol_ScrollbarGrab, rgb(0xCDCDCD));
    set(ImGuiCol_ScrollbarGrabHovered, rgb(0xA6CEEC));
    set(ImGuiCol_ScrollbarGrabActive, rgb(0x7FB8E2));
    set(ImGuiCol_CheckMark, rgb(0x4D6185));
    set(ImGuiCol_TextSelectedBg, rgb(0x3399FF, 110U));
    set(ImGuiCol_NavCursor, color.highlight);
    set(ImGuiCol_TextLink, color.link_text);

    aero::color = color;
    detail::muted = ImVec4(0.427F, 0.427F, 0.427F, 1.0F);
    detail::accent = ImVec4(0.0F, 0.2F, 0.6F, 1.0F);
    detail::ready = ImVec4(0.0F, 0.45F, 0.0F, 1.0F);
    detail::warning = ImVec4(0.75F, 0.29F, 0.0F, 1.0F);
}

/* GDOX's own dark green look, with ImGui's built-in font. */
void gdox(ImGuiStyle &style)
{
    using aero::rgb;
    ImVec4 *colors = style.Colors;
    ImGuiIO &io = ImGui::GetIO();

    style.WindowPadding = ImVec2(20.0F, 18.0F);
    style.FramePadding = ImVec2(13.0F, 8.0F);
    style.ItemSpacing = ImVec2(10.0F, 10.0F);
    style.WindowRounding = 12.0F;
    style.ChildRounding = 12.0F;
    style.FrameRounding = 8.0F;
    style.PopupRounding = 10.0F;
    style.ScrollbarRounding = 10.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    io.FontDefault = nullptr;
    style.FontSizeBase = io.Fonts->Fonts.empty()
        ? 13.0F : io.Fonts->Fonts[0]->LegacySize;

    colors[ImGuiCol_Text] = ImVec4(0.91F, 0.93F, 0.94F, 1.00F);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.46F, 0.50F, 0.53F, 1.00F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.055F, 0.064F, 0.071F, 1.00F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.075F, 0.086F, 0.094F, 1.00F);
    colors[ImGuiCol_PopupBg] = ImVec4(0.070F, 0.080F, 0.087F, 0.98F);
    colors[ImGuiCol_Border] = ImVec4(0.15F, 0.17F, 0.18F, 1.00F);
    colors[ImGuiCol_FrameBg] = ImVec4(0.11F, 0.12F, 0.13F, 1.00F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15F, 0.17F, 0.18F, 1.00F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18F, 0.20F, 0.21F, 1.00F);
    colors[ImGuiCol_Button] = ImVec4(0.063F, 0.486F, 0.063F, 1.00F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.086F, 0.60F, 0.086F, 1.00F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.043F, 0.38F, 0.043F, 1.00F);
    colors[ImGuiCol_Header] = ImVec4(0.13F, 0.15F, 0.16F, 1.00F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.17F, 0.19F, 0.20F, 1.00F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20F, 0.22F, 0.23F, 1.00F);
    colors[ImGuiCol_CheckMark] = ImVec4(0.36F, 0.76F, 0.12F, 1.00F);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.063F, 0.486F, 0.063F, 1.00F);
    colors[ImGuiCol_SliderGrabActive] =
        ImVec4(0.086F, 0.60F, 0.086F, 1.00F);
    colors[ImGuiCol_PlotHistogram] =
        ImVec4(0.063F, 0.486F, 0.063F, 1.00F);
    colors[ImGuiCol_PlotHistogramHovered] =
        ImVec4(0.086F, 0.60F, 0.086F, 1.00F);
    colors[ImGuiCol_NavCursor] = ImVec4(0.36F, 0.76F, 0.12F, 1.00F);
    colors[ImGuiCol_TextLink] = ImVec4(0.36F, 0.76F, 0.12F, 1.00F);

    /* For what GDOX still paints itself: the page, status bar and disc. */
    aero::color = aero::palette{
        rgb(0x0E1012), rgb(0x131618), rgb(0xE8EDF0), rgb(0x7A858C),
        rgb(0x758087), rgb(0x5CC21F), rgb(0x5CC21F), rgb(0x5CC21F),
        rgb(0x169916), rgb(0x262B2E), rgb(0x169916),
    };
    detail::muted = ImVec4(0.48F, 0.52F, 0.55F, 1.0F);
    detail::accent = ImVec4(0.36F, 0.76F, 0.12F, 1.0F);
    detail::ready = ImVec4(0.31F, 0.70F, 0.20F, 1.0F);
    detail::warning = ImVec4(0.91F, 0.56F, 0.42F, 1.0F);
}

}

void load_fonts()
{
    const std::string path = interface_font();

    if (path.empty()) {
        return;
    }
    /* ImGui sizes a font by its line height. Segoe UI's 9 point (12 px em)
     * line is 16 px tall; most other faces are larger on the em. */
    interface_font_size = path.find("segoeui") != std::string::npos
        || path.find("selawk") != std::string::npos ? 16.0F : 15.0F;
    interface_font_face = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        path.c_str(), interface_font_size
    );
}

void apply_theme(std::uint8_t theme, bool gaming_mode)
{
    /* Each theme starts from ImGui's own style, so switching back and forth
     * leaves nothing of the other one behind. */
    static const ImGuiStyle base = ImGui::GetStyle();
    ImGuiStyle &style = ImGui::GetStyle();

    style = base;
    aero::plain = theme == GDOX_THEME_GDOX;
    if (aero::plain) {
        gdox(style);
    } else {
        windows_7(style);
    }
    if (gaming_mode) {
        style.ScaleAllSizes(1.18F);
        style.FontScaleMain = 1.20F;
    }
}

}
