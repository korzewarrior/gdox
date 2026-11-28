#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/presentation.hpp"
#include "ui/presentation_internal.hpp"
#include "ui/cover_art.hpp"
#include "ui/theme.hpp"
#include "ui/window_frame.hpp"

#include "nfd.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace gdox::ui::detail {
namespace {

constexpr ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoDecoration
    | ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoBringToFrontOnFocus;

bool file_dialogs_ready = false;
std::array<char, 192> ui_notice{};
/* The white page under the tabs, set by draw_root() for begin_page(). */
ImVec2 page_min;
ImVec2 page_max;

bool draw_header()
{
    const char *site_label = "gdox.korze.org";
    const char *discord_label = "Discord";
    constexpr float link_gap = 7.0F;
    constexpr float quit_width = 75.0F;
    const ImVec2 origin = ImGui::GetCursorPos();
    const float right = origin.x + ImGui::GetContentRegionAvail().x;
    const float line = ImGui::GetTextLineHeight();
    const float button_height = ImGui::GetFrameHeight();

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6F);
    const ImVec2 brand = ImGui::CalcTextSize("GDOX");
    ImGui::TextColored(accent, "GDOX");
    ImGui::PopFont();
    const float row = std::max(brand.y, button_height);
    const float links_width = ImGui::CalcTextSize(site_label).x
        + ImGui::CalcTextSize("|").x
        + ImGui::CalcTextSize(discord_label).x
        + link_gap * 2.0F;
    const float links_x = right - quit_width - 14.0F - links_width;

    ImGui::SetCursorPos(ImVec2(
        links_x, origin.y + std::floor((row - line) * 0.5F)
    ));
    if (aero::link(site_label)) {
        OpenURL("https://gdox.korze.org");
    }
    ImGui::SameLine(0.0F, link_gap);
    ImGui::TextColored(
        ImGui::ColorConvertU32ToFloat4(aero::rgb(0xA0A0A0)), "|"
    );
    ImGui::SameLine(0.0F, link_gap);
    if (aero::link(discord_label)) {
        OpenURL("https://discord.gg/TEzuUEJk4B");
    }
    ImGui::SetCursorPos(ImVec2(
        right - quit_width,
        origin.y + std::floor((row - button_height) * 0.5F)
    ));
    const bool quit_requested = aero::button(
        "Quit", ImVec2(quit_width, 0.0F)
    );
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + row + 9.0F));
    return quit_requested;
}

void draw_tabs(gdox_app &app, const gdox_app_snapshot &snapshot)
{
    static constexpr std::array<const char *, 5> labels = {
        "Play",
        "Preserve",
        "Details",
        "Settings",
        "Sources",
    };
    static constexpr std::array<gdox_app_page, 5> pages = {
        GDOX_APP_PAGE_PLAY,
        GDOX_APP_PAGE_PRESERVE,
        GDOX_APP_PAGE_DETAILS,
        GDOX_APP_PAGE_SETTINGS,
        GDOX_APP_PAGE_SOURCES,
    };
    ImDrawList *draw = ImGui::GetWindowDrawList();
    int selected = 0;

    for (std::size_t index = 0U; index < pages.size(); ++index) {
        if (snapshot.page == pages[index]) {
            selected = static_cast<int>(index);
        }
    }
    draw->AddRectFilled(page_min, page_max, aero::color.window);
    draw->AddRect(page_min, page_max, aero::color.control_border);
    const int clicked = aero::tabs(
        labels.data(), static_cast<int>(labels.size()), selected
    );
    if (clicked >= 0) {
        gdox_app_select_page(&app, pages[static_cast<std::size_t>(clicked)]);
    }
}

