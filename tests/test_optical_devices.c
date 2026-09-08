#include "platform/optical_driver.h"
#include "platform/asus_nr09_source.h"
#include "platform/gp08_source.h"
#include "platform/mt1887_source.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures;
static unsigned int backend_calls;
static unsigned int source_closes;
static gdox_usb_bot_identity routed_identity;
static char routed_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
static uint16_t routed_speed;
static uint8_t routed_retries;
static uint32_t routed_timeout;
static bool fail_with_owned_source;
static bool list_failure;
static bool list_overflow;
static bool listed_query_media;
static size_t listed_excluded_count;
static char listed_excluded_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
static gdox_usb_bot_device inventory[3];
static gdox_mt1887_media_profile selected_media;
static gdox_asus_nr09_media_kind selected_asus_media;
static unsigned int source_owner;

static void check(bool condition, const char *message)
{
    if (!condition) {
        ++failures;
        (void)fprintf(stderr, "%s\n", message);
    }
}

static uint64_t fake_sector_count(const void *context)
{
    (void)context;
    return 100U;
}

static bool fake_read(void *context, uint64_t lba, uint32_t blocks,
    uint8_t *output, size_t bytes, gdox_error *error)
{
    (void)context; (void)lba; (void)blocks; (void)output; (void)bytes;
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected read in routing test");
    return false;
}

static bool fake_close(void *context, gdox_error *error)
{
    check(context == &source_owner, "source keeps original cleanup owner");
    ++source_closes;
    gdox_error_clear(error);
    return true;
}

static const gdox_sector_source_ops source_ops = {
    fake_sector_count, fake_read, NULL, fake_close, NULL, NULL, NULL, NULL, NULL,
};

bool gdox_usb_bot_open_device(gdox_usb_bot_identity identity, const char *id,
    gdox_scsi_transport *transport, gdox_error *error)
{
    (void)transport;
    ++backend_calls;
    routed_identity = identity;
    (void)snprintf(routed_id, sizeof(routed_id), "%s", id);
    gdox_error_clear(error);
    return true;
}

bool gdox_usb_bot_device_connected(gdox_usb_bot_identity identity, const char *id,
    bool *connected, gdox_error *error)
{
    gdox_scsi_transport transport = {0};
    const bool success = gdox_usb_bot_open_device(identity, id, &transport, error);
    *connected = true;
    return success;
}

bool gdox_usb_bot_list_devices_filtered(gdox_usb_bot_device *devices, size_t capacity,
    size_t *count, const gdox_optical_media_query *query, gdox_error *error)
{
    ++backend_calls;
    listed_query_media = query->enabled;
    listed_excluded_count = query->excluded_device_count;
    if (listed_excluded_count != 0U) {
        (void)snprintf(listed_excluded_id, sizeof(listed_excluded_id), "%s",
            query->excluded_device_ids[0]);
    }
    *count = capacity < 3U ? capacity : 3U;
    memcpy(devices, inventory, *count * sizeof(*devices));
    if (list_overflow) *count = capacity + 1U;
    if (list_failure) gdox_error_set(error, GDOX_ERROR_IO, "inventory unavailable");
    return !list_failure;
}

bool gdox_usb_bot_observe_all(gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT], gdox_error *error)
{
    (void)observations; (void)error;
    check(false, "device API must not use aggregated observation");
    return false;
}

bool gdox_usb_bot_present_all(bool present[GDOX_USB_BOT_IDENTITY_COUNT], gdox_error *error)
{
    (void)present; (void)error;
    check(false, "device API must not use aggregated presence");
    return false;
}

static bool open_profile(gdox_mt1887_transport_opener opener, void *context,
    uint8_t retries, uint32_t timeout, gdox_sector_source *source, gdox_error *error)
{
    gdox_scsi_transport transport = {0};
    routed_retries = retries;
    routed_timeout = timeout;
    if (!opener(context, &transport, error)) return false;
    *source = (gdox_sector_source){&source_owner, &source_ops};
    if (fail_with_owned_source) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "restore must be retried");
        return false;
    }
    return true;
}

