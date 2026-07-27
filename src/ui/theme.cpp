#include "ui/theme.hpp"

#include "imgui.h"

namespace gdox::ui {

void apply_theme()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

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
}

}