void draw_status_bar(
    const gdox_app_snapshot &snapshot,
    bool gaming_mode,
    const ImVec2 &min,
    const ImVec2 &max
)
{
    const char *notice = ui_notice[0] != '\0'
        ? ui_notice.data() : snapshot.notice;
    const bool has_notice = notice[0] != '\0';
    const char *aside = has_notice ? notice
        : gaming_mode
            ? "D-pad navigate  |  A select  |  LB/RB pages"
            : "F11 playback fullscreen  |  Ctrl+P pause";
    const float inset = ImGui::GetStyle().WindowPadding.x;
    const float y = min.y + 2.0F + std::floor(
        (max.y - min.y - 2.0F - ImGui::GetTextLineHeight()) * 0.5F
    );
    const float aside_x = std::max(
        min.x + inset, max.x - inset - ImGui::CalcTextSize(aside).x
    );
    ImDrawList *draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        min,
        ImVec2(max.x, min.y + 1.0F),
        ImGui::GetColorU32(ImGuiCol_Separator)
    );
    draw->AddRectFilled(
        ImVec2(min.x, min.y + 1.0F),
        ImVec2(max.x, min.y + 2.0F),
        aero::color.window
    );
    draw->PushClipRect(min, ImVec2(aside_x - 12.0F, max.y), true);
    draw->AddText(
        ImVec2(min.x + inset, y), aero::color.text, snapshot.status
    );
    draw->PopClipRect();
    draw->AddText(
        ImVec2(aside_x, y),
        has_notice
            ? ImGui::ColorConvertFloat4ToU32(warning)
            : aero::color.gray_text,
        aside
    );
}

