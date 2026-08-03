#ifndef GDOX_UI_PRESENTATION_INTERNAL_HPP
#define GDOX_UI_PRESENTATION_INTERNAL_HPP

#include "app/app.h"

#include "imgui.h"

namespace gdox::ui::detail {

inline constexpr ImVec4 muted = {0.48F, 0.52F, 0.55F, 1.0F};
inline constexpr ImVec4 accent = {0.36F, 0.76F, 0.12F, 1.0F};
inline constexpr ImVec4 ready = {0.31F, 0.70F, 0.20F, 1.0F};
inline constexpr ImVec4 warning = {0.91F, 0.56F, 0.42F, 1.0F};
inline constexpr float footer_height = 28.0F;

bool initialize_dialogs();
void shutdown_dialogs();
bool dialogs_ready();
const char *notice();
void set_notice(const char *message);
void set_dialog_error(const char *operation);

bool action_button(const char *label, bool enabled, const ImVec2 &size);
void centered_text(const char *text, const ImVec4 &color);
bool disc_is_present(const gdox_app_snapshot &snapshot);
void choose_preservation_folder(
    gdox_app &app,
    const gdox_app_snapshot &snapshot
);
void choose_disc_image(gdox_app &app);

void draw_play(gdox_app &app, const gdox_app_snapshot &snapshot);
void draw_preserve(gdox_app &app, const gdox_app_snapshot &snapshot);
void draw_details(const gdox_app_snapshot &snapshot);
void draw_settings(gdox_app &app, const gdox_app_snapshot &snapshot);
void draw_sources(gdox_app &app, const gdox_app_snapshot &snapshot);

}

#endif
