#include "ui/gamepad_input_policy.h"

void gdox_gamepad_input_initialize(gdox_gamepad_input_policy *policy)
{
    policy->armed = true;
    policy->navigation_enabled = true;
}

void gdox_gamepad_input_update(
    gdox_gamepad_input_policy *policy,
    bool emulator_running,
    bool window_focused,
    bool all_buttons_released
)
{
    if (emulator_running || !window_focused) {
        policy->armed = false;
        policy->navigation_enabled = false;
        return;
    }
    if (!policy->armed && all_buttons_released) {
        policy->armed = true;
    }
    policy->navigation_enabled = policy->armed;
}
