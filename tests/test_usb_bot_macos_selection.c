#include "platform/usb_bot.h"
#include "platform/macos_mount_guard.h"

#include <stdio.h>
#include <string.h>

typedef struct GdoxMacScsiDevice { uint64_t registry_id; } GdoxMacScsiDevice;
static GdoxMacScsiDevice fake_devices[] = {{111U}, {222U}};
static gdox_macos_mount_guards guards;
static unsigned int failures, opens, releases, commands, guard_starts, guard_clears;
static bool selected_present = true;
static bool replace_during_retry;
static char selected_id[GDOX_OPTICAL_DEVICE_ID_CAPACITY];

static void check(bool condition, const char *message)
{
    if (!condition) { (void)fprintf(stderr, "%s\n", message); ++failures; }
}

int gdox_macos_scsi_start_mount_guard(char *error, size_t capacity)
{ (void)error; (void)capacity; ++guard_starts; return 0; }
void gdox_macos_scsi_clear_mount_guard(uint64_t registry_id)
{
    check(registry_id == 111U || registry_id == 222U, "clear only the selected generation's guard");
    gdox_macos_mount_guard_release(&guards, registry_id);
    ++guard_clears;
}

bool gdox_macos_scsi_list_devices(gdox_usb_bot_device *devices, size_t capacity,
    size_t *count, const gdox_optical_media_query *query, gdox_error *error)
{
    const size_t total = selected_present ? 2U : 1U;
    gdox_error_clear(error);
    check(!query->enabled, "selection inventory remains non-commanding");
    if (capacity < total) return false;
    memset(devices, 0, total * sizeof(*devices));
    for (size_t index = 0U; index < total; ++index) {
        devices[index].identity = GDOX_USB_BOT_GP63;
        devices[index].accessible = true;
        (void)snprintf(devices[index].id, sizeof(devices[index].id), "macos:%s",
                       index == 0U ? "first" : "second");
    }
    *count = total;
    return true;
}

int gdox_macos_scsi_open(int identity, const char *device_id, uint64_t *registry_id,
    GdoxMacScsiDevice **output, char *error, size_t capacity)
{
    (void)identity;
    const size_t selected = strcmp(device_id, "macos:first") == 0 ? 0U : 1U;
    check(strcmp(device_id, selected_id) == 0, "never retry another matching model");
    *output = NULL;
    ++opens;
    if (opens == 1U) {
        check(*registry_id == 0U, "first selection has no previous native generation");
        *registry_id = fake_devices[selected].registry_id;
        check(gdox_macos_mount_guard_acquire(&guards, *registry_id), "acquire this request's guard reference");
        return 7;
    }
    check(*registry_id == fake_devices[selected].registry_id, "retry pins the original IORegistryEntryID");
    if (replace_during_retry) {
        (void)snprintf(error, capacity, "selected physical device disconnected");
        return 1;
    }
    *output = &fake_devices[selected];
    return 0;
}

int gdox_macos_scsi_release_system_media(int identity, const char *device_id,
    uint64_t registry_id, char *error, size_t capacity)
{
    (void)identity; (void)error; (void)capacity;
    check(strcmp(device_id, selected_id) == 0 && (registry_id == 111U || registry_id == 222U),
           "unmount only the explicitly selected physical generation");
    ++releases;
    return 0;
}

void gdox_macos_scsi_close(GdoxMacScsiDevice *device)
{
    check(device == &fake_devices[0] || device == &fake_devices[1], "close the opened device only");
    gdox_macos_mount_guard_release(&guards, device->registry_id);
}
int gdox_macos_scsi_observe_all(int *drives, int *media)
{ (void)drives; (void)media; return 1; }

int gdox_macos_scsi_command_in(GdoxMacScsiDevice *device, const uint8_t *cdb,
    size_t cdb_length, uint8_t *data, size_t data_length, uint32_t timeout_ms,
    uint8_t *sense, size_t sense_capacity, size_t *transferred, char *error, size_t capacity)
{
    (void)device; (void)cdb; (void)cdb_length; (void)data; (void)data_length;
    (void)timeout_ms; (void)sense; (void)sense_capacity; (void)transferred;
    (void)error; (void)capacity; ++commands; return 1;
}
int gdox_macos_scsi_command_out(GdoxMacScsiDevice *device, const uint8_t *cdb,
    size_t cdb_length, const uint8_t *data, size_t data_length, uint32_t timeout_ms,
    uint8_t *sense, size_t sense_capacity, size_t *transferred, char *error, size_t capacity)
{
    (void)device; (void)cdb; (void)cdb_length; (void)data; (void)data_length;
    (void)timeout_ms; (void)sense; (void)sense_capacity; (void)transferred;
    (void)error; (void)capacity; ++commands; return 1;
}
int gdox_macos_scsi_command_none(GdoxMacScsiDevice *device, const uint8_t *cdb,
    size_t cdb_length, uint32_t timeout_ms, uint8_t *sense, size_t sense_capacity,
    char *error, size_t capacity)
{
    (void)device; (void)cdb; (void)cdb_length; (void)timeout_ms; (void)sense;
    (void)sense_capacity; (void)error; (void)capacity; ++commands; return 1;
}