bool gdox_gp08_source_open(gdox_gp08_transport_opener opener, void *context,
    uint8_t retries, uint32_t timeout, gdox_sector_source *source, gdox_error *error)
{
    return open_profile(opener, context, retries, timeout, source, error);
}

bool gdox_mt1887_source_open(gdox_mt1887_transport_opener opener, void *context,
    gdox_usb_bot_identity identity, uint16_t speed, uint8_t retries,
    uint32_t timeout, gdox_sector_source *source, gdox_error *error)
{
    const bool success = open_profile(opener, context, retries, timeout, source, error);
    routed_speed = speed;
    check(identity == routed_identity, "MT profile and transport identities agree");
    return success;
}

bool gdox_mt1887_detected_source_open_for_identity(gdox_mt1887_transport_opener opener,
    void *context, gdox_usb_bot_identity identity, uint16_t speed, uint8_t retries,
    uint32_t timeout, gdox_sector_source *source,
    const gdox_mt1887_media_profile **media, gdox_error *error)
{
    *media = &selected_media;
    return gdox_mt1887_source_open(opener, context, identity, speed, retries,
        timeout, source, error);
}

bool gdox_asus_nr09_detected_source_open(gdox_asus_nr09_transport_opener opener,
    void *context, uint8_t retries, uint32_t timeout, gdox_sector_source *source,
    gdox_asus_nr09_media_kind *media, gdox_error *error)
{
    *media = selected_asus_media;
    return open_profile(opener, context, retries, timeout, source, error);
}

bool gdox_gp08_source_eject(gdox_gp08_transport_opener opener, void *context, gdox_error *error)
{
    gdox_scsi_transport transport = {0};
    return opener(context, &transport, error);
}

bool gdox_mt1887_source_eject(gdox_mt1887_transport_opener opener, void *context,
    gdox_usb_bot_identity identity, gdox_error *error)
{
    gdox_scsi_transport transport = {0};
    const bool success = opener(context, &transport, error);
    check(identity == routed_identity, "eject profile and physical identity agree");
    return success;
}

/* Link the real registry so mapping/eject/read-size policy is exercised. */
static bool legacy_call(gdox_error *error)
{
    check(false, "selected device must never use a model-only legacy opener");
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "unexpected legacy opener");
    return false;
}
#define LEGACY_OPEN(name) \
    bool name(uint8_t retries, uint32_t timeout, gdox_sector_source *source, gdox_error *error) \
    { (void)retries; (void)timeout; (void)source; return legacy_call(error); }
#define LEGACY_MEDIA(name) \
    bool name(uint8_t retries, uint32_t timeout, gdox_sector_source *source, \
        gdox_optical_media_info *info, gdox_error *error) \
    { (void)retries; (void)timeout; (void)source; (void)info; return legacy_call(error); }
LEGACY_OPEN(gdox_optical_open_gp63)
LEGACY_OPEN(gdox_optical_open_gp65)
LEGACY_OPEN(gdox_optical_open_gp08)
LEGACY_OPEN(gdox_optical_open_sp80)
LEGACY_OPEN(gdox_optical_open_asus_nr09)
LEGACY_OPEN(gdox_optical_open_asus_mt1862)
LEGACY_OPEN(gdox_optical_open_gp57)
LEGACY_MEDIA(gdox_optical_open_gp63_media)
LEGACY_MEDIA(gdox_optical_open_asus_nr09_media)
LEGACY_MEDIA(gdox_optical_open_asus_mt1862_media)
LEGACY_MEDIA(gdox_optical_open_gp57_media)
bool gdox_optical_eject_gp63(gdox_error *error) { return legacy_call(error); }
bool gdox_optical_eject_gp65(gdox_error *error) { return legacy_call(error); }
bool gdox_optical_eject_gp08(gdox_error *error) { return legacy_call(error); }

