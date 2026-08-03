#include "app/physical_media_monitor.h"

#include <stdio.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            (void)fprintf(                                                     \
                stderr, "%s:%d: check failed: %s\n",                         \
                __FILE__, __LINE__, #expression                               \
            );                                                                 \
            return 1;                                                          \
        }                                                                      \
    } while (false)

static int check_generation_change(void)
{
    gdox_physical_media_monitor monitor;
    gdox_media_observation media = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(7),
    };

    gdox_physical_media_monitor_initialize(&monitor);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    media.generation = UINT64_C(8);
    media.readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_CHANGED);
    CHECK(gdox_physical_media_monitor_connection(&monitor, true, false)
        == GDOX_PHYSICAL_MEDIA_EVENT_CHANGED);
    return 0;
}

static int check_absence_debounce(void)
{
    gdox_physical_media_monitor monitor;
    gdox_media_observation media = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(3),
    };

    gdox_physical_media_monitor_initialize(&monitor);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    media.readiness = GDOX_MEDIA_READINESS_ABSENT;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    media.readiness = GDOX_MEDIA_READINESS_PRESENT;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    media.readiness = GDOX_MEDIA_READINESS_ABSENT;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_CHANGED);
    return 0;
}

static int check_disconnect_debounce(void)
{
    gdox_physical_media_monitor monitor;
    uint32_t attempt;

    gdox_physical_media_monitor_initialize(&monitor);
    for (attempt = 1U;
         attempt < GDOX_PHYSICAL_MEDIA_DISCONNECT_CONFIRMATIONS;
         ++attempt) {
        CHECK(gdox_physical_media_monitor_connection(&monitor, true, false)
            == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    }
    CHECK(gdox_physical_media_monitor_connection(&monitor, false, false)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    CHECK(monitor.disconnected_observations
        == GDOX_PHYSICAL_MEDIA_DISCONNECT_CONFIRMATIONS - 1U);
    CHECK(gdox_physical_media_monitor_connection(&monitor, true, true)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    for (attempt = 1U;
         attempt < GDOX_PHYSICAL_MEDIA_DISCONNECT_CONFIRMATIONS;
         ++attempt) {
        CHECK(gdox_physical_media_monitor_connection(&monitor, true, false)
            == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    }
    CHECK(gdox_physical_media_monitor_connection(&monitor, true, false)
        == GDOX_PHYSICAL_MEDIA_EVENT_DISCONNECTED);
    return 0;
}

static int check_session_fault(void)
{
    gdox_physical_media_monitor monitor;
    gdox_media_observation media = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(4),
    };
    uint32_t attempt;

    gdox_physical_media_monitor_initialize(&monitor);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    gdox_physical_media_monitor_session_fault(&monitor);
    media.readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    for (attempt = 1U;
         attempt < GDOX_PHYSICAL_MEDIA_FAULT_CONFIRMATIONS;
         ++attempt) {
        CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
            == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    }
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT);

    gdox_physical_media_monitor_initialize(&monitor);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    gdox_physical_media_monitor_session_fault(&monitor);
    media.readiness = GDOX_MEDIA_READINESS_PRESENT;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT);
    return 0;
}

static int check_typed_event_priority(void)
{
    gdox_physical_media_monitor monitor;
    gdox_media_observation media = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(9),
        .event = GDOX_MEDIA_EVENT_NEW_MEDIA,
    };

    gdox_physical_media_monitor_initialize(&monitor);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);

    gdox_physical_media_monitor_initialize(&monitor);
    gdox_physical_media_monitor_session_fault(&monitor);
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_SESSION_FAULT);

    gdox_physical_media_monitor_initialize(&monitor);
    media.event = GDOX_MEDIA_EVENT_NONE;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_NONE);
    gdox_physical_media_monitor_session_fault(&monitor);
    media.generation = UINT64_C(10);
    media.readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    media.event = GDOX_MEDIA_EVENT_CHANGED;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_CHANGED);

    gdox_physical_media_monitor_initialize(&monitor);
    gdox_physical_media_monitor_session_fault(&monitor);
    media.event = GDOX_MEDIA_EVENT_EJECT_REQUEST;
    CHECK(gdox_physical_media_monitor_observe(&monitor, &media)
        == GDOX_PHYSICAL_MEDIA_EVENT_EJECT_REQUEST);
    return 0;
}

static int check_eject_request_identity(void)
{
    gdox_media_observation media = {
        .readiness = GDOX_MEDIA_READINESS_PRESENT,
        .generation = UINT64_C(12),
        .event = GDOX_MEDIA_EVENT_EJECT_REQUEST,
    };

    CHECK(gdox_physical_media_eject_request_matches(
        &media, UINT64_C(12)
    ));
    media.generation = UINT64_C(13);
    CHECK(!gdox_physical_media_eject_request_matches(
        &media, UINT64_C(12)
    ));
    media.generation = UINT64_C(12);
    media.readiness = GDOX_MEDIA_READINESS_ABSENT;
    CHECK(!gdox_physical_media_eject_request_matches(
        &media, UINT64_C(12)
    ));
    media.readiness = GDOX_MEDIA_READINESS_PRESENT;
    media.event = GDOX_MEDIA_EVENT_CHANGED;
    CHECK(!gdox_physical_media_eject_request_matches(
        &media, UINT64_C(12)
    ));
    return 0;
}

int main(void)
{
    int result = check_generation_change();

    if (result == 0) result = check_absence_debounce();
    if (result == 0) result = check_disconnect_debounce();
    if (result == 0) result = check_session_fault();
    if (result == 0) result = check_typed_event_priority();
    if (result == 0) result = check_eject_request_identity();
    return result;
}
