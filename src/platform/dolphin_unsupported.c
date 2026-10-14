#include "gdox/dolphin.h"
#include <string.h>

static bool unavailable(gdox_error *error)
{
    gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
        "Live GameCube or Wii playback currently requires Linux and a supported LG GP63EX70 RF02 drive");
    return false;
}
bool gdox_dolphin_resolve(gdox_dolphin_runtime *runtime, gdox_error *error)
{
    if (runtime != NULL) memset(runtime, 0, sizeof(*runtime));
    return unavailable(error);
}
bool gdox_dolphin_launch(const gdox_dolphin_runtime *runtime,
    const char *user_directory, const char *disc_uri, uint64_t length,
    bool fullscreen, gdox_dolphin_process **output, gdox_error *error)
{
    (void)runtime; (void)user_directory; (void)disc_uri; (void)length; (void)fullscreen;
    if (output != NULL) *output = NULL;
    return unavailable(error);
}
bool gdox_dolphin_poll(gdox_dolphin_process *process,
    bool *running, int *exit_code, gdox_error *error)
{
    (void)process;
    if (running != NULL) *running = false;
    if (exit_code != NULL) *exit_code = 1;
    return unavailable(error);
}
bool gdox_dolphin_stop(gdox_dolphin_process **process, gdox_error *error)
{
    if (process == NULL || *process == NULL) { gdox_error_clear(error); return true; }
    return unavailable(error);
}
