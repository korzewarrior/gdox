#include "platform/optical_driver.h"
#include "platform/optical_inventory_filter.h"

#include "platform/asus_nr09_source.h"
#include "platform/gp08_source.h"
#include "platform/mt1887_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_device(const gdox_optical_device *device, gdox_error *error)
{
    if (device == NULL || device->id[0] == '\0'
        || memchr(device->id, '\0', sizeof(device->id)) == NULL
        || gdox_optical_identity_for_drive(device->drive)
            == GDOX_USB_BOT_IDENTITY_COUNT) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "a supported physical optical drive selection is required");
        return false;
    }
    return true;
}

static int compare_devices(const void *left, const void *right)
{
    const gdox_optical_device *a = left;
    const gdox_optical_device *b = right;
    return strcmp(a->id, b->id);
}

bool gdox_optical_list_devices(
    gdox_optical_device *devices,
    size_t capacity,
    size_t *count,
    bool query_media,
    gdox_error *error
)
{
    const gdox_optical_media_query query = {.enabled = query_media};
    return gdox_optical_list_devices_filtered(devices, capacity, count, &query, error);
}

bool gdox_optical_list_devices_filtered(
    gdox_optical_device *devices,
    size_t capacity,
    size_t *count,
    const gdox_optical_media_query *query,
    gdox_error *error
)
{
    gdox_usb_bot_device *observations;
    size_t observed = 0U;
    size_t index;
    bool success;

    gdox_error_clear(error);
    if (count != NULL) {
        *count = 0U;
    }
    if (!gdox_optical_media_query_valid(query, error)) {
        return false;
    }
    if (devices == NULL || count == NULL || capacity == 0U
        || capacity > SIZE_MAX / sizeof(*observations)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "optical device storage and count are required");
        return false;
    }
    observations = calloc(capacity, sizeof(*observations));
    if (observations == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL,
            "could not allocate optical device inventory");
        return false;
    }
    success = gdox_usb_bot_list_devices_filtered(
        observations, capacity, &observed, query, error
    );
    if (observed > capacity) {
        free(observations);
        gdox_error_set(error, GDOX_ERROR_INTERNAL,
            "optical device inventory exceeded its capacity");
        return false;
    }
    for (index = 0U; index < observed; ++index) {
        if (observations[index].id[0] == '\0'
            || memchr(observations[index].id, '\0', sizeof(observations[index].id)) == NULL) {
            free(observations);
            gdox_error_set(error, GDOX_ERROR_INTERNAL,
                "optical device inventory returned an invalid physical identifier");
            return false;
        }
    }
    for (index = 0U; index < observed; ++index) {
        const gdox_usb_bot_device *input = &observations[index];
        gdox_optical_device *output = &devices[index];
        *output = (gdox_optical_device){0};
        output->drive = gdox_optical_drive_for_identity(input->identity);
        memcpy(output->id, input->id, sizeof(output->id));
        memcpy(output->location, input->location, sizeof(output->location));
        if (output->drive == GDOX_OPTICAL_DRIVE_NONE) {
            memcpy(output->name, input->name, sizeof(output->name));
        } else {
            (void)snprintf(output->name, sizeof(output->name), "%s",
                gdox_optical_drive_name(output->drive));
        }
        output->name[sizeof(output->name) - 1U] = '\0';
        output->location[sizeof(output->location) - 1U] = '\0';
        output->connection = input->connection;
        output->media_status_known = input->media_status_known;
        output->media_present = input->media_present;
        output->accessible = input->accessible;
    }
    free(observations);
    qsort(devices, observed, sizeof(*devices), compare_devices);
    *count = observed;
    return success;
}

bool gdox_optical_device_connected(
    const gdox_optical_device *device,
    bool *connected,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (connected != NULL) {
        *connected = false;
    }
    if (!valid_device(device, error)) {
        return false;
    }
    if (connected == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "optical connection output is required");
        return false;
    }
    return gdox_usb_bot_device_connected(
        gdox_optical_identity_for_drive(device->drive), device->id,
        connected, error
    );
}

static bool open_selected_transport(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    const gdox_optical_device *device = context;
    return gdox_usb_bot_open_device(
        gdox_optical_identity_for_drive(device->drive), device->id,
        transport, error
    );
}

