#include "test.h"

#include "ui/gamepad_input_policy.h"

static void test_emulator_owns_gamepad_input(void)
{
    gdox_gamepad_input_policy policy;

    gdox_gamepad_input_initialize(&policy);
    GDOX_TEST_CHECK(policy.armed);
    GDOX_TEST_CHECK(policy.navigation_enabled);

    gdox_gamepad_input_update(&policy, true, true, true);
    GDOX_TEST_CHECK(!policy.armed);
    GDOX_TEST_CHECK(!policy.navigation_enabled);

    gdox_gamepad_input_update(&policy, true, true, true);
    GDOX_TEST_CHECK(!policy.navigation_enabled);

    gdox_gamepad_input_update(&policy, false, true, false);
    GDOX_TEST_CHECK(!policy.armed);
    GDOX_TEST_CHECK(!policy.navigation_enabled);

    gdox_gamepad_input_update(&policy, false, true, true);
    GDOX_TEST_CHECK(policy.armed);
    GDOX_TEST_CHECK(policy.navigation_enabled);
}

static void test_focus_loss_requires_button_release(void)
{
    gdox_gamepad_input_policy policy;

    gdox_gamepad_input_initialize(&policy);
    gdox_gamepad_input_update(&policy, false, false, true);
    GDOX_TEST_CHECK(!policy.armed);
    GDOX_TEST_CHECK(!policy.navigation_enabled);

    gdox_gamepad_input_update(&policy, false, true, false);
    GDOX_TEST_CHECK(!policy.navigation_enabled);

    gdox_gamepad_input_update(&policy, false, true, true);
    GDOX_TEST_CHECK(policy.armed);
    GDOX_TEST_CHECK(policy.navigation_enabled);
}

void gdox_test_gamepad_input_policy(void)
{
    test_emulator_owns_gamepad_input();
    test_focus_loss_requires_button_release();
}
