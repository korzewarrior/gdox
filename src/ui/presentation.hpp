#ifndef GDOX_UI_PRESENTATION_HPP
#define GDOX_UI_PRESENTATION_HPP

extern "C" {
#include "app/app.h"
}

namespace gdox::ui {

void initialize_presentation();
void shutdown_presentation();
bool draw_application(gdox_app &app, bool gaming_mode);

}

#endif
