#include "app/runtime_drives.h"
#include "app/runtime_playback.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gdox_test_failures = 0;

typedef struct fake_owner {
    bool fail;
    unsigned int closes;
} fake_owner;

static gdox_optical_device inventory[4];
static size_t inventory_count;
static bool last_query_media;
static unsigned int media_queries[4];
static size_t last_excluded_count;
static bool playing;
static bool inventory_fails;
static gdox_preferences saved_preferences;
static const gdox_sector_source_ops source_ops = {0};
static const gdox_random_disc_ops disc_ops = {0};

void gdox_runtime_copy_text(char *out, size_t capacity, const char *text)
{
    (void)snprintf(out, capacity, "%s", text);
}

bool gdox_runtime_playback_running(const gdox_runtime *runtime)
{
    (void)runtime;
    return playing;
}

bool gdox_preferences_save(const gdox_preferences *preferences, gdox_error *error)
{
    saved_preferences = *preferences;
    gdox_error_clear(error);
    return true;
}

bool gdox_optical_list_devices_filtered(
    gdox_optical_device *devices, size_t capacity, size_t *count,
    const gdox_optical_media_query *query, gdox_error *error
)
{
    if (inventory_fails) {
        *count = 0U;
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected inventory failure");
        return false;
    }
    if (inventory_count > capacity) {
        ++gdox_test_failures;
        return false;
    }
    memcpy(devices, inventory, inventory_count * sizeof(*devices));
    *count = inventory_count;
    last_query_media = query->enabled;
    last_excluded_count = query->excluded_device_count;
    memset(media_queries, 0, sizeof(media_queries));
    for (size_t index = 0U; index < inventory_count; ++index) {
        bool excluded = !query->enabled;
        for (size_t owner = 0U; owner < query->excluded_device_count; ++owner) {
            excluded = excluded
                || strcmp(devices[index].id, query->excluded_device_ids[owner]) == 0;
        }
        if (excluded) {
            devices[index].media_status_known = false;
            devices[index].media_present = false;
        } else {
            ++media_queries[index];
        }
    }
    gdox_error_clear(error);
    return true;
}

bool gdox_runtime_media_is_owned(const gdox_runtime_media_session *session)
{
    return session->open || session->exported != NULL
        || session->retained_source.context != NULL
        || session->validated_disc.context != NULL;
}

bool gdox_runtime_media_close(gdox_runtime_media_session *session, gdox_error *error)
{
    fake_owner *owner = session->retained_source.context;
    if (owner == NULL) {
        memset(session, 0, sizeof(*session));
        return true;
    }
    ++owner->closes;
    if (session->validated_disc.context != owner
        || session->exported != (gdox_nbd_export *)owner) {
        ++gdox_test_failures;
        return false;
    }
    session->open = false;
    if (owner->fail) {
        gdox_error_set(error, GDOX_ERROR_IO, "injected restoration failure");
        return false;
    }
    memset(session, 0, sizeof(*session));
    gdox_error_clear(error);
    return true;
}

static gdox_optical_device device(const char *id, gdox_optical_drive drive,
    const char *location, bool media)
{
    gdox_optical_device result = {0};
    gdox_runtime_copy_text(result.id, sizeof(result.id), id);
    gdox_runtime_copy_text(result.name, sizeof(result.name),
        drive == GDOX_OPTICAL_DRIVE_ASUS_MT1862 ? "ASUS DRW-24D5MT 2.00" : "GP57EB40 PB00");
    gdox_runtime_copy_text(result.location, sizeof(result.location), location);
    result.drive = drive;
    result.accessible = true;
    result.media_status_known = true;
    result.media_present = media;
    return result;
}

static void own(gdox_runtime_media_session *session, fake_owner *owner)
{
    *session = (gdox_runtime_media_session){0};
    session->open = true;
    session->info.source = GDOX_MEDIA_PHYSICAL_DISC;
    session->exported = (gdox_nbd_export *)owner;
    session->retained_source.context = owner;
    session->retained_source.ops = &source_ops;
    session->validated_disc.context = owner;
    session->validated_disc.ops = &disc_ops;
}