static gdox_optical_device selected_device(gdox_optical_drive drive)
{
    gdox_optical_device device = {0};
    device.drive = drive;
    device.accessible = true;
    (void)snprintf(device.id, sizeof(device.id), "physical-unit-B");
    return device;
}

static void test_inventory_mapping(void)
{
    gdox_optical_device devices[3];
    gdox_error error;
    size_t count = 0U;
    memset(inventory, 0, sizeof(inventory));
    inventory[0].identity = GDOX_USB_BOT_GP57;
    (void)snprintf(inventory[0].id, sizeof(inventory[0].id), "z-drive");
    inventory[0].connection = GDOX_OPTICAL_CONNECTION_USB;
    inventory[0].media_status_known = true;
    inventory[0].media_present = true;
    inventory[0].accessible = true;
    inventory[1] = inventory[0];
    (void)snprintf(inventory[1].id, sizeof(inventory[1].id), "a-drive");
    inventory[2].identity = GDOX_USB_BOT_IDENTITY_COUNT;
    (void)snprintf(inventory[2].id, sizeof(inventory[2].id), "m-drive");
    (void)snprintf(inventory[2].name, sizeof(inventory[2].name), "Unknown optical model");
    check(gdox_optical_list_devices(devices, 3U, &count, false, &error)
        && count == 3U && !listed_query_media, "inventory forwards passive mode");
    check(strcmp(devices[0].id, "a-drive") == 0 && strcmp(devices[2].id, "z-drive") == 0
        && devices[0].drive == GDOX_OPTICAL_DRIVE_GP57
        && devices[2].drive == GDOX_OPTICAL_DRIVE_GP57, "registry maps duplicate physical rows and sorts by stable ID");
    check(devices[1].drive == GDOX_OPTICAL_DRIVE_NONE
        && strcmp(devices[1].name, "Unknown optical model") == 0,
        "unknown devices retain their display identity without becoming supported");
    check(devices[0].media_present && devices[0].media_status_known
        && devices[0].accessible && devices[0].connection == GDOX_OPTICAL_CONNECTION_USB,
        "inventory copies per-device state");
    {
        const char *excluded[] = {"z-drive"};
        gdox_optical_media_query query = {
            .enabled = true, .excluded_device_ids = excluded,
            .excluded_device_count = 1U,
        };
        check(gdox_optical_list_devices_filtered(devices, 3U, &count, &query, &error)
            && count == 3U && listed_query_media && listed_excluded_count == 1U
            && strcmp(listed_excluded_id, "z-drive") == 0,
            "exact media-query exclusions reach backend without hiding inventory rows");
        query.excluded_device_ids = NULL;
        check(!gdox_optical_list_devices_filtered(devices, 3U, &count, &query, &error)
            && count == 0U && error.code == GDOX_ERROR_INVALID_ARGUMENT,
            "missing exclusion storage is rejected");
        excluded[0] = "";
        query.excluded_device_ids = excluded;
        check(!gdox_optical_list_devices_filtered(devices, 3U, &count, &query, &error),
            "empty excluded IDs are rejected");
        check(!gdox_optical_list_devices_filtered(devices, 3U, &count, NULL, &error),
            "missing media query options are rejected");
    }
    list_failure = true;
    check(!gdox_optical_list_devices(devices, 3U, &count, true, &error)
        && count == 3U && listed_query_media && error.code == GDOX_ERROR_IO,
        "backend inventory failure remains visible with bounded partial results");
    list_failure = false;
    list_overflow = true;
    check(!gdox_optical_list_devices(devices, 3U, &count, false, &error)
        && count == 0U, "backend count overflow is rejected");
    list_overflow = false;
    memset(inventory[0].id, 'x', sizeof(inventory[0].id));
    check(!gdox_optical_list_devices(devices, 3U, &count, false, &error)
        && count == 0U, "unterminated opaque ID is rejected rather than truncated");
    inventory[0].id[0] = '\0';
    check(!gdox_optical_list_devices(devices, 3U, &count, false, &error),
        "empty opaque device ID is rejected");
}

