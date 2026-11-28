#ifndef GDOX_UI_COVER_ART_HPP
#define GDOX_UI_COVER_ART_HPP

#include "gdox/media.h"

#include "raylib.h"

namespace gdox::ui::cover {

/* The front cover of the disc in the drive, or nullptr while it is being
 * fetched or when there is none. Each cover is downloaded once, into the
 * GDOX data directory, and read from there afterwards. */
const Texture2D *for_disc(gdox_media_platform platform, const char *media_id);
void shutdown();

}

#endif
