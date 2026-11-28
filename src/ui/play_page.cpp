#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/presentation_internal.hpp"
#include "ui/cover_art.hpp"
#include "ui/playback_labels.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gdox::ui::detail {
namespace {

using aero::rgb;

/* The reflection on a disc: one turn around it passes two highlights with a
 * trace of the rainbow beside them. */
ImU32 disc_sheen(float turn)
{
    struct stop {
        float at;
        ImU32 color;
    };
    static constexpr stop stops[] = {
        {0.00F, rgb(0xC6CCD4)}, {0.16F, rgb(0xE6D6E8)},
        {0.26F, rgb(0xFFFFFF)}, {0.36F, rgb(0xD0EAF4)},
        {0.54F, rgb(0xBEC5CE)}, {0.78F, rgb(0xAEB6C1)},
        {1.00F, rgb(0xC6CCD4)},
    };
    const float position = turn * 2.0F - std::floor(turn * 2.0F);

    for (std::size_t index = 1U; index < std::size(stops); ++index) {
        if (position <= stops[index].at) {
            return aero::mix(
                stops[index - 1U].color,
                stops[index].color,
                (position - stops[index - 1U].at)
                    / (stops[index].at - stops[index - 1U].at)
            );
        }
    }
    return stops[0].color;
}

void paint_disc(
    ImDrawList *draw,
    const ImVec2 &center,
    float radius,
    bool present,
    bool lit
)
{
    constexpr float two_pi = 6.2831853F;
    constexpr int steps = 144;
    const float inner = radius * 0.36F;
    const float hub = radius * 0.15F;

    for (int layer = 0; layer < 6; ++layer) {
        draw->AddCircleFilled(
            center + ImVec2(0.0F, 4.0F),
            radius + 5.0F - static_cast<float>(layer),
            rgb(0x000000, present ? 9U : 5U),
            96
        );
    }
    if (!present) {
        /* An empty tray: only the outline of where a disc goes. */
        draw->AddCircleFilled(center, radius, rgb(0xF5F6F8), 96);
        draw->AddCircle(center, radius, rgb(0xC9CDD3), 96, 1.0F);
        draw->AddCircle(center, inner, rgb(0xDCDFE4), 64, 1.0F);
    } else {
        /* Wedges that share their edges exactly; ImGui's own anti-aliasing
         * would leave a seam between them. */
        const ImDrawListFlags flags = draw->Flags;
        draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (int step = 0; step < steps; ++step) {
            const float from = two_pi * static_cast<float>(step)
                / static_cast<float>(steps);
            const float to = two_pi * static_cast<float>(step + 1)
                / static_cast<float>(steps);
            const ImVec2 a(std::cos(from), std::sin(from));
            const ImVec2 b(std::cos(to), std::sin(to));
            draw->AddQuadFilled(
                center + a * inner,
                center + a * radius,
                center + b * radius,
                center + b * inner,
                disc_sheen(
                    (static_cast<float>(step) + 0.5F)
                        / static_cast<float>(steps)
                )
            );
        }
        draw->Flags = flags;
        draw->AddCircle(center, radius - 2.5F, rgb(0xFFFFFF, 120U), 96, 1.0F);
        draw->AddCircle(center, radius, rgb(0x7F8894), 96, 1.3F);
        draw->AddCircleFilled(center, inner, rgb(0xE9EDF2), 64);
        draw->AddCircle(center, inner, rgb(0x9BA4B0), 64, 1.0F);
        draw->AddCircle(center, inner * 0.66F, rgb(0xC3CAD4), 48, 2.0F);
    }
    draw->AddCircleFilled(center, hub, aero::color.window, 48);
    draw->AddCircle(
        center, hub, present ? rgb(0x8A93A0) : rgb(0xC9CDD3), 48, 1.0F
    );
    if (lit) {
        for (int layer = 0; layer < 5; ++layer) {
            draw->AddCircle(
                center,
                radius + 1.0F + static_cast<float>(layer),
                rgb(0x56B9F5, static_cast<unsigned int>(110 - layer * 22)),
                96,
                1.5F
            );
        }
        draw->AddCircle(center, radius, aero::color.hot_border, 96, 1.5F);
    }
}

/* A keep case turned a little to the left, so its open edge shows on the
 * right where the disc comes out. `min`/`max` bound its front face as if it
 * faced the viewer. */
void paint_case(
    ImDrawList *draw,
    const ImVec2 &min,
    const ImVec2 &max,
    const Texture2D &cover,
    gdox_media_platform platform
)
{
    struct plastic {
        ImU32 front_top, front_bottom, side_front, side_back, ridge, edge,
            shine;
    };
    /* Black for Xbox and GameCube, white for Wii, glossy lime for 360. */
    static constexpr plastic black = {
        rgb(0x1C1D20), rgb(0x1C1D20), rgb(0x2A2C30), rgb(0x0E0F11),
        rgb(0xFFFFFF, 36U), rgb(0xFFFFFF, 60U), rgb(0xFFFFFF, 40U),
    };
    static constexpr plastic white = {
        rgb(0xF2F3F5), rgb(0xF2F3F5), rgb(0xE1E4E8), rgb(0xBCC1C8),
        rgb(0x000000, 30U), rgb(0xFFFFFF, 160U), rgb(0xFFFFFF, 0U),
    };
    static constexpr plastic lime = {
        rgb(0xA6E550), rgb(0x5BAF1A), rgb(0x9BE05A), rgb(0x3E8C0C),
        rgb(0xFFFFFF, 80U), rgb(0xFFFFFF, 190U), rgb(0xFFFFFF, 170U),
    };
    const plastic &skin = platform == GDOX_MEDIA_PLATFORM_WII ? white
        : platform == GDOX_MEDIA_PLATFORM_XBOX_360 ? lime : black;

    const float turn = 0.3F;
    const float sin_turn = std::sin(turn);
    const float cos_turn = std::cos(turn);
    /* The near edge grows in perspective; keep it inside the box. */
    const float height = (max.y - min.y) * 0.95F;
    const float half_width = (max.x - min.x) * 0.5F * 0.95F;
    const float half_height = height * 0.5F;
    const float depth = half_width * 0.2F;
    const float focal = height * 3.0F;
    const ImVec2 center((min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F);
    /* x across the front from its centre, y down from its middle, z back. */
    const auto at = [&](float x, float y, float z) {
        const float scale =
            focal / (focal - (x * sin_turn - z * cos_turn));
        return center
            + ImVec2((x * cos_turn + z * sin_turn) * scale, y * scale);
    };
    const ImVec2 front[4] = {
        at(-half_width, -half_height, 0.0F), at(half_width, -half_height, 0.0F),
        at(half_width, half_height, 0.0F), at(-half_width, half_height, 0.0F),
    };
    const ImVec2 side[4] = {
        front[1], at(half_width, -half_height, depth),
        at(half_width, half_height, depth), front[2],
    };
    const ImVec2 outline[6] = {
        front[0], front[1], side[1], side[2], front[2], front[3],
    };

    for (int layer = 0; layer < 6; ++layer) {
        const float grow = 6.0F - static_cast<float>(layer);
        ImVec2 shadow[6];
        for (int index = 0; index < 6; ++index) {
            const ImVec2 out = outline[index] - center;
            shadow[index] = outline[index] + ImVec2(2.0F, 5.0F)
                + out * (grow / std::sqrt(out.x * out.x + out.y * out.y));
        }
        draw->AddConvexPolyFilled(shadow, 6, rgb(0x000000, 10U));
    }

    int first = draw->VtxBuffer.Size;
    draw->AddConvexPolyFilled(side, 4, IM_COL32_WHITE);
    aero::shade(
        draw, first, side[0], side[1], skin.side_front, skin.side_back
    );
    first = draw->VtxBuffer.Size;
    draw->AddConvexPolyFilled(front, 4, IM_COL32_WHITE);
    aero::shade(
        draw, first, front[0], front[3], skin.front_top, skin.front_bottom
    );

    /* The art in narrow strips, so it follows the perspective instead of
     * bending along one diagonal. */
    const float hinge = half_width * 0.12F;
    const float art_left = -half_width + hinge;
    const float art_right = half_width - 3.0F;
    const float art_top = -half_height + 3.0F;
    const float art_bottom = half_height - 3.0F;
    constexpr int strips = 16;
    for (int strip = 0; strip < strips; ++strip) {
        const float u0 = static_cast<float>(strip) / strips;
        const float u1 = static_cast<float>(strip + 1) / strips;
        const float x0 = art_left + (art_right - art_left) * u0;
        const float x1 = art_left + (art_right - art_left) * u1;
        draw->AddImageQuad(
            static_cast<ImTextureID>(cover.id),
            at(x0, art_top, 0.0F), at(x1, art_top, 0.0F),
            at(x1, art_bottom, 0.0F), at(x0, art_bottom, 0.0F),
            ImVec2(u0, 0.0F), ImVec2(u1, 0.0F),
            ImVec2(u1, 1.0F), ImVec2(u0, 1.0F)
        );
    }
    for (int ridge = 0; ridge < 3; ++ridge) {
        const float x = -half_width
            + hinge * (0.3F + 0.2F * static_cast<float>(ridge));
        draw->AddLine(
            at(x, -half_height + 6.0F, 0.0F),
            at(x, half_height - 6.0F, 0.0F),
            skin.ridge
        );
    }

    /* Light on the clear sleeve, from the top left. */
    const ImVec2 sleeve[4] = {
        at(art_left, art_top, 0.0F), at(art_right, art_top, 0.0F),
        at(art_right, art_bottom, 0.0F), at(art_left, art_bottom, 0.0F),
    };
    first = draw->VtxBuffer.Size;
    draw->AddConvexPolyFilled(sleeve, 4, IM_COL32_WHITE);
    aero::shade(
        draw, first, sleeve[0], sleeve[0] + (sleeve[2] - sleeve[0]) * 0.6F,
        rgb(0xFFFFFF, 64U), rgb(0xFFFFFF, 0U)
    );

    /* Specular stripes down the open edge and the hinge. */
    draw->AddLine(
        at(half_width, -half_height + 2.0F, depth * 0.35F),
        at(half_width, half_height - 2.0F, depth * 0.35F),
        skin.shine,
        1.5F
    );
    draw->AddLine(
        at(-half_width + hinge * 0.82F, -half_height + 3.0F, 0.0F),
        at(-half_width + hinge * 0.82F, half_height - 3.0F, 0.0F),
        skin.shine,
        1.5F
    );
    draw->AddLine(front[1], front[2], skin.edge);
    draw->AddPolyline(
        outline, 6, rgb(0x000000, 110U), ImDrawFlags_Closed, 1.0F
    );
}

bool draw_disc(const gdox_app_snapshot &snapshot)
{
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float diameter = ImGui::GetContentRegionAvail().y < 450.0F
        ? 150.0F : content_width >= 1000.0F ? 238.0F : 194.0F;
    /* With a cover, the case stands in front and the disc shows beside it. */
    const Texture2D *cover =
        cover::for_disc(snapshot.media_platform, snapshot.media_id);
    const float case_width = cover == nullptr ? 0.0F : std::round(
        diameter * std::clamp(
            static_cast<float>(cover->width)
                / static_cast<float>(cover->height),
            0.55F,
            1.0F
        )
    );
    const float radius = cover == nullptr
        ? diameter * 0.5F - 10.0F : diameter * 0.34F;
    const float width = cover == nullptr
        ? diameter : case_width + radius * 1.6F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 button_origin(
        std::round(origin.x + std::max(0.0F, (content_width - width) * 0.5F)),
        origin.y
    );

    ImGui::SetCursorScreenPos(button_origin);
    (void)ImGui::InvisibleButton("##disc", ImVec2(width, diameter));
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = snapshot.can_start && ImGui::IsItemClicked();

    paint_disc(
        ImGui::GetWindowDrawList(),
        button_origin + ImVec2(
            cover == nullptr ? diameter * 0.5F : case_width + radius * 0.6F,
            diameter * 0.5F
        ),
        radius,
        disc_is_present(snapshot),
        hovered && snapshot.can_start
    );
    if (cover != nullptr) {
        paint_case(
            ImGui::GetWindowDrawList(),
            button_origin,
            button_origin + ImVec2(case_width, diameter),
            *cover,
            snapshot.media_platform
        );
    }
    if (hovered && snapshot.can_start) {
        aero::tooltip(
            gdox_playback_labels_for_backend(snapshot.media_backend).start
        );
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + diameter));
    return pressed;
}

void center_next(float width)
{
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX()
            + std::max(0.0F, (ImGui::GetContentRegionAvail().x - width) * 0.5F)
    );
}

