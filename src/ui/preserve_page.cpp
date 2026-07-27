#include "ui/presentation_internal.hpp"

#include "app/preservation_naming.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace gdox::ui::detail {
namespace {

const char *preservation_phase_label(gdox_preservation_phase phase)
{
    switch (phase) {
        case GDOX_PRESERVATION_PREPARING:
            return "Preparing";
        case GDOX_PRESERVATION_READING:
            return "Reading disc";
        case GDOX_PRESERVATION_VERIFYING:
            return "Verifying";
        case GDOX_PRESERVATION_FINALIZING:
            return "Finalizing";
    }
    return "Working";
}

bool valid_output_name(const char *name)
{
    return name != nullptr
        && name[0] != '\0'
        && std::strcmp(name, ".") != 0
        && std::strcmp(name, "..") != 0
        && std::strchr(name, '/') == nullptr
        && std::strchr(name, '\\') == nullptr;
}

bool build_output_path(
    const char *directory,
    const char *name,
    std::string &output
)
{
    if (directory == nullptr || directory[0] == '\0'
        || !valid_output_name(name)) {
        return false;
    }
    output = directory;
    const char last = output.back();
    if (last != '/' && last != '\\') {
#if defined(_WIN32)
        output.push_back('\\');
#else
        output.push_back('/');
#endif
    }
    output += name;
    return output.size() < GDOX_EMULATOR_PATH_CAPACITY;
}

void draw_preservation_progress(
    gdox_app &app,
    const gdox_app_snapshot &snapshot
)
{
    const float fraction = snapshot.preservation_total_bytes != 0U
        ? static_cast<float>(
            static_cast<double>(snapshot.preservation_completed_bytes)
                / static_cast<double>(snapshot.preservation_total_bytes)
        )
        : 0.0F;
    ImGui::SetWindowFontScale(1.25F);
    ImGui::TextUnformatted(
        preservation_phase_label(snapshot.preservation_phase)
    );
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(muted, "%s", snapshot.disc);
    ImGui::Dummy(ImVec2(0.0F, 18.0F));
    ImGui::ProgressBar(fraction, ImVec2(-1.0F, 24.0F));
    ImGui::Text(
        "%.2f / %.2f GiB",
        static_cast<double>(snapshot.preservation_completed_bytes)
            / (1024.0 * 1024.0 * 1024.0),
        static_cast<double>(snapshot.preservation_total_bytes)
            / (1024.0 * 1024.0 * 1024.0)
    );
    ImGui::SameLine();
    ImGui::TextColored(
        muted,
        "%.1f MiB/s",
        snapshot.preservation_bytes_per_second / (1024.0 * 1024.0)
    );
    if (snapshot.preservation_unreadable_sectors != 0U) {
        ImGui::Text(
            "Unexpected unreadable sectors: %llu",
            static_cast<unsigned long long>(
                snapshot.preservation_unreadable_sectors
            )
        );
    }
    ImGui::TextWrapped("Output: %s", snapshot.preservation_output);
    ImGui::Dummy(ImVec2(0.0F, 18.0F));
    if (action_button(
            "Cancel",
            snapshot.can_cancel_preservation,
            ImVec2(-1.0F, 42.0F)
        )) {
        gdox_app_cancel_preservation(&app);
    }
}

}

void draw_preserve(gdox_app &app, const gdox_app_snapshot &snapshot)
{
    static int format = 0;
    static bool verify = true;
    static std::array<char, GDOX_PRESERVATION_FILENAME_CAPACITY> filename{};
    static std::array<char, GDOX_APP_TEXT_CAPACITY> suggested_title{};
    static int suggested_format = -1;
    static bool name_is_default = true;

    ImGui::BeginChild(
        "preserve-content",
        ImVec2(0.0F, -footer_height),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened
    );
    const bool compact_layout = ImGui::GetWindowHeight() < 420.0F;
    if (snapshot.phase == GDOX_APP_PRESERVING) {
        draw_preservation_progress(app, snapshot);
        ImGui::EndChild();
        return;
    }

    if (snapshot.preservation_complete) {
        ImGui::SetWindowFontScale(1.20F);
        ImGui::TextUnformatted("Preservation complete");
        ImGui::SetWindowFontScale(1.0F);
        ImGui::TextWrapped("%s", snapshot.notice);
        ImGui::TextWrapped("Output: %s", snapshot.preservation_output);
        ImGui::Separator();
    }

    ImGui::TextUnformatted("Format");
    (void)ImGui::RadioButton("Playable XISO", &format, 0);
    ImGui::SameLine();
    (void)ImGui::RadioButton("Full-disc preservation", &format, 1);
    const char *disc_title =
        snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC
            && disc_is_present(snapshot)
        ? snapshot.disc
        : "";
    if (name_is_default
        && (suggested_format != format
            || std::strcmp(suggested_title.data(), disc_title) != 0)) {
        (void)gdox_preservation_suggest_filename(
            disc_title,
            format == 0
                ? GDOX_PRESERVATION_XISO_COMPACT
                : GDOX_PRESERVATION_REDUMP,
            filename.data(),
            filename.size()
        );
        (void)std::snprintf(
            suggested_title.data(),
            suggested_title.size(),
            "%s",
            disc_title
        );
        suggested_format = format;
    }
    ImGui::TextColored(
        muted,
        "%s",
        format == 0
            ? "Smaller file size, ready to play in xemu."
            : "Bigger file size, more complete for archival purposes."
    );
    ImGui::Dummy(ImVec2(0.0F, compact_layout ? 2.0F : 12.0F));

    ImGui::TextUnformatted("Save folder");
    ImGui::TextColored(
        muted,
        "%s",
        snapshot.preservation_directory[0] != '\0'
            ? snapshot.preservation_directory
            : "Choose a folder before preserving"
    );
    if (ImGui::Button("Choose folder...", ImVec2(148.0F, 34.0F))) {
        choose_preservation_folder(app, snapshot);
    }
    if (!compact_layout) {
        ImGui::Dummy(ImVec2(0.0F, 8.0F));
    }

    ImGui::TextUnformatted("File name");
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText(
            "##preservation-name",
            filename.data(),
            filename.size()
        )) {
        name_is_default = false;
    }
    if (!valid_output_name(filename.data())) {
        ImGui::TextColored(warning, "Enter a file name, without folders.");
    }
    (void)ImGui::Checkbox("Verify the finished image", &verify);
    if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
        ImGui::TextColored(
            muted,
            "Preservation reads from a physical disc. Switch sources on Play."
        );
    }

    const float remaining = ImGui::GetContentRegionAvail().y;
    if (remaining > 42.0F) {
        ImGui::Dummy(ImVec2(0.0F, remaining - 42.0F));
    }
    std::string output_path;
    const bool output_ready = build_output_path(
        snapshot.preservation_directory,
        filename.data(),
        output_path
    );
    const bool can_begin = snapshot.can_preserve && output_ready;
    if (action_button(
            "Begin preservation",
            can_begin,
            ImVec2(-1.0F, 42.0F)
        )) {
        if (!gdox_app_begin_preservation(
                &app,
                format == 0
                    ? GDOX_PRESERVATION_XISO_COMPACT
                    : GDOX_PRESERVATION_REDUMP,
                output_path.c_str(),
                verify
            )) {
            set_notice("Could not start preservation");
        } else {
            set_notice("");
        }
    }
    ImGui::EndChild();
}

}
