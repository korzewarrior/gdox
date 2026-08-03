#include "ui/presentation_internal.hpp"
#include "ui/playback_labels.h"

#include <algorithm>

namespace gdox::ui::detail {
namespace {

bool draw_disc(const gdox_app_snapshot &snapshot)
{
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float diameter = content_width >= 1000.0F ? 238.0F : 194.0F;
    const float radius = diameter * 0.5F - 10.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 button_origin(
        origin.x + std::max(0.0F, (content_width - diameter) * 0.5F),
        origin.y
    );
    const ImVec2 center(
        button_origin.x + diameter * 0.5F,
        button_origin.y + diameter * 0.5F
    );
    const bool present = disc_is_present(snapshot);

    ImGui::SetCursorScreenPos(button_origin);
    (void)ImGui::InvisibleButton("##disc", ImVec2(diameter, diameter));
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = snapshot.can_start && ImGui::IsItemClicked();
    ImDrawList *draw = ImGui::GetWindowDrawList();

    draw->AddCircleFilled(
        ImVec2(center.x, center.y + 5.0F),
        radius + 3.0F,
        ImGui::GetColorU32(ImVec4(0.0F, 0.0F, 0.0F, 0.31F)),
        96
    );
    if (present) {
        draw->AddCircleFilled(
            center,
            radius,
            ImGui::GetColorU32(ImVec4(0.28F, 0.31F, 0.30F, 1.0F)),
            96
        );
        draw->PathArcTo(center, radius - 8.0F, 3.55F, 5.85F, 48);
        draw->PathStroke(
            ImGui::GetColorU32(ImVec4(0.52F, 0.58F, 0.55F, 0.42F)),
            0,
            5.0F
        );
        draw->PathArcTo(center, radius - 9.0F, 0.42F, 2.42F, 42);
        draw->PathStroke(
            ImGui::GetColorU32(ImVec4(0.17F, 0.20F, 0.19F, 0.72F)),
            0,
            4.0F
        );
        draw->AddCircle(
            center,
            radius,
            ImGui::GetColorU32(
                hovered
                    ? ImVec4(0.37F, 0.77F, 0.14F, 1.0F)
                    : ImVec4(0.18F, 0.45F, 0.10F, 1.0F)
            ),
            96,
            hovered ? 2.5F : 1.8F
        );
    } else {
        draw->AddCircleFilled(
            center,
            radius,
            ImGui::GetColorU32(ImVec4(0.085F, 0.098F, 0.106F, 1.0F)),
            96
        );
        draw->AddCircle(
            center,
            radius,
            ImGui::GetColorU32(ImVec4(0.22F, 0.25F, 0.27F, 1.0F)),
            96,
            2.0F
        );
    }
    draw->AddCircleFilled(
        center,
        18.0F,
        ImGui::GetColorU32(ImVec4(0.055F, 0.064F, 0.071F, 1.0F)),
        48
    );
    draw->AddCircle(
        center,
        18.0F,
        ImGui::GetColorU32(
            present
                ? ImVec4(0.48F, 0.53F, 0.51F, 0.9F)
                : ImVec4(0.20F, 0.23F, 0.24F, 1.0F)
        ),
        48,
        1.5F
    );

    if (hovered && snapshot.can_start) {
        const gdox_playback_labels labels =
            gdox_playback_labels_for_backend(snapshot.media_backend);
        ImGui::SetTooltip("%s", labels.start);
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + diameter));
    return pressed;
}

bool centered_link(const char *label)
{
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX()
            + std::max(0.0F, (available - width) * 0.5F)
    );
    return ImGui::TextLink(label);
}

void centered_wrapped_text(const char *text, const ImVec4 &color)
{
    const float available = ImGui::GetContentRegionAvail().x;
    const float wrap_width = std::min(available, 620.0F);
    const ImVec2 text_size = ImGui::CalcTextSize(
        text,
        nullptr,
        false,
        wrap_width
    );
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX()
            + std::max(0.0F, (available - text_size.x) * 0.5F)
    );
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextColored(color, "%s", text);
    ImGui::PopTextWrapPos();
}

}

