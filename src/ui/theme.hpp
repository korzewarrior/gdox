#ifndef GDOX_THEME_HPP
#define GDOX_THEME_HPP

#include <cstdint>

namespace gdox::ui {

/* Adds the interface font to the atlas; call once before the first frame. */
void load_fonts();
/* GDOX_THEME_WINDOWS_7 or GDOX_THEME_GDOX, over ImGui's own style. */
void apply_theme(std::uint8_t theme, bool gaming_mode);

}

#endif