static void test_open_routing(void)
{
    const gdox_optical_drive drives[] = {GDOX_OPTICAL_DRIVE_GP63, GDOX_OPTICAL_DRIVE_GP65,
        GDOX_OPTICAL_DRIVE_GP08, GDOX_OPTICAL_DRIVE_ASUS_NR09, GDOX_OPTICAL_DRIVE_SP80,
        GDOX_OPTICAL_DRIVE_ASUS_MT1862, GDOX_OPTICAL_DRIVE_GP57};
    const gdox_usb_bot_identity identities[] = {GDOX_USB_BOT_GP63, GDOX_USB_BOT_GP65,
        GDOX_USB_BOT_GP08, GDOX_USB_BOT_ASUS_NR09, GDOX_USB_BOT_SP80,
        GDOX_SATA_ASUS_MT1862, GDOX_USB_BOT_GP57};
    for (size_t index = 0U; index < sizeof(drives) / sizeof(drives[0]); ++index) {
        gdox_optical_device device = selected_device(drives[index]);
        gdox_sector_source source = {0};
        gdox_optical_media_info info;
        gdox_error error;
        selected_media.kind = index >= 5U ? GDOX_MT1887_MEDIA_GP63_XGD2 : GDOX_MT1887_MEDIA_XGD1;
        selected_media.game_partition_lba = GDOX_XGD2_GAME_PARTITION_LBA;
        selected_asus_media = GDOX_ASUS_NR09_MEDIA_XGD1;
        check(gdox_optical_open_device_media(&device, 7U, 1234U, &source, &info, &error),
            "selected supported device opens through its existing backend");
        check(routed_identity == identities[index] && strcmp(routed_id, device.id) == 0
            && routed_retries == 7U && routed_timeout == 1234U,
            "physical ID, registry identity and read policy reach the opener unchanged");
        check(info.profile == (index >= 5U ? GDOX_OPTICAL_MEDIA_XGD2 : GDOX_OPTICAL_MEDIA_XGD1),
            "media profile maps to the existing public classification");
        if (index >= 5U) check(routed_speed == 0U && info.sequential_read_blocks == 32U
            && info.game_partition_lba == GDOX_XGD2_GAME_PARTITION_LBA,
            "experimental profiles retain speed and use bounded XGD2 reads");
        check(gdox_source_close(&source, &error), "opened device retains close ownership");
    }
    for (unsigned int media = 0U; media < 2U; ++media) {
        gdox_optical_device device = selected_device(media == 0U
            ? GDOX_OPTICAL_DRIVE_GP63 : GDOX_OPTICAL_DRIVE_ASUS_NR09);
        gdox_sector_source source = {0};
        gdox_optical_media_info info;
        gdox_error error;
        selected_media.kind = GDOX_MT1887_MEDIA_GP63_XGD3;
        selected_media.game_partition_lba = GDOX_GP63_XGD3_GAME_PARTITION_LBA;
        selected_asus_media = GDOX_ASUS_NR09_MEDIA_XGD2;
        check(gdox_optical_open_device_media(&device, 0U, 0U, &source, &info, &error),
            "detected Xbox 360 media opens through selected device");
        check(info.profile == (media == 0U ? GDOX_OPTICAL_MEDIA_XGD3 : GDOX_OPTICAL_MEDIA_XGD2)
            && info.game_partition_lba == (media == 0U ? GDOX_GP63_XGD3_GAME_PARTITION_LBA
                : GDOX_XGD2_GAME_PARTITION_LBA), "Xbox 360 partition mapping remains unchanged");
        check(gdox_source_close(&source, &error), "detected source closes");
    }
}

