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
    GDOX_TEST_CHECK(!gdox_optical_monitor_has_pending_failure(&monitor));

    gdox_optical_monitor_session_ended(&monitor);
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    gdox_optical_monitor_observation_failed(&monitor);
    GDOX_TEST_CHECK(gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_has_pending_failure(&monitor));
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));

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

    gdox_optical_monitor_fail(
        &monitor, GDOX_OPTICAL_MONITOR_FAILURE_TERMINAL
    );
    GDOX_TEST_CHECK(gdox_optical_monitor_has_pending_failure(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));

    for (observation = 0U;
         observation < GDOX_MEDIA_REARM_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &absent));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_has_pending_failure(&monitor));
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));

    gdox_optical_monitor_session_ended(&monitor);
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));

    for (observation = 0U;
         observation < GDOX_MEDIA_TRANSIENT_RETRY_LIMIT;
         ++observation) {
        gdox_optical_monitor_fail(
            &monitor, GDOX_OPTICAL_MONITOR_FAILURE_TRANSIENT
        );
        GDOX_TEST_CHECK(gdox_optical_monitor_is_armed(&monitor));
        GDOX_TEST_CHECK(gdox_optical_monitor_has_pending_failure(&monitor));
        for (uint32_t stable = 1U;
             stable < GDOX_MEDIA_STABLE_OBSERVATIONS;
             ++stable) {
            GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
        }
        GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));
    }
    gdox_optical_monitor_fail(
        &monitor, GDOX_OPTICAL_MONITOR_FAILURE_TRANSIENT
    );
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));

    gdox_optical_monitor_block(&monitor);
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    gdox_optical_monitor_retry(&monitor);
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));

    gdox_optical_monitor_eject_completed(
        &monitor, GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED
    );
    GDOX_TEST_CHECK(gdox_optical_monitor_is_armed(&monitor));
    for (observation = 1U;
         observation < GDOX_MEDIA_STABLE_OBSERVATIONS;
         ++observation) {
        GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
    }
    GDOX_TEST_CHECK(gdox_optical_monitor_observe(&monitor, &ready));

    gdox_optical_monitor_eject_completed(
        &monitor,
        GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT
    );
    GDOX_TEST_CHECK(!gdox_optical_monitor_is_armed(&monitor));
    GDOX_TEST_CHECK(!gdox_optical_monitor_observe(&monitor, &ready));
}
