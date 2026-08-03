#ifndef GDOX_UI_GAMEPAD_INPUT_POLICY_H
#define GDOX_UI_GAMEPAD_INPUT_POLICY_H

#include <stdbool.h>

typedef struct gdox_gamepad_input_policy {
    bool armed;
    bool navigation_enabled;
} gdox_gamepad_input_policy;

#if defined(__cplusplus)
extern "C" {
#endif

void gdox_gamepad_input_initialize(gdox_gamepad_input_policy *policy);

void gdox_gamepad_input_update(
    gdox_gamepad_input_policy *policy,
    bool playback_running,
    bool window_focused,
    bool all_buttons_released
);

#if defined(__cplusplus)
}
#endif

#endif