static bool open_selected_media(
    gdox_optical_device *device,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
)
{
    const gdox_usb_bot_identity identity =
        gdox_optical_identity_for_drive(device->drive);
    const gdox_mt1887_media_profile *mt_media = NULL;
    gdox_asus_nr09_media_kind asus_media = GDOX_ASUS_NR09_MEDIA_UNKNOWN;

    if (device->drive == GDOX_OPTICAL_DRIVE_GP08) {
        return gdox_gp08_source_open(open_selected_transport, device,
            read_retries, ready_timeout_ms, source, error);
    }
    if (device->drive == GDOX_OPTICAL_DRIVE_ASUS_NR09) {
        if (!gdox_asus_nr09_detected_source_open(open_selected_transport,
                device, read_retries, ready_timeout_ms, source,
                &asus_media, error)) {
            return false;
        }
        if (asus_media == GDOX_ASUS_NR09_MEDIA_XGD2) {
            info->profile = GDOX_OPTICAL_MEDIA_XGD2;
            info->game_partition_lba = GDOX_XGD2_GAME_PARTITION_LBA;
        } else if (asus_media != GDOX_ASUS_NR09_MEDIA_XGD1) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL,
                "selected ASUS media profile has no optical mapping");
            return false;
        }
        return true;
    }
    if (device->drive == GDOX_OPTICAL_DRIVE_GP65
        || device->drive == GDOX_OPTICAL_DRIVE_SP80) {
        return gdox_mt1887_source_open(open_selected_transport, device,
            identity, UINT16_C(0xffff), read_retries, ready_timeout_ms,
            source, error);
    }
    if (!gdox_mt1887_detected_source_open_for_identity(
            open_selected_transport, device, identity,
            device->drive == GDOX_OPTICAL_DRIVE_GP63
                ? UINT16_C(0xffff) : 0U,
            read_retries, ready_timeout_ms, source, &mt_media, error)) {
        return false;
    }
    switch (mt_media->kind) {
        case GDOX_MT1887_MEDIA_XGD1:
            break;
        case GDOX_MT1887_MEDIA_GP63_XGD2:
            info->profile = GDOX_OPTICAL_MEDIA_XGD2;
            info->game_partition_lba = mt_media->game_partition_lba;
            break;
        case GDOX_MT1887_MEDIA_GP63_XGD3:
            info->profile = GDOX_OPTICAL_MEDIA_XGD3;
            info->game_partition_lba = mt_media->game_partition_lba;
            break;
        default:
            gdox_error_set(error, GDOX_ERROR_INTERNAL,
                "selected media profile has no optical mapping");
            return false;
    }
    return true;
}

bool gdox_optical_open_device_media(
    const gdox_optical_device *device,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_optical_media_info *info,
    gdox_error *error
)
{
    gdox_optical_device selected;
    gdox_error_clear(error);
    if (!valid_device(device, error)) {
        return false;
    }
    if (source == NULL || gdox_source_is_valid(source) || info == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "an empty source and optical media output are required");
        return false;
    }
    selected = *device;
    *info = (gdox_optical_media_info){0};
    info->profile = GDOX_OPTICAL_MEDIA_XGD1;
    info->sequential_read_blocks =
        gdox_optical_sequential_read_blocks(device->drive);
    /* On failure a valid source retains cleanup ownership for this device. */
    return open_selected_media(&selected, read_retries, ready_timeout_ms,
        source, info, error);
}

bool gdox_optical_eject_device(
    const gdox_optical_device *device,
    gdox_error *error
)
{
    gdox_optical_device selected;
    gdox_error_clear(error);
    if (!valid_device(device, error)) {
        return false;
    }
    if (!gdox_optical_drive_can_eject(device->drive)) {
        gdox_error_set(error, GDOX_ERROR_UNSUPPORTED,
            "operate the selected drive's tray manually");
        return false;
    }
    selected = *device;
    if (device->drive == GDOX_OPTICAL_DRIVE_GP08) {
        return gdox_gp08_source_eject(open_selected_transport, &selected, error);
    }
    return gdox_mt1887_source_eject(open_selected_transport, &selected,
        gdox_optical_identity_for_drive(device->drive), error);
}

bool gdox_optical_complete_device_eject_request(
    const gdox_optical_device *device,
    gdox_optical_eject_completion *completion,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (completion != NULL) {
        *completion = GDOX_OPTICAL_EJECT_COMPLETION_NONE;
    }
    if (!valid_device(device, error)) {
        return false;
    }
    if (completion == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT,
            "an eject completion output is required");
        return false;
    }
    if (!gdox_optical_drive_can_eject(device->drive)) {
        *completion = GDOX_OPTICAL_EJECT_COMPLETION_RELEASED_FOR_MANUAL_EJECT;
        return true;
    }
    if (!gdox_optical_eject_device(device, error)) {
        return false;
    }
    *completion = GDOX_OPTICAL_EJECT_COMPLETION_TRAY_EJECTED;
    return true;
}
