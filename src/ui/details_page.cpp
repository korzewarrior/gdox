#include "ui/presentation_internal.hpp"

#include <cstdint>

namespace gdox::ui::detail {
namespace {

void status_line(const char *label, bool value)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(190.0F);
    ImGui::TextColored(value ? ready : muted, "%s", value ? "Ready" : "Needed");
}

const char *image_layout_name(gdox_media_image_layout layout)
{
    switch (layout) {
        case GDOX_MEDIA_IMAGE_PLAYABLE_XISO:
            return "Playable XISO";
        case GDOX_MEDIA_IMAGE_WHOLE_DISC:
            return "Full-disc image";
        case GDOX_MEDIA_IMAGE_NONE:
            return "Not validated";
    }
    return "Not validated";
}

}

void draw_details(const gdox_app_snapshot &snapshot)
{
    ImGui::BeginChild(
        "details-content",
        ImVec2(0.0F, -footer_height),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened
    );
    ImGui::SetWindowFontScale(1.25F);
    ImGui::TextUnformatted("Technical details");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(muted, "GDOX %s", GDOX_VERSION);
    ImGui::Dummy(ImVec2(0.0F, 12.0F));

    ImGui::TextUnformatted("Game source");
    ImGui::Separator();
    ImGui::TextColored(
        snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC ? ready : muted,
        "%s",
        snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC
            ? "Physical disc"
            : "Disc image"
    );
    ImGui::TextWrapped("%s", snapshot.drive);
    ImGui::TextWrapped("%s", snapshot.disc);
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        ImGui::TextColored(
            muted,
            "%s",
            image_layout_name(snapshot.image_layout)
        );
        ImGui::TextWrapped("%s", snapshot.disc_image_path);
    }
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    ImGui::TextUnformatted(
        snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC
            ? "Physical disc reads"
            : "Disc image"
    );
    ImGui::Separator();
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        ImGui::TextColored(
            muted,
            "Read-only file source; no physical optical reads are claimed."
        );
        if (snapshot.image_source_sectors != 0U) {
            ImGui::Text(
                "Image size          %.2f GiB",
                static_cast<double>(snapshot.image_source_sectors)
                    * 2048.0
                    / (1024.0 * 1024.0 * 1024.0)
            );
            ImGui::Text(
                "Game partition LBA  %llu",
                static_cast<unsigned long long>(
                    snapshot.image_game_partition_lba
                )
            );
        }
    } else if (snapshot.physical_read_commands == 0U) {
        ImGui::TextColored(muted, "No successful optical data reads yet");
    } else {
        ImGui::TextColored(
            ready,
            "Confirmed directly from the optical transport"
        );
        ImGui::Text(
            "READ(12) commands   %llu",
            static_cast<unsigned long long>(snapshot.physical_read_commands)
        );
        ImGui::Text(
            "Physical sectors   %llu",
            static_cast<unsigned long long>(snapshot.physical_read_sectors)
        );
        ImGui::Text(
            "Data delivered     %.2f MiB",
            static_cast<double>(snapshot.physical_read_bytes)
                / (1024.0 * 1024.0)
        );
        ImGui::Text(
            "Last physical LBA  %llu",
            static_cast<unsigned long long>(snapshot.physical_last_lba)
        );
    }
    if (snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC) {
        ImGui::TextColored(
            muted,
            "Counts only successful hardware reads; virtual metadata is excluded."
        );
    }
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    ImGui::TextUnformatted("Runtime");
    ImGui::Separator();
    status_line("xemu", snapshot.xemu_ready);
    status_line("MCPX boot ROM", snapshot.mcpx_ready);
    status_line("Xbox BIOS", snapshot.flash_ready);
    status_line("Xbox hard disk", snapshot.hdd_ready);
    ImGui::TextUnformatted("Xbox X/Y/Z cache");
    ImGui::SameLine(190.0F);
    ImGui::TextColored(
        snapshot.hdd_cache_reset ? ready : muted,
        "%s",
        snapshot.hdd_cache_reset
            ? "Reset on launch"
            : "User-managed HDD"
    );
    ImGui::TextColored(muted, "%s", snapshot.xemu_setup);
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    ImGui::TextUnformatted("Current state");
    ImGui::Separator();
    ImGui::TextWrapped("%s", snapshot.status);
    ImGui::TextColored(muted, "%s", snapshot.notice);
    ImGui::EndChild();
}

}
