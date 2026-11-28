#ifndef GDOX_UI_PRESENTATION_HPP
#define GDOX_UI_PRESENTATION_HPP

extern "C" {
#include "app/app.h"
}

namespace gdox::ui {

/* What the user asked of the window this frame. */
enum class request { none, close_window, quit };

void initialize_presentation();
void shutdown_presentation();
void clear_notice();
request draw_application(gdox_app &app, bool gaming_mode);
/* Shown while a quit waits for the drive to be restored. */
void draw_closing(const gdox_app_snapshot &snapshot);

}

#endif
