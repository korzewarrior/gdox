#include "ui/presentation_internal.hpp"

#include "nfd.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace gdox::ui::detail {
namespace {

void show_runtime_notice(gdox_app &app)
{
    gdox_app_tick(&app);
    set_notice(gdox_app_snapshot_get(&app)->notice);
}

std::string drive_label(const gdox_optical_device &device)
{
    const char *connection = device.connection == GDOX_OPTICAL_CONNECTION_USB
        ? "USB, " : device.connection == GDOX_OPTICAL_CONNECTION_SATA
            ? "SATA, " : "";
    return std::string(device.name) + " (" + connection + device.location + ")";
}

std::string normalize_xemu_selection(const char *path)
{
    std::string selected = path != nullptr ? path : "";
#if defined(__APPLE__)
    if (selected.size() >= 4U
        && selected.compare(selected.size() - 4U, 4U, ".app") == 0) {
        selected += "/Contents/MacOS/xemu";
    }
#endif
    return selected;
}

void choose_xemu(gdox_app &app)
{
    if (!dialogs_ready()) {
        set_notice("xemu picker is unavailable");
        return;
    }
    nfdu8char_t *path = nullptr;
    nfdopendialogu8args_t arguments{};
    const nfdresult_t result = NFD_OpenDialogU8_With(&path, &arguments);
    if (result == NFD_OKAY) {
        const std::string selected = normalize_xemu_selection(path);
        if (gdox_app_set_xemu_override(&app, selected.c_str())) {
            set_notice("");
        } else {
            show_runtime_notice(app);
        }
        NFD_FreePathU8(path);
    } else if (result == NFD_ERROR) {
        set_dialog_error("xemu picker");
    }
}

void path_row(
    const char *identifier,
    const char *label,
    const char *path,
    const char *empty_label
)
{
    ImGui::PushID(identifier);
    ImGui::TextUnformatted(label);
    const char *shown = path != nullptr && path[0] != '\0'
        ? path
        : empty_label;
    const bool has_path = path != nullptr && path[0] != '\0';
    if (has_path) {
        const float copy_width = ImGui::CalcTextSize("Copy path").x;
        ImGui::SameLine(
            std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - copy_width
            )
        );
        if (ImGui::TextLink("Copy path")) {
            ImGui::SetClipboardText(path);
            set_notice("Path copied");
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 5.0F));
    ImGui::BeginChild(
        "##path",
        ImVec2(0.0F, 43.0F),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysHorizontalScrollbar
    );
    ImGui::TextUnformatted(shown);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", shown);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void choose_firmware(gdox_app &app, bool mcpx)
{
    if (!dialogs_ready()) {
        set_notice("Firmware picker is unavailable");
        return;
    }
    nfdu8char_t *path = nullptr;
    nfdopendialogu8args_t arguments{};
    const nfdresult_t result = NFD_OpenDialogU8_With(&path, &arguments);
    if (result == NFD_OKAY) {
        const bool accepted = mcpx
            ? gdox_app_import_mcpx(&app, path)
            : gdox_app_import_bios(&app, path);
        if (accepted) {
            set_notice("");
        } else {
            show_runtime_notice(app);
        }
        NFD_FreePathU8(path);
    } else if (result == NFD_ERROR) {
        set_dialog_error("Firmware picker");
    }
}

void source_actions_spacing()
{
    ImGui::Dummy(ImVec2(0.0F, 8.0F));
}

}
void draw_drive_selector(gdox_app &app, const gdox_app_snapshot &snapshot)
{
    const char *selected = snapshot.settings.optical_device_id;
    std::string preview = selected[0] == '\0' ? "Automatic"
        : snapshot.settings.optical_device_label[0] != '\0'
            ? snapshot.settings.optical_device_label : "Selected drive";
    bool found = false;
    for (size_t index = 0U; index < snapshot.optical_device_count; ++index) {
        const gdox_app_optical_device &entry = snapshot.optical_devices[index];
        if (std::strcmp(entry.device.id, selected) == 0) {
            found = entry.connected;
            preview = drive_label(entry.device);
        }
    }
    if (selected[0] != '\0' && !found) {
        preview += snapshot.optical_inventory_notice[0] != '\0'
            ? " - status unavailable" : " - disconnected";
    }
    ImGui::TextUnformatted("Drive");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::BeginDisabled(!snapshot.can_select_drive);
    if (ImGui::BeginCombo("##drive-selection", preview.c_str())) {
        if (ImGui::Selectable("Automatic", selected[0] == '\0')) {
            if (gdox_app_select_drive(&app, "")) set_notice("");
            else show_runtime_notice(app);
        }
        if (selected[0] != '\0' && !found) {
            (void)ImGui::Selectable(preview.c_str(), true);
        }
        for (size_t index = 0U; index < snapshot.optical_device_count; ++index) {
            const gdox_app_optical_device &entry = snapshot.optical_devices[index];
            std::string label = drive_label(entry.device);
            if (!entry.connected) {
                label += snapshot.optical_inventory_notice[0] != '\0'
                    ? " - status unavailable" : " - disconnected";
            } else if (entry.cleanup_pending) {
                label += " - restoration pending";
            } else if (entry.device.drive == GDOX_OPTICAL_DRIVE_NONE) {
                label += " - unsupported firmware";
            } else if (!entry.device.accessible) {
                label += " - access unavailable";
            } else if (entry.device.media_status_known) {
                label += entry.device.media_present ? " - disc inserted" : " - empty";
            }
            ImGui::PushID(entry.device.id);
            if (ImGui::Selectable(label.c_str(), std::strcmp(entry.device.id, selected) == 0)) {
                if (gdox_app_select_drive(&app, entry.device.id)) set_notice("");
                else show_runtime_notice(app);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (snapshot.optical_inventory_notice[0] != '\0') {
        ImGui::TextWrapped("Drive list: %s", snapshot.optical_inventory_notice);
    }
    if (!snapshot.can_select_drive) {
        ImGui::TextColored(muted, "Drive selection is available after the current action finishes.");
    }
    if (snapshot.pending_cleanup_count != 0U) {
        ImGui::PushStyleColor(ImGuiCol_Text, warning);
        ImGui::TextWrapped("%s", snapshot.pending_cleanup_notice);
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0F, 8.0F));
}

void draw_settings(gdox_app &app, const gdox_app_snapshot &snapshot)
{
    static constexpr std::array<const char *, 4> aspect_labels = {
        "Automatic (game signal)",
        "16:9 widescreen",
        "4:3 original",
        "Native framebuffer",
    };
    static constexpr std::array<const char *, 3> fit_labels = {
        "Center at native size",
        "Scale and preserve aspect",
        "Stretch to fill window",
    };
    static constexpr std::array<const char *, 4> resolution_labels = {
        "1280 x 720",
        "1600 x 900",
        "1920 x 1080",
        "2560 x 1440",
    };
    static constexpr std::array<uint16_t, 4> widths = {
        1280U,
        1600U,
        1920U,
        2560U,
    };
    static constexpr std::array<uint16_t, 4> heights = {
        720U,
        900U,
        1080U,
        1440U,
    };
    int scale = snapshot.settings.internal_resolution_scale;
    int scale_index = scale - 1;
    int aspect = static_cast<int>(snapshot.settings.display_aspect);
    int fit = static_cast<int>(snapshot.settings.display_fit);
    int resolution = 0;
    bool fullscreen = snapshot.settings.fullscreen;
    bool display_changed = false;
    const bool handheld =
        app.host_profile == GDOX_HOST_PROFILE_HANDHELD;

    for (std::size_t index = 0U; index < widths.size(); ++index) {
        if (snapshot.settings.window_width == widths[index]
            && snapshot.settings.window_height == heights[index]) {
            resolution = static_cast<int>(index);
            break;
        }
    }

    ImGui::BeginChild(
        "settings-content",
        ImVec2(0.0F, -footer_height),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened
    );
    ImGui::SetWindowFontScale(1.25F);
    ImGui::TextUnformatted("Settings");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::Dummy(ImVec2(0.0F, 8.0F));

    ImGui::TextUnformatted("Playback display");
    ImGui::Separator();
    ImGui::SetNextItemWidth(250.0F);
    ImGui::BeginDisabled(handheld);
    if (ImGui::Combo(
            "Rendering scale",
            &scale_index,
            "1x\0"
            "2x\0"
            "3x\0"
            "4x\0"
            "5x\0"
            "6x\0"
            "7x\0"
            "8x\0"
            "9x\0"
            "10x\0"
        )) {
        scale = scale_index + 1;
        display_changed = true;
    }
    ImGui::EndDisabled();
    if (handheld) {
        ImGui::TextColored(muted, "Steam Deck playback uses fixed 1x rendering.");
    }
    ImGui::SetNextItemWidth(250.0F);
    if (ImGui::Combo(
            "Picture shape",
            &aspect,
            aspect_labels.data(),
            static_cast<int>(aspect_labels.size())
        )) {
        display_changed = true;
    }
    ImGui::SetNextItemWidth(250.0F);
    if (ImGui::Combo(
            "Fit to window",
            &fit,
            fit_labels.data(),
            static_cast<int>(fit_labels.size())
        )) {
        display_changed = true;
    }
    ImGui::SetNextItemWidth(250.0F);
    if (ImGui::Combo(
            "Window size",
            &resolution,
            resolution_labels.data(),
            static_cast<int>(resolution_labels.size())
        )) {
        display_changed = true;
    }
    if (ImGui::Checkbox("Start playback fullscreen", &fullscreen)) {
        display_changed = true;
    }
    if (display_changed) {
        gdox_app_set_display(
            &app,
            static_cast<uint8_t>(scale),
            static_cast<gdox_emulator_aspect>(aspect),
            static_cast<gdox_emulator_fit>(fit),
            fullscreen,
            widths[static_cast<std::size_t>(resolution)],
            heights[static_cast<std::size_t>(resolution)]
        );
    }
    ImGui::TextColored(
        muted,
        "Changes are saved. Running playback restarts automatically."
    );
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    ImGui::TextUnformatted("Startup");
    ImGui::Separator();
    bool auto_start = snapshot.settings.auto_start;
    if (ImGui::Checkbox("Auto start on insert", &auto_start)) {
        gdox_app_set_auto_start(&app, auto_start);
    }
    ImGui::EndChild();
}

void draw_sources(gdox_app &app, const gdox_app_snapshot &snapshot)
{
    ImGui::BeginChild(
        "sources-content",
        ImVec2(0.0F, -footer_height),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
        ImGuiWindowFlags_AlwaysVerticalScrollbar
    );
    ImGui::SetWindowFontScale(1.25F);
    ImGui::TextUnformatted("Sources");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(muted, "Choose or inspect the files GDOX uses.");
    ImGui::Dummy(ImVec2(0.0F, 10.0F));

    path_row(
        "disc-image",
        "Optional disc image",
        snapshot.disc_image_path,
        "Physical disc selected"
    );
    if (ImGui::Button("Choose disc image...", ImVec2(174.0F, 34.0F))) {
        choose_disc_image(app);
    }
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        ImGui::SameLine();
        if (ImGui::Button("Use physical disc", ImVec2(150.0F, 34.0F))) {
            gdox_app_use_physical_disc(&app);
        }
    }
    ImGui::TextColored(
        muted,
        "Physical discs remain the default and are never replaced automatically."
    );
    source_actions_spacing();
    if (snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360) {
        path_row(
            "xenia",
            "Verified Xenia executable",
            snapshot.xenia_executable,
            "Not available"
        );
        ImGui::TextColored(muted, "%s", snapshot.xenia_setup);
        source_actions_spacing();
    }
    path_row("xemu", "xemu executable", snapshot.xemu_executable, "Not available");
    if (ImGui::Button("Choose xemu...", ImVec2(142.0F, 34.0F))) {
        choose_xemu(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Use included xemu", ImVec2(166.0F, 34.0F))) {
        if (gdox_app_use_bundled_xemu(&app)) {
            set_notice("");
        } else {
            show_runtime_notice(app);
        }
    }
    ImGui::TextWrapped("%s", snapshot.xemu_setup);
    source_actions_spacing();
    path_row(
        "config",
        "xemu configuration",
        snapshot.xemu_configuration,
        "Not prepared"
    );
    ImGui::TextColored(muted, "Managed by GDOX");
    source_actions_spacing();
    path_row(
        "mcpx", "MCPX boot ROM",
        snapshot.mcpx_ready ? snapshot.mcpx_path : "", "Not imported"
    );
    if (ImGui::Button("Choose MCPX...", ImVec2(142.0F, 34.0F))) {
        choose_firmware(app, true);
    }
    source_actions_spacing();
    path_row(
        "bios", "Xbox BIOS",
        snapshot.flash_ready ? snapshot.flash_path : "", "Not imported"
    );
    if (ImGui::Button("Choose BIOS...", ImVec2(142.0F, 34.0F))) {
        choose_firmware(app, false);
    }
    source_actions_spacing();
    path_row(
        "hdd", "Included Xbox hard disk", snapshot.hdd_path, "Not prepared"
    );
    ImGui::TextColored(
        muted,
        "Included clean image; saves persist separately and the image is not replaced"
    );
    source_actions_spacing();
    path_row(
        "preservation",
        "Preservation folder",
        snapshot.settings.preservation_directory,
        "Choose a folder before preserving"
    );
    if (ImGui::Button("Choose folder...", ImVec2(148.0F, 34.0F))) {
        choose_preservation_folder(app, snapshot);
    }
    if (snapshot.preservation_output[0] != '\0') {
        path_row(
            "last-output",
            "Latest preservation output",
            snapshot.preservation_output,
            "None"
        );
    }
    ImGui::EndChild();
}

}