bool centered_link(const char *label)
{
    center_next(ImGui::CalcTextSize(label).x);
    return aero::link(label);
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

    begin_page("play-content");
    draw_drive_selector(app, snapshot);
    /* Slightly above the middle of the room under the drive list. */
    const float top_space = std::clamp(
        (ImGui::GetContentRegionAvail().y - 372.0F) * 0.46F,
        4.0F,
        160.0F
    );
    ImGui::Dummy(ImVec2(0.0F, top_space));
    if (draw_disc(snapshot)) {
        gdox_app_command(&app, GDOX_SESSION_LAUNCH_REQUESTED);
    }
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 4.0F / 3.0F);
    centered_text(snapshot.disc, accent);
    ImGui::PopFont();
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
    ImGui::Dummy(ImVec2(0.0F, 3.0F));
    if (centered_link(
            snapshot.media_source == GDOX_MEDIA_DISC_IMAGE
                ? "Choose another disc image"
                : "Open a preserved disc image"
        )) {
        choose_disc_image(app);
    }
    ImGui::Dummy(ImVec2(0.0F, 8.0F));

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = std::clamp(
        (ImGui::GetContentRegionAvail().x - gap * 2.0F) / 3.0F,
        100.0F,
        150.0F
    );
    const ImVec2 size(width, ImGui::GetFrameHeight() + 4.0F);
    center_next(width * 3.0F + gap * 2.0F);
    if (action_button(labels.start, snapshot.can_start, size, true)) {
        gdox_app_command(&app, GDOX_SESSION_LAUNCH_REQUESTED);
    }
    ImGui::SameLine();
    if (action_button(labels.restart, snapshot.can_restart, size)) {
        gdox_app_command(&app, GDOX_SESSION_RESTART_REQUESTED);
    }
    ImGui::SameLine();
    if (action_button(labels.close, snapshot.can_close, size)) {
        gdox_app_command(&app, GDOX_SESSION_CLOSE_REQUESTED);
    }

    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        center_next(150.0F);
        if (action_button(
                "Use physical disc",
                snapshot.phase != GDOX_APP_PRESERVING,
                ImVec2(150.0F, 0.0F)
            )) {
            gdox_app_use_physical_disc(&app);
        }
    } else {
        center_next(88.0F);
        if (action_button(
                "Eject", snapshot.can_eject, ImVec2(88.0F, 0.0F)
            )) {
            gdox_app_command(&app, GDOX_SESSION_EJECT_REQUESTED);
        }
    }
    end_page();
}

}