static void test_owned_and_invalid_outputs(void)
{
    gdox_optical_device device = selected_device(GDOX_OPTICAL_DRIVE_GP57);
    gdox_sector_source owned = {&source_owner, &source_ops};
    gdox_sector_source empty = {0};
    gdox_optical_media_info info = {GDOX_OPTICAL_MEDIA_XGD3, 99U, 23U};
    const gdox_optical_media_info saved = info;
    gdox_error error;
    unsigned int calls = backend_calls;
    check(!gdox_optical_open_device_media(&device, 0U, 0U, &owned, &info, &error)
        && owned.context == &source_owner && owned.ops == &source_ops
        && memcmp(&info, &saved, sizeof(info)) == 0 && calls == backend_calls,
        "pre-owned source and media output survive rejected replacement");
    check(!gdox_optical_open_device_media(&device, 0U, 0U, &empty, NULL, &error)
        && !gdox_source_is_valid(&empty) && calls == backend_calls,
        "NULL media output causes no backend work");
    device.drive = GDOX_OPTICAL_DRIVE_NONE;
    check(!gdox_optical_open_device_media(&device, 0U, 0U, &empty, &info, &error)
        && memcmp(&info, &saved, sizeof(info)) == 0 && calls == backend_calls,
        "unknown device selection preserves outputs and never opens a transport");
    device = selected_device(GDOX_OPTICAL_DRIVE_GP57);
    check(!gdox_optical_device_connected(&device, NULL, &error)
        && calls == backend_calls, "NULL presence output never reaches backend");
    fail_with_owned_source = true;
    check(!gdox_optical_open_device_media(&device, 0U, 0U, &empty, &info, &error)
        && gdox_source_is_valid(&empty) && empty.context == &source_owner,
        "failed initialization retains its source for restoration retry");
    fail_with_owned_source = false;
    calls = source_closes;
    check(gdox_source_close(&empty, &error) && source_closes == calls + 1U,
        "caller can close retained failed initialization exactly once");
}

static void test_connection_and_eject_routing(void)
{
    gdox_error error;
    gdox_optical_eject_completion completion;
    bool connected = false;
    gdox_optical_device device = selected_device(GDOX_OPTICAL_DRIVE_GP57);
    check(gdox_optical_device_connected(&device, &connected, &error) && connected
        && routed_identity == GDOX_USB_BOT_GP57 && strcmp(routed_id, device.id) == 0,
        "connection check targets selected physical instance");
    for (gdox_optical_drive drive = GDOX_OPTICAL_DRIVE_GP63;
         drive <= GDOX_OPTICAL_DRIVE_GP57; ++drive) {
        const bool automatic = drive == GDOX_OPTICAL_DRIVE_GP63
            || drive == GDOX_OPTICAL_DRIVE_GP65 || drive == GDOX_OPTICAL_DRIVE_GP08;
        const unsigned int calls = backend_calls;
        device = selected_device(drive);
        check(gdox_optical_complete_device_eject_request(&device, &completion, &error),
            "selected drive follows its registered tray policy");
        check(completion == (automatic ? GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED
            : GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT),
            "manual tray profiles remain manual");
        check(backend_calls == calls + (automatic ? 1U : 0U),
            "manual completion issues no transport command");
        if (automatic) check(strcmp(routed_id, device.id) == 0
            && routed_identity == gdox_optical_identity_for_drive(drive),
            "automatic ejection uses exact selected device ID and profile");
        else check(!gdox_optical_eject_device(&device, &error)
            && error.code == GDOX_ERROR_UNSUPPORTED && backend_calls == calls,
            "direct ejection cannot bypass manual tray policy");
    }
}

int main(void)
{
    test_inventory_mapping();
    test_open_routing();
    test_owned_and_invalid_outputs();
    test_connection_and_eject_routing();
    return failures == 0U ? 0 : 1;
}
