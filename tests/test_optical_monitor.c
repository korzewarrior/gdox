#include "test.h"

#include "app/optical_monitor.h"

void gdox_test_optical_monitor(void)
{
    gdox_optical_monitor monitor;
    gdox_optical_presence ready = {
        .drive_present = true,
        .media_status_known = true,
        .media_present = true,
    };
    gdox_optical_presence absent = {0};
    uint32_t observation;

    gdox_optical_monitor_initialize(&monitor);
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));

    for (observation = 1U;
         observation < GDOX_MEDIA_REARM_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &absent));
    }
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));

    for (observation = 0U;
         observation < GDOX_MEDIA_REARM_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &absent));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_is_armed(&monitor));
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));

    gdox_optical_monitor_fail(&monitor);
    for (observation = 0U;
         observation < GDOX_MEDIA_REARM_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &absent));
    }
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));

    gdox_optical_monitor_block(&monitor);
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    gdox_optical_monitor_retry(&monitor);
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));
}
