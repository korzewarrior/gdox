#include "ui/presentation_internal.hpp"

#include <cstdarg>
#include <cstdint>

namespace gdox::ui::detail {
namespace {

constexpr float value_column = 200.0F;

void status_line(const char *label, bool value)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(value_column);
    ImGui::TextColored(value ? ready : muted, "%s", value ? "Ready" : "Needed");
}

/* A name and its value in two columns. */
void row(const char *label, const char *format, ...) IM_FMTARGS(2);
void row(const char *label, const char *format, ...)
{
    va_list arguments;

    ImGui::TextUnformatted(label);
    ImGui::SameLine(value_column);
    va_start(arguments, format);
    ImGui::TextV(format, arguments);
    va_end(arguments);
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
    begin_page("details-content");
    /* Rows of facts sit closer together than controls do. */
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, 4.0F)
    );
    aero::heading("Technical details");
    ImGui::TextColored(muted, "GDOX %s", GDOX_VERSION);
    ImGui::Dummy(ImVec2(0.0F, 12.0F));

    aero::group("Game source");
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
                : snapshot.media_platform == GDOX_MEDIA_PLATFORM_GAMECUBE
                    ? "Nintendo GameCube"
                    : snapshot.media_platform == GDOX_MEDIA_PLATFORM_WII
                        ? "Nintendo Wii" : "Original Xbox"
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

    aero::group(
        snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC
            ? "Physical disc reads"
            : "Disc image"
    );
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        ImGui::TextColored(
            muted,
            "Read-only file source; no physical optical reads are claimed."
        );
        if (snapshot.image_source_sectors != 0U) {
            row(
                "Image size",
                "%.2f GiB",
                static_cast<double>(snapshot.image_source_sectors)
                    * 2048.0
                    / (1024.0 * 1024.0 * 1024.0)
            );
            row(
                "Game partition LBA",
                "%llu",
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
        row(
            "READ(12) commands",
            "%llu",
            static_cast<unsigned long long>(snapshot.physical_read_commands)
        );
        row(
            "Physical sectors",
            "%llu",
            static_cast<unsigned long long>(snapshot.physical_read_sectors)
        );
        row(
            "Data delivered",
            "%.2f MiB",
            static_cast<double>(snapshot.physical_read_bytes)
                / (1024.0 * 1024.0)
        );
        row(
            "Last physical LBA",
            "%llu",
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
            aero::group("Live playback reads");
            row(
                "Guest requests",
                "%llu (%.2f MiB)",
                static_cast<unsigned long long>(reads.requests),
                static_cast<double>(reads.requested_bytes)
                    / (1024.0 * 1024.0)
            );
            row(
                "Served without drive I/O",
                "%llu / %llu (%.1f%%)",
                static_cast<unsigned long long>(
                    reads.served_without_drive_io_requests
                ),
                static_cast<unsigned long long>(
                    reads.successful_requests
                ),
                no_drive_percent
            );
            row(
                "Drive-backed reads",
                "%llu",
                static_cast<unsigned long long>(
                    reads.requests_with_drive_io
                )
            );
            row(
                "Guest optical I/O",
                "%llu commands / %.2f MiB",
                static_cast<unsigned long long>(reads.physical_commands),
                static_cast<double>(reads.physical_bytes)
                    / (1024.0 * 1024.0)
            );
            row(
                "Sequential / discontinuous",
                "%llu / %llu",
                static_cast<unsigned long long>(
                    reads.sequential_requests
                ),
                static_cast<unsigned long long>(
                    reads.discontinuous_requests
                )
            );
            row(
                "Read service avg / max",
                "%.1f / %llu ms",
                average_service_ms,
                static_cast<unsigned long long>(
                    reads.maximum_service_milliseconds
                )
            );
            if (reads.failed_requests != 0U) {
                ImGui::TextColored(
                    warning,
                    "Failed guest reads: %llu",
                    static_cast<unsigned long long>(
                        reads.failed_requests
                    )
                );
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    aero::group("Runtime");
    if (snapshot.media_backend == GDOX_MEDIA_BACKEND_XENIA) {
        status_line("Xenia", snapshot.xenia_ready);
        if (snapshot.xenia_policy != nullptr
            && snapshot.xenia_policy->runtime != nullptr) {
            row(
                "Revision",
                "%s",
                snapshot.xenia_policy->runtime->revision
            );
            row(
                "Launch module",
                "%s",
                snapshot.xenia_policy->launch_module
            );
        }
        ImGui::TextColored(muted, "%s", snapshot.xenia_setup);
    } else if (snapshot.media_backend == GDOX_MEDIA_BACKEND_DOLPHIN) {
        status_line("Dolphin 2609a", snapshot.dolphin_ready);
        ImGui::TextColored(muted, "%s", snapshot.dolphin_setup);
    } else {
        status_line("xemu", snapshot.xemu_ready);
        status_line("MCPX boot ROM", snapshot.mcpx_ready);
        status_line("Xbox BIOS", snapshot.flash_ready);
        status_line("Xbox hard disk", snapshot.hdd_ready);
        ImGui::TextColored(muted, "%s", snapshot.xemu_setup);
    }
    ImGui::Dummy(ImVec2(0.0F, 14.0F));

    aero::group("Current state");
    ImGui::TextWrapped("%s", snapshot.status);
    ImGui::TextColored(muted, "%s", snapshot.notice);
    ImGui::PopStyleVar();
    end_page();
}

}