static void test_guard_ownership(void)
{
    gdox_macos_mount_guards owned = {0};
    check(gdox_macos_mount_guard_acquire(&owned, 111U), "guard old pending cleanup source");
    check(gdox_macos_mount_guard_acquire(&owned, 222U), "guard new healthy source");
    check(gdox_macos_mount_guard_acquire(&owned, 111U), "second request references same physical source");
    gdox_macos_mount_guard_release(&owned, 111U);
    check(gdox_macos_mount_guard_contains(&owned, 111U), "failed second request cannot clear original owner");
    gdox_macos_mount_guard_release(&owned, 222U);
    check(gdox_macos_mount_guard_contains(&owned, 111U)
        && !gdox_macos_mount_guard_contains(&owned, 222U), "new source close preserves pending old source");
    check(!gdox_macos_mount_guard_contains(&owned, 333U), "unrelated drive never guarded");
    gdox_macos_mount_guard_release(&owned, 111U);
    check(!gdox_macos_mount_guard_contains(&owned, 111U), "last owner releases guard");
    for (size_t index = 0U; index < GDOX_OPTICAL_MAX_DEVICES; ++index) {
        check(gdox_macos_mount_guard_acquire(&owned, index + 1U), "fill bounded guard set");
    }
    check(!gdox_macos_mount_guard_acquire(&owned, 999U), "full set refuses extra ownership");
    check(gdox_macos_mount_guard_contains(&owned, 1U), "capacity failure preserves older guard");
}

int main(void)
{
    gdox_usb_bot_device devices[4];
    gdox_scsi_transport transport = {0};
    gdox_error error;
    size_t count;
    bool connected;
    test_guard_ownership();
    check(gdox_usb_bot_list_devices(devices, 4U, &count, false, &error), "list duplicate devices");
    check(count == 2U && strcmp(devices[0].id, devices[1].id) != 0, "keep both physical instances");
    check(commands == 0U && opens == 0U && guard_starts == 0U, "inventory must not open or guard media");
    check(gdox_usb_bot_device_connected(GDOX_USB_BOT_GP63, "macos:second", &connected, &error)
           && connected, "find the selected second instance");
    selected_present = false;
    check(gdox_usb_bot_device_connected(GDOX_USB_BOT_GP63, "macos:second", &connected, &error)
           && !connected, "another same-model device does not mean selected device is connected");
    selected_present = true;
    (void)snprintf(selected_id, sizeof(selected_id), "macos:second");
    check(gdox_usb_bot_open_device(GDOX_USB_BOT_GP63, selected_id, &transport, &error), "open selected instance after unmount");
    check(opens == 2U && releases == 1U && commands == 0U, "bounded selected-device retry only");
    gdox_scsi_transport another = {0};
    opens = releases = 0U;
    (void)snprintf(selected_id, sizeof(selected_id), "macos:first");
    check(gdox_usb_bot_open_device(GDOX_USB_BOT_GP63, selected_id, &another, &error), "open healthy drive while old source remains owned");
    check(gdox_macos_mount_guard_contains(&guards, 111U)
        && gdox_macos_mount_guard_contains(&guards, 222U), "both still-owned sources retain guards");
    check(gdox_scsi_transport_close(&another, &error), "close healthy drive independently");
    check(gdox_macos_mount_guard_contains(&guards, 222U)
        && !gdox_macos_mount_guard_contains(&guards, 111U), "pending old source keeps its guard");
    check(gdox_scsi_transport_close(&transport, &error), "close selected transport");
    (void)snprintf(selected_id, sizeof(selected_id), "macos:second");
    opens = releases = 0U;
    replace_during_retry = true;
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP63, selected_id, &transport, &error), "refuse replacement during mount-busy retry");
    check(opens == 2U && releases == 1U && guard_clears == 1U, "clear selected guard on failure");
    check(!gdox_scsi_transport_is_valid(&transport), "failed selection leaves no transport");
    opens = guard_starts = 0U;
    check(!gdox_usb_bot_open_device(GDOX_USB_BOT_GP57, selected_id, &transport, &error), "GP57 stays Windows-only");
    check(opens == 0U && guard_starts == 0U, "unsupported profile cannot guard or claim macOS media");
    return failures == 0U ? 0 : 1;
}
