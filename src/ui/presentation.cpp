#include "ui/presentation.hpp"
#include "ui/presentation_internal.hpp"
#include "ui/theme.hpp"

#include "nfd.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace gdox::ui::detail {
namespace {

bool file_dialogs_ready = false;
std::array<char, 192> ui_notice{};

bool page_button(const char *label, bool selected, float width)
{
    if (selected) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.063F, 0.486F, 0.063F, 1.0F)
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.086F, 0.60F, 0.086F, 1.0F)
        );
    } else {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.10F, 0.12F, 0.13F, 1.0F)
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.15F, 0.17F, 0.18F, 1.0F)
        );
    }
    const bool pressed = ImGui::Button(
        label,
        ImVec2(width, std::max(34.0F, ImGui::GetFrameHeight()))
    );
    ImGui::PopStyleColor(2);
    return pressed;
}

bool draw_header(gdox_app &app, const gdox_app_snapshot &snapshot)
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

    const char *tagline =
        "made by korze, with love, for gaming preservation everywhere";
    const float row_x = ImGui::GetCursorPosX();
    const float row_y = ImGui::GetCursorPosY();
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float content_right = ImGui::GetWindowContentRegionMax().x;
    const bool compact = content_width < 840.0F;
    const float title_scale = compact ? 1.85F : 2.10F;
    const float tagline_scale = compact ? 0.65F : 0.82F;
    const float button_width = compact ? 58.0F : 62.0F;
    const float button_height = ImGui::GetFrameHeight();
    const char *site_label = compact ? "Site" : "gdox.korze.org";
    const char *discord_label = "Discord";
    const float link_gap = 7.0F;
    const float button_gap = 12.0F;
    const float site_width = ImGui::CalcTextSize(site_label).x;
    const float separator_width = ImGui::CalcTextSize("·").x;
    const float discord_width = ImGui::CalcTextSize(discord_label).x;
    const float links_width = site_width + link_gap + separator_width
        + link_gap + discord_width;
    const float actions_width = links_width + button_gap + button_width;
    const float actions_x = content_right - actions_width;

    ImGui::SetWindowFontScale(title_scale);
    const ImVec2 title_size = ImGui::CalcTextSize("GDOX");
    ImGui::SetWindowFontScale(tagline_scale);
    const ImVec2 tagline_size = ImGui::CalcTextSize(tagline);
    ImGui::SetWindowFontScale(1.0F);
    const float brand_width = title_size.x + 14.0F + tagline_size.x;
    const bool single_row = brand_width + 18.0F + actions_width
        <= content_width;
    const float brand_height = std::max(title_size.y, tagline_size.y);
    const float actions_height = std::max(
        button_height,
        ImGui::GetTextLineHeight()
    );
    const float single_row_height = std::max(brand_height, actions_height);
    const float brand_y = single_row
        ? row_y + (single_row_height - brand_height) * 0.5F
        : row_y;
    const float actions_y = single_row
        ? row_y + (single_row_height - actions_height) * 0.5F
        : row_y + brand_height + 4.0F;
    const float text_y = actions_y
        + std::max(
            0.0F,
            (actions_height - ImGui::GetTextLineHeight()) * 0.5F
        );
    const float header_height = single_row
        ? single_row_height
        : brand_height + 4.0F + actions_height;

    ImGui::SetWindowFontScale(title_scale);
    ImGui::SetCursorPos(ImVec2(
        row_x,
        brand_y + std::max(0.0F, (brand_height - title_size.y) * 0.5F)
    ));
    ImGui::TextColored(accent, "GDOX");

    ImGui::SetWindowFontScale(tagline_scale);
    const float tagline_x = row_x + title_size.x + 14.0F;
    ImGui::SetCursorPos(ImVec2(
        tagline_x,
        brand_y + std::max(
            0.0F,
            (brand_height - tagline_size.y) * 0.5F
        )
    ));
    ImGui::TextColored(
        ImVec4(0.38F, 0.41F, 0.43F, 1.0F),
        "%s",
        tagline
    );

    ImGui::SetWindowFontScale(1.0F);
    ImGui::SetCursorPos(ImVec2(actions_x, text_y));
    ImGui::PushStyleColor(
        ImGuiCol_TextLink,
        ImVec4(0.62F, 0.66F, 0.68F, 1.0F)
    );
    if (ImGui::TextLink(site_label)) {
        OpenURL("https://gdox.korze.org");
    }
    ImGui::SameLine(0.0F, 7.0F);
    ImGui::TextColored(muted, "·");
    ImGui::SameLine(0.0F, 7.0F);
    if (ImGui::TextLink(discord_label)) {
        OpenURL("https://discord.gg/TEzuUEJk4B");
    }
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(
        content_right - button_width,
        actions_y + (actions_height - button_height) * 0.5F
    ));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ButtonTextAlign,
        ImVec2(0.5F, 0.5F)
    );
    const bool quit_requested = ImGui::Button(
        "Quit",
        ImVec2(button_width, button_height)
    );
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(row_x, row_y + header_height));
    ImGui::Dummy(ImVec2(0.0F, 4.0F));

    constexpr float gap = 8.0F;
    const float width = std::max(
        88.0F,
        (ImGui::GetContentRegionAvail().x
            - gap * static_cast<float>(labels.size() - 1U))
            / static_cast<float>(labels.size())
    );
    for (std::size_t index = 0U; index < labels.size(); ++index) {
        if (index != 0U) {
            ImGui::SameLine(0.0F, gap);
        }
        if (page_button(labels[index], snapshot.page == pages[index], width)) {
            gdox_app_select_page(&app, pages[index]);
        }
    }
    ImGui::Dummy(ImVec2(0.0F, 4.0F));
    return quit_requested;
}

void draw_footer(const gdox_app_snapshot &snapshot, bool gaming_mode)
{
    ImGui::TextColored(muted, "%s", snapshot.status);
    if (ui_notice[0] != '\0') {
        const float notice_width = ImGui::CalcTextSize(ui_notice.data()).x;
        ImGui::SameLine(
            std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - notice_width
            )
        );
        ImGui::TextColored(warning, "%s", ui_notice.data());
    } else {
        const char *controls = gaming_mode
            ? "D-pad navigate  |  A select  |  LB/RB pages"
            : "F11 playback fullscreen  |  Ctrl+P pause";
        const float controls_width = ImGui::CalcTextSize(controls).x;
        ImGui::SameLine(
            std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - controls_width
            )
        );
        ImGui::TextColored(muted, "%s", controls);
    }
}

bool draw_root(gdox_app &app, bool gaming_mode)
{
    const gdox_app_snapshot *snapshot = gdox_app_snapshot_get(&app);
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (snapshot == nullptr) {
        return false;
    }
    bool quit_requested = false;

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (ImGui::Begin("GDOX", nullptr, flags)) {
        quit_requested = draw_header(app, *snapshot);
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
        draw_footer(*snapshot, gaming_mode);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return quit_requested;
}

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

bool action_button(const char *label, bool enabled, const ImVec2 &size)
{
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const bool pressed = ImGui::Button(label, size);
    if (!enabled) {
        ImGui::EndDisabled();
    }
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
    apply_theme();
    (void)detail::initialize_dialogs();
}

void shutdown_presentation()
{
    detail::shutdown_dialogs();
}

bool draw_application(gdox_app &app, bool gaming_mode)
{
    return detail::draw_root(app, gaming_mode);
}

}
