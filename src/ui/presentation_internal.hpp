#ifndef GDOX_UI_PRESENTATION_INTERNAL_HPP
#define GDOX_UI_PRESENTATION_INTERNAL_HPP

#include "app/app.h"
#include "ui/aero.hpp"

#include "imgui.h"

namespace gdox::ui::detail {

/* Windows 7 grey text, heading blue, and status colours for a white page. */
/* Set by apply_theme(). */
inline ImVec4 muted = {0.427F, 0.427F, 0.427F, 1.0F};
inline ImVec4 accent = {0.0F, 0.2F, 0.6F, 1.0F};
inline ImVec4 ready = {0.0F, 0.45F, 0.0F, 1.0F};
inline ImVec4 warning = {0.75F, 0.29F, 0.0F, 1.0F};

bool initialize_dialogs();
void shutdown_dialogs();
bool dialogs_ready();
const char *notice();
void set_notice(const char *message);
void set_dialog_error(const char *operation);

/* Opens the white page under the tabs; closed with end_page(). */
void begin_page(const char *identifier, ImGuiWindowFlags flags = 0);
void end_page();
bool action_button(
    const char *label,
    bool enabled,
    const ImVec2 &size,
    bool is_default = false
);
void centered_text(const char *text, const ImVec4 &color);
bool disc_is_present(const gdox_app_snapshot &snapshot);
void choose_preservation_folder(
    gdox_app &app,
    const gdox_app_snapshot &snapshot
);
void choose_disc_image(gdox_app &app);
void draw_drive_selector(gdox_app &app, const gdox_app_snapshot &snapshot);

void draw_play(gdox_app &app, const gdox_app_snapshot &snapshot);
void draw_preserve(gdox_app &app, const gdox_app_snapshot &snapshot);
void draw_details(const gdox_app_snapshot &snapshot);
void draw_settings(gdox_app &app, const gdox_app_snapshot &snapshot);
void draw_sources(gdox_app &app, const gdox_app_snapshot &snapshot);

}

#endif
