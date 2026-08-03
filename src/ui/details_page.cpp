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

const char *image_layout_name(const gdox_app_snapshot &snapshot)
{
    if (snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360) {
        return gdox_x360_image_layout_name(snapshot.x360_image_layout);
    }
    switch (snapshot.image_layout) {
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
    if (snapshot.media_platform != GDOX_MEDIA_PLATFORM_NONE) {
        ImGui::TextColored(
            muted,
            "%s",
            snapshot.media_platform == GDOX_MEDIA_PLATFORM_XBOX_360
                ? "Xbox 360"
                : "Original Xbox"
        );
    }
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        ImGui::TextColored(
            muted,
            "%s",
            image_layout_name(snapshot)
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
        if (snapshot.nbd_read_stats.requests != 0U) {
            const gdox_nbd_read_stats &reads = snapshot.nbd_read_stats;
            const double no_drive_percent = reads.successful_requests != 0U
                ? static_cast<double>(
                    reads.served_without_drive_io_requests
                ) * 100.0
                    / static_cast<double>(reads.successful_requests)
                : 0.0;
            const double average_service_ms = reads.requests != 0U
                ? static_cast<double>(reads.service_milliseconds)
                    / static_cast<double>(reads.requests)
                : 0.0;

            ImGui::Dummy(ImVec2(0.0F, 8.0F));
            ImGui::TextUnformatted("Live playback reads");
            ImGui::Text(
                "Guest requests      %llu (%.2f MiB)",
                static_cast<unsigned long long>(reads.requests),
                static_cast<double>(reads.requested_bytes)
                    / (1024.0 * 1024.0)
            );
            ImGui::Text(
                "Served without drive I/O  %llu / %llu (%.1f%%)",
                static_cast<unsigned long long>(
                    reads.served_without_drive_io_requests
                ),
                static_cast<unsigned long long>(
                    reads.successful_requests
                ),
                no_drive_percent
            );
            ImGui::Text(
                "Drive-backed reads  %llu",
                static_cast<unsigned long long>(
                    reads.requests_with_drive_io
                )
            );
            ImGui::Text(
                "Guest optical I/O   %llu commands / %.2f MiB",
                static_cast<unsigned long long>(reads.physical_commands),
                static_cast<double>(reads.physical_bytes)
                    / (1024.0 * 1024.0)
            );
            ImGui::Text(
                "Sequential / discontinuous  %llu / %llu",
                static_cast<unsigned long long>(
                    reads.sequential_requests
                ),
                static_cast<unsigned long long>(
                    reads.discontinuous_requests
                )
            );
            ImGui::Text(
                "Read service avg / max  %.1f / %llu ms",
                average_service_ms,
                static_cast<unsigned long long>(
                    reads.maximum_service_milliseconds
                )
            );
            if (reads.failed_requests != 0U) {
                ImGui::TextColored(
                    warning,
                    "Failed guest reads  %llu",
                    static_cast<unsigned long long>(
                        reads.failed_requests
                    )
                );
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    ImGui::TextUnformatted("Runtime");
    ImGui::Separator();
    if (snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA) {
        status_line("Xenia", snapshot.xenia_ready);
        if (snapshot.xenia_policy != nullptr
            && snapshot.xenia_policy->runtime != nullptr) {
            ImGui::Text(
                "Revision            %s",
                snapshot.xenia_policy->runtime->revision
            );
            ImGui::Text(
                "Launch module       %s",
                snapshot.xenia_policy->launch_module
            );
        }
        ImGui::TextColored(muted, "%s", snapshot.xenia_setup);
    } else {
        status_line("xemu", snapshot.xemu_ready);
        status_line("MCPX boot ROM", snapshot.mcpx_ready);
        status_line("Xbox BIOS", snapshot.flash_ready);
        status_line("Xbox hard disk", snapshot.hdd_ready);
        ImGui::TextColored(muted, "%s", snapshot.xemu_setup);
    }
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    ImGui::TextUnformatted("Current state");
    ImGui::Separator();
    ImGui::TextWrapped("%s", snapshot.status);
    ImGui::TextColored(muted, "%s", snapshot.notice);
    ImGui::EndChild();
}

}