request draw_root(gdox_app &app, bool gaming_mode)
{
    const gdox_app_snapshot *snapshot = gdox_app_snapshot_get(&app);
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (snapshot == nullptr) {
        return request::none;
    }
    static int applied_theme = -1;
    if (snapshot->settings.theme != applied_theme) {
        applied_theme = snapshot->settings.theme;
        apply_theme(snapshot->settings.theme, gaming_mode);
        frame::show(snapshot->settings.theme == GDOX_THEME_WINDOWS_7);
    }
    ImVec2 position = viewport->WorkPos;
    ImVec2 size = viewport->WorkSize;
    request result = frame::draw("GDOX", position, size)
        ? request::close_window : request::none;
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const float status_height = ImGui::GetFontSize() + 8.0F;

    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    /* The root window is the whole client area: no rounding or border. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    const bool root_open = ImGui::Begin("GDOX", nullptr, root_flags);
    ImGui::PopStyleVar(2);
    if (root_open) {
        if (draw_header()) {
            result = request::quit;
        }
        page_min = ImGui::GetCursorScreenPos() + ImVec2(
            0.0F,
            aero::plain ? ImGui::GetFrameHeight() + 6.0F
                        : ImGui::GetFontSize() + 8.0F
        );
        page_max = position + size
            - ImVec2(padding.x, status_height + padding.y - 2.0F);
        draw_tabs(app, *snapshot);
        switch (snapshot->page) {
            case GDOX_APP_PAGE_PLAY:
                draw_play(app, *snapshot);
                break;
            case GDOX_APP_PAGE_PRESERVE:
                draw_preserve(app, *snapshot);
                break;
            case GDOX_APP_PAGE_DETAILS:
                draw_details(*snapshot);
                break;
            case GDOX_APP_PAGE_SETTINGS:
                draw_settings(app, *snapshot);
                break;
            case GDOX_APP_PAGE_SOURCES:
                draw_sources(app, *snapshot);
                break;
        }
        draw_status_bar(
            *snapshot,
            gaming_mode,
            ImVec2(position.x, position.y + size.y - status_height),
            position + size
        );
    }
    ImGui::End();
    return result;
}

}
void begin_page(const char *identifier, ImGuiWindowFlags flags)
{
    ImGui::SetCursorScreenPos(page_min + ImVec2(1.0F, 1.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 12.0F));
    ImGui::BeginChild(
        identifier,
        page_max - page_min - ImVec2(2.0F, 2.0F),
        ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
        flags
    );
    ImGui::PopStyleVar();
    aero::skin_scrollbar(
        (flags & ImGuiWindowFlags_AlwaysVerticalScrollbar) != 0
    );
}

void end_page()
{
    ImGui::EndChild();
}

bool initialize_dialogs()
{
    file_dialogs_ready = NFD_Init() == NFD_OKAY;
    if (!file_dialogs_ready) {
        set_dialog_error("Native file dialogs");
    }
    return file_dialogs_ready;
}

void shutdown_dialogs()
{
    if (file_dialogs_ready) {
        NFD_Quit();
        file_dialogs_ready = false;
    }
}

bool dialogs_ready()
{
    return file_dialogs_ready;
}

const char *notice()
{
    return ui_notice.data();
}

void set_notice(const char *message)
{
    (void)std::snprintf(
        ui_notice.data(),
        ui_notice.size(),
        "%s",
        message != nullptr ? message : ""
    );
}

void set_dialog_error(const char *operation)
{
    const char *detail = NFD_GetError();
    (void)std::snprintf(
        ui_notice.data(),
        ui_notice.size(),
        "%s: %s",
        operation,
        detail != nullptr ? detail : "file dialog failed"
    );
}

bool action_button(
    const char *label,
    bool enabled,
    const ImVec2 &size,
    bool is_default
)
{
    ImGui::BeginDisabled(!enabled);
    const bool pressed = aero::button(label, size, is_default && enabled);
    ImGui::EndDisabled();
    return enabled && pressed;
}

void centered_text(const char *text, const ImVec4 &color)
{
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX()
            + std::max(0.0F, (available - width) * 0.5F)
    );
    ImGui::TextColored(color, "%s", text);
}

bool disc_is_present(const gdox_app_snapshot &snapshot)
{
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        return snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360
            ? snapshot.x360_image_layout != GDOX_X360_IMAGE_LAYOUT_NONE
            : snapshot.image_layout != GDOX_MEDIA_IMAGE_NONE;
    }
    return snapshot.phase == GDOX_APP_DISC_READY
        || snapshot.phase == GDOX_APP_PLAYING
        || snapshot.phase == GDOX_APP_PRESERVING
        || snapshot.phase == GDOX_APP_PRESERVED
        || snapshot.can_start
        || snapshot.can_restart
        || snapshot.can_preserve;
}

void choose_disc_image(gdox_app &app)
{
    if (!dialogs_ready()) {
        set_notice("Disc image picker is unavailable");
        return;
    }
    static const nfdu8filteritem_t filters[] = {
        {"Xbox disc images", "iso,xiso"},
    };
    nfdu8char_t *path = nullptr;
    nfdopendialogu8args_t arguments{};
    arguments.filterList = filters;
    arguments.filterCount = 1U;
    const nfdresult_t result = NFD_OpenDialogU8_With(&path, &arguments);
    if (result == NFD_OKAY) {
        if (!gdox_app_open_disc_image(&app, path)) {
            set_notice("That disc image could not be queued");
        } else {
            set_notice("");
        }
        NFD_FreePathU8(path);
    } else if (result == NFD_ERROR) {
        set_dialog_error("Disc image picker");
    }
}

void choose_preservation_folder(
    gdox_app &app,
    const gdox_app_snapshot &snapshot
)
{
    if (!dialogs_ready()) {
        set_notice("Folder picker is unavailable");
        return;
    }
    nfdu8char_t *path = nullptr;
    nfdpickfolderu8args_t arguments{};
    arguments.defaultPath = snapshot.settings.preservation_directory[0] != '\0'
        ? snapshot.settings.preservation_directory
        : nullptr;
    const nfdresult_t result = NFD_PickFolderU8_With(&path, &arguments);
    if (result == NFD_OKAY) {
        if (!gdox_app_set_preservation_directory(&app, path)) {
            set_notice("Could not save the preservation folder");
        } else {
            set_notice("");
        }
        NFD_FreePathU8(path);
    } else if (result == NFD_ERROR) {
        set_dialog_error("Folder picker");
    }
}

}

namespace gdox::ui {

void initialize_presentation()
{
    load_fonts();
    (void)detail::initialize_dialogs();
}

void shutdown_presentation()
{
    cover::shutdown();
    detail::shutdown_dialogs();
}

void clear_notice()
{
    detail::set_notice("");
}

request draw_application(gdox_app &app, bool gaming_mode)
{
    return detail::draw_root(app, gaming_mode);
}

void draw_closing(const gdox_app_snapshot &snapshot)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 position = viewport->WorkPos;
    ImVec2 size = viewport->WorkSize;

    (void)frame::draw("GDOX", position, size);
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::Begin("##closing", nullptr, detail::root_flags);
    ImGui::PopStyleVar(2);
    aero::heading("Closing GDOX");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", snapshot.drive);
    ImGui::TextWrapped("%s", snapshot.notice);
    if (snapshot.pending_cleanup_count != 0U) {
        ImGui::TextWrapped("%s", snapshot.pending_cleanup_notice);
    }
    ImGui::End();
}

}