void draw_play(gdox_app &app, const gdox_app_snapshot &snapshot)
{
    const gdox_playback_labels labels =
        gdox_playback_labels_for_backend(snapshot.media_backend);
    const gdox_playback_setup_notice setup =
        gdox_playback_setup_for_media(
            snapshot.media_platform,
            snapshot.xemu_ready,
            snapshot.xemu_setup
        );
    const char *attention = gdox_playback_attention_notice(
        snapshot.phase == GDOX_APP_ATTENTION,
        snapshot.notice
    );

    ImGui::BeginChild(
        "play-content",
        ImVec2(0.0F, -footer_height),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened
    );
    const float top_space = std::clamp(
        (ImGui::GetContentRegionAvail().y - 410.0F) * 0.24F,
        4.0F,
        44.0F
    );
    ImGui::Dummy(ImVec2(0.0F, top_space));
    if (draw_disc(snapshot)) {
        gdox_app_command(&app, GDOX_SESSION_LAUNCH_REQUESTED);
    }
    ImGui::Dummy(ImVec2(0.0F, 4.0F));
    centered_text(snapshot.disc, ImVec4(0.91F, 0.93F, 0.94F, 1.0F));
    centered_text(snapshot.drive, muted);
    if (attention[0] != '\0') {
        ImGui::Dummy(ImVec2(0.0F, 3.0F));
        centered_wrapped_text(attention, warning);
    }
    if (setup.action == GDOX_PLAYBACK_SETUP_OPEN_SOURCES) {
        ImGui::Dummy(ImVec2(0.0F, 3.0F));
        centered_wrapped_text(setup.message, warning);
        if (centered_link("Open Sources to finish xemu setup")) {
            gdox_app_select_page(&app, GDOX_APP_PAGE_SOURCES);
        }
    }
    ImGui::Dummy(ImVec2(0.0F, 5.0F));
    ImGui::PushStyleColor(
        ImGuiCol_TextLink,
        ImVec4(0.53F, 0.66F, 0.49F, 1.0F)
    );
    if (centered_link(
            snapshot.media_source == GDOX_MEDIA_DISC_IMAGE
                ? "Choose another disc image"
                : "Open a preserved disc image"
        )) {
        choose_disc_image(app);
    }
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0F, 6.0F));

    constexpr float gap = 10.0F;
    const float width = std::max(
        100.0F,
        (ImGui::GetContentRegionAvail().x - gap * 2.0F) / 3.0F
    );
    if (action_button(
            labels.start,
            snapshot.can_start,
            ImVec2(width, 42.0F)
        )) {
        gdox_app_command(&app, GDOX_SESSION_LAUNCH_REQUESTED);
    }
    ImGui::SameLine(0.0F, gap);
    if (action_button(
            labels.restart,
            snapshot.can_restart,
            ImVec2(width, 42.0F)
        )) {
        gdox_app_command(&app, GDOX_SESSION_RESTART_REQUESTED);
    }
    ImGui::SameLine(0.0F, gap);
    if (action_button(
            labels.close,
            snapshot.can_close,
            ImVec2(width, 42.0F)
        )) {
        gdox_app_command(&app, GDOX_SESSION_CLOSE_REQUESTED);
    }

    ImGui::Dummy(ImVec2(0.0F, 4.0F));
    const float source_action_width =
        snapshot.media_source == GDOX_MEDIA_DISC_IMAGE ? 150.0F : 88.0F;
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX()
            + std::max(
                0.0F,
                (
                    ImGui::GetContentRegionAvail().x
                    - source_action_width
                ) * 0.5F
            )
    );
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        if (action_button(
                "Use physical disc",
                snapshot.phase != GDOX_APP_PRESERVING,
                ImVec2(150.0F, 32.0F)
            )) {
            gdox_app_use_physical_disc(&app);
        }
    } else if (action_button(
                   "Eject",
                   snapshot.can_eject,
                   ImVec2(88.0F, 32.0F)
               )) {
        gdox_app_command(&app, GDOX_SESSION_EJECT_REQUESTED);
    }
    ImGui::EndChild();
}

}