static void run(gdox_runtime *runtime, gdox_runtime_snapshot *snapshot)
{
    fake_owner old = {true, 0U};
    fake_owner current = {false, 0U};
    fake_owner full[GDOX_OPTICAL_MAX_DEVICES] = {{0}};
    gdox_optical_presence presence;
    gdox_error error;

    inventory[0] = device("sata:internal", GDOX_OPTICAL_DRIVE_ASUS_MT1862, "D:", false);
    inventory[1] = device("usb:one", GDOX_OPTICAL_DRIVE_GP57, "E:", true);
    inventory[2] = device("usb:two", GDOX_OPTICAL_DRIVE_GP57, "F:", false);
    inventory_count = 3U;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(last_query_media);
    GDOX_TEST_CHECK(snapshot->optical_device_count == 3U);
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:one") == 0);

    GDOX_TEST_CHECK(gdox_runtime_drives_select(runtime, snapshot, "usb:two", &error));
    GDOX_TEST_CHECK(strcmp(saved_preferences.optical_device_id, "usb:two") == 0);
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:two") == 0);
    GDOX_TEST_CHECK(!presence.media_present);
    inventory_count = 2U;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    GDOX_TEST_CHECK(strcmp(runtime->snapshot.settings.optical_device_id, "usb:two") == 0);
    inventory_count = 3U;
    inventory[2].drive = GDOX_OPTICAL_DRIVE_NONE;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    inventory[2].drive = GDOX_OPTICAL_DRIVE_GP57;

    GDOX_TEST_CHECK(gdox_runtime_drives_select(runtime, snapshot, "sata:internal", &error));
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    own(&runtime->media, &old);
    runtime->media.open = false;
    GDOX_TEST_CHECK(gdox_runtime_drives_select(runtime, snapshot, "usb:one", &error));
    GDOX_TEST_CHECK(old.closes == 1U);
    GDOX_TEST_CHECK(!gdox_runtime_media_is_owned(&runtime->media));
    GDOX_TEST_CHECK(gdox_runtime_drives_cleanup_pending(runtime));
    GDOX_TEST_CHECK(strcmp(runtime->pending_cleanup[0].device.id, "sata:internal") == 0);
    GDOX_TEST_CHECK(runtime->pending_cleanup[0].media.retained_source.context == &old);
    GDOX_TEST_CHECK(runtime->pending_cleanup[0].media.validated_disc.context == &old);
    GDOX_TEST_CHECK(runtime->pending_cleanup[0].media.exported == (gdox_nbd_export *)&old);
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(last_query_media && last_excluded_count == 1U);
    GDOX_TEST_CHECK(media_queries[0] == 0U && media_queries[1] == 1U
        && media_queries[2] == 1U);
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:one") == 0);
    /* Pending restoration must not trap Automatic on an empty healthy drive. */
    runtime->snapshot.settings.optical_device_id[0] = '\0';
    inventory[1].media_present = false;
    inventory[2].media_present = true;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:two") == 0);
    GDOX_TEST_CHECK(presence.media_status_known && presence.media_present);
    GDOX_TEST_CHECK(media_queries[0] == 0U && old.closes == 1U);
    GDOX_TEST_CHECK(runtime->pending_cleanup[0].media.retained_source.context == &old);
    GDOX_TEST_CHECK(gdox_runtime_drives_select(runtime, snapshot, "usb:one", &error));
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    own(&runtime->media, &current);
    gdox_runtime_drives_retry_cleanup(runtime);
    GDOX_TEST_CHECK(old.closes == 1U);
    runtime->snapshot.settings.optical_device_id[0] = '\0';
    inventory[1].media_present = false;
    inventory[2].media_present = true;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!last_query_media && last_excluded_count == 2U);
    GDOX_TEST_CHECK(media_queries[0] == 0U && media_queries[1] == 0U
        && media_queries[2] == 0U);
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:one") == 0);
    inventory_fails = true;
    GDOX_TEST_CHECK(!gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(snapshot->optical_inventory_notice[0] != '\0');
    GDOX_TEST_CHECK(!snapshot->optical_devices[1].connected);
    GDOX_TEST_CHECK(gdox_runtime_drives_resolve(runtime, snapshot, &presence, &error));
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:one") == 0);
    inventory_fails = false;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    gdox_runtime_copy_text(runtime->snapshot.settings.optical_device_id,
        sizeof(runtime->snapshot.settings.optical_device_id), "usb:one");
    runtime->media.open = false;
    gdox_runtime_drives_retry_cleanup(runtime);
    GDOX_TEST_CHECK(old.closes == 2U);
    GDOX_TEST_CHECK(current.closes == 0U);
    GDOX_TEST_CHECK(runtime->media.retained_source.context == &current);
    GDOX_TEST_CHECK(strcmp(runtime->optical_device.id, "usb:one") == 0);
    GDOX_TEST_CHECK(!gdox_runtime_drives_close_pending(runtime, &error));
    GDOX_TEST_CHECK(gdox_runtime_drives_cleanup_pending(runtime));
    gdox_runtime_drives_describe(runtime, snapshot);
    GDOX_TEST_CHECK(snapshot->pending_cleanup_count == 1U);
    GDOX_TEST_CHECK(strstr(snapshot->pending_cleanup_notice, "ASUS") != NULL);
    playing = true;
    GDOX_TEST_CHECK(!gdox_runtime_drives_select(runtime, snapshot, "usb:two", &error));
    GDOX_TEST_CHECK(current.closes == 0U);
    playing = false;
    snapshot->phase = GDOX_RUNTIME_PRESERVING;
    GDOX_TEST_CHECK(!gdox_runtime_drives_select(runtime, snapshot, "usb:two", &error));
    GDOX_TEST_CHECK(current.closes == 0U);
    snapshot->phase = GDOX_RUNTIME_EMPTY;
    old.fail = false;
    GDOX_TEST_CHECK(gdox_runtime_drives_close_pending(runtime, &error));
    GDOX_TEST_CHECK(!gdox_runtime_drives_cleanup_pending(runtime));
    GDOX_TEST_CHECK(current.closes == 0U);
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!last_query_media);
    GDOX_TEST_CHECK(gdox_runtime_media_close(&runtime->media, &error));
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(last_query_media);
    /* Busy states remain non-commanding even without runtime->media ownership. */
    playing = true;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!last_query_media);
    playing = false;
    snapshot->phase = GDOX_RUNTIME_PRESERVING;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!last_query_media);
    snapshot->phase = GDOX_RUNTIME_PREPARING;
    GDOX_TEST_CHECK(gdox_runtime_drives_refresh(runtime, snapshot, &error));
    GDOX_TEST_CHECK(!last_query_media);
    snapshot->phase = GDOX_RUNTIME_EMPTY;

    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        full[index].fail = true;
        own(&runtime->pending_cleanup[index].media, &full[index]);
        runtime->pending_cleanup[index].device = inventory[0];
        (void)snprintf(runtime->pending_cleanup[index].device.id,
            sizeof(runtime->pending_cleanup[index].device.id), "pending:%zu", index);
    }
    current.fail = true;
    own(&runtime->media, &current);
    GDOX_TEST_CHECK(!gdox_runtime_drives_select(runtime, snapshot, "usb:two", &error));
    GDOX_TEST_CHECK(runtime->media.retained_source.context == &current);
    GDOX_TEST_CHECK(strcmp(runtime->snapshot.settings.optical_device_id, "usb:one") == 0);
    current.fail = false;
    GDOX_TEST_CHECK(gdox_runtime_media_close(&runtime->media, &error));
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        full[index].fail = false;
    }
    GDOX_TEST_CHECK(gdox_runtime_drives_close_pending(runtime, &error));
}

int main(void)
{
    gdox_runtime *runtime = calloc(1U, sizeof(*runtime));
    gdox_runtime_snapshot *snapshot = calloc(1U, sizeof(*snapshot));
    if (runtime == NULL || snapshot == NULL || !gdox_mutex_init(&runtime->mutex)) {
        return 1;
    }
    run(runtime, snapshot);
    gdox_mutex_destroy(&runtime->mutex);
    free(snapshot);
    free(runtime);
    return gdox_test_failures == 0 ? 0 : 1;
}
