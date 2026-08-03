#include "app/xemu_performance.h"
#include "test.h"

void gdox_test_xemu_performance(void)
{
    GDOX_TEST_CHECK(
        gdox_xemu_effective_resolution_scale(
            GDOX_HOST_PROFILE_DESKTOP, 1U
        ) == 1U
    );
    GDOX_TEST_CHECK(
        gdox_xemu_effective_resolution_scale(
            GDOX_HOST_PROFILE_DESKTOP, 4U
        ) == 4U
    );
    GDOX_TEST_CHECK(
        gdox_xemu_effective_resolution_scale(
            GDOX_HOST_PROFILE_HANDHELD, 1U
        ) == 1U
    );
    GDOX_TEST_CHECK(
        gdox_xemu_effective_resolution_scale(
            GDOX_HOST_PROFILE_HANDHELD, 4U
        ) == 1U
    );
}
