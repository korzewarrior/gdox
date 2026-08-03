#include "gdox/android_disc.h"

#include "gdox/disc.h"
#include "gdox/live.h"
#include "gdox/optical.h"
#include "platform/mt1887_source.h"
#include "platform/scsi_transport.h"
#include "platform/usb_bot.h"

#include <stdlib.h>
#include <string.h>

#define GDOX_ANDROID_READ_SPEED_KBPS UINT16_C(2770)

struct gdox_android_disc {
    gdox_random_disc live;
};

struct gdox_android_drive_monitor {
    gdox_scsi_transport transport;
};

static bool open_android_transport(
    void *context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    const int file_descriptor = *(const int *)context;

    return gdox_usb_bot_open_file_descriptor(
        file_descriptor,
        GDOX_GP63_USB_VENDOR_ID,
        GDOX_GP63_USB_PRODUCT_ID,
        transport,
        error
    );
}

static bool open_android_source(
    int file_descriptor,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_mt1887_source_open(
        open_android_transport,
        &file_descriptor,
        GDOX_USB_BOT_GP63,
        GDOX_ANDROID_READ_SPEED_KBPS,
        read_retries,
        ready_timeout_ms,
        source,
        error
    );
}

static bool monitor_test_unit_ready(
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    static const uint8_t command[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    return gdox_scsi_command_none(
        transport,
        "TEST UNIT READY",
        command,
        sizeof(command),
        UINT32_C(5000),
        error
    );
}

static bool monitor_request_sense(
    gdox_scsi_transport *transport,
    uint8_t output[18],
    size_t *transferred,
    gdox_error *error
)
{
    static const uint8_t command[6] = {0x03U, 0U, 0U, 0U, 18U, 0U};
    return gdox_scsi_command_in(
        transport,
        "REQUEST SENSE",
        command,
        sizeof(command),
        output,
        18U,
        UINT32_C(5000),
        transferred,
        error
    );
}

static bool monitor_validate_identity(
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    static const uint8_t command[6] = {0x12U, 0U, 0U, 0U, 96U, 0U};
    static const uint8_t vendor[8] = {
        'H', 'L', '-', 'D', 'T', '-', 'S', 'T'
    };
    static const uint8_t model[16] = {
        'D', 'V', 'D', 'R', 'A', 'M', ' ', 'G',
        'P', '6', '3', 'E', 'X', '7', '0', ' '
    };
    static const uint8_t revision[4] = {'R', 'F', '0', '2'};
    uint8_t response[96];
    size_t transferred;
    unsigned int attempt;

    for (attempt = 0U; attempt < 2U; ++attempt) {
        if (gdox_scsi_command_in(
                transport,
                "INQUIRY",
                command,
                sizeof(command),
                response,
                sizeof(response),
                attempt == 0U ? UINT32_C(1200) : UINT32_C(5000),
                &transferred,
                error
            )) {
            break;
        }
    }
    if (attempt == 2U) {
        return false;
    }
    if (transferred < 36U
        || memcmp(response + 8U, vendor, sizeof(vendor)) != 0
        || memcmp(response + 16U, model, sizeof(model)) != 0
        || memcmp(response + 32U, revision, sizeof(revision)) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "USB device is not the validated HL-DT-ST GP63EX70 RF02 mechanism"
        );
        return false;
    }
    return true;
}

static bool decode_sense(
    const uint8_t *sense,
    size_t bytes,
    uint8_t *key,
    uint8_t *additional,
    uint8_t *qualifier
)
{
    const uint8_t format = bytes != 0U ? sense[0] & 0x7fU : 0U;

    if ((format == 0x70U || format == 0x71U) && bytes >= 14U) {
        *key = sense[2] & 0x0fU;
        *additional = sense[12];
        *qualifier = sense[13];
        return true;
    }
    if ((format == 0x72U || format == 0x73U) && bytes >= 4U) {
        *key = sense[1] & 0x0fU;
        *additional = sense[2];
        *qualifier = sense[3];
        return true;
    }
    return false;
}

bool gdox_android_drive_monitor_open(
    int file_descriptor,
    gdox_android_drive_monitor **output,
    gdox_error *error
)
{
    gdox_android_drive_monitor *monitor;

    gdox_error_clear(error);
    if (file_descriptor < 0 || output == NULL || *output != NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an Android USB file descriptor and empty monitor output are required"
        );
        return false;
    }
    monitor = calloc(1U, sizeof(*monitor));
    if (monitor == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Android drive monitor"
        );
        return false;
    }
    if (!gdox_usb_bot_open_observer_file_descriptor(
            file_descriptor,
            GDOX_GP63_USB_VENDOR_ID,
            GDOX_GP63_USB_PRODUCT_ID,
            &monitor->transport,
            error
        )) {
        free(monitor);
        return false;
    }
    if (!monitor_validate_identity(&monitor->transport, error)) {
        gdox_error ignored;

        (void)gdox_scsi_transport_close(&monitor->transport, &ignored);
        free(monitor);
        return false;
    }
    *output = monitor;
    return true;
}

bool gdox_android_drive_monitor_poll(
    gdox_android_drive_monitor *monitor,
    gdox_android_media_state *state,
    gdox_error *error
)
{
    gdox_error ready_error;
    uint8_t sense[18];
    size_t transferred;
    uint8_t key;
    uint8_t additional;
    uint8_t qualifier;

    gdox_error_clear(error);
    if (monitor == NULL || state == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open Android drive monitor and media-state output are required"
        );
        return false;
    }
    if (monitor_test_unit_ready(&monitor->transport, &ready_error)) {
        *state = GDOX_ANDROID_MEDIA_READY;
        return true;
    }
    if (!monitor_request_sense(
            &monitor->transport,
            sense,
            &transferred,
            error
        )
        || !decode_sense(
            sense,
            transferred,
            &key,
            &additional,
            &qualifier
        )) {
        if (!gdox_error_is_set(error)) {
            *error = ready_error;
        }
        return false;
    }
    (void)qualifier;
    if (additional == 0x3aU) {
        *state = GDOX_ANDROID_MEDIA_EMPTY;
        return true;
    }
    if (key == 0x02U || key == 0x06U || key == 0x0bU) {
        *state = GDOX_ANDROID_MEDIA_CHANGING;
        return true;
    }
    *error = ready_error;
    return false;
}

bool gdox_android_drive_monitor_eject(
    gdox_android_drive_monitor *monitor,
    gdox_error *error
)
{
    static const uint8_t allow[6] = {0x1eU, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t eject[6] = {0x1bU, 0U, 0U, 0U, 0x02U, 0U};
    gdox_error ignored;

    gdox_error_clear(error);
    if (monitor == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an open Android drive monitor is required"
        );
        return false;
    }
    (void)gdox_scsi_command_none(
        &monitor->transport,
        "PREVENT ALLOW MEDIUM REMOVAL (allow)",
        allow,
        sizeof(allow),
        UINT32_C(5000),
        &ignored
    );
    return gdox_scsi_command_none(
        &monitor->transport,
        "START STOP UNIT (eject)",
        eject,
        sizeof(eject),
        UINT32_C(30000),
        error
    );
}

bool gdox_android_drive_monitor_close(
    gdox_android_drive_monitor *monitor,
    gdox_error *error
)
{
    bool closed;

    gdox_error_clear(error);
    if (monitor == NULL) {
        return true;
    }
    closed = gdox_scsi_transport_close(&monitor->transport, error);
    free(monitor);
    return closed;
}

bool gdox_android_drive_monitor_handoff(
    gdox_android_drive_monitor *monitor,
    gdox_error *error
)
{
    gdox_error handoff_error;
    bool prepared;
    bool closed;

    gdox_error_clear(error);
    if (monitor == NULL) {
        return true;
    }
    prepared = gdox_usb_bot_prepare_handoff(
        &monitor->transport,
        &handoff_error
    );
    closed = gdox_scsi_transport_close(&monitor->transport, error);
    free(monitor);
    if (!prepared) {
        *error = handoff_error;
        return false;
    }
    return closed;
}

bool gdox_android_disc_identify(
    int file_descriptor,
    gdox_android_disc_info *info,
    gdox_error *error
)
{
    gdox_sector_source source = {0};
    gdox_live_disc_info live_info;
    const gdox_mt1887_media_profile *media = NULL;
    gdox_error close_error;
    bool identified;

    gdox_error_clear(error);
    if (file_descriptor < 0 || info == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an Android USB file descriptor and disc information are required"
        );
        return false;
    }
    memset(info, 0, sizeof(*info));
    identified = gdox_mt1887_detected_source_open(
        open_android_transport,
        &file_descriptor,
        GDOX_ANDROID_READ_SPEED_KBPS,
        3U,
        UINT32_C(20000),
        &source,
        &media,
        error
    );
    if (identified && media->kind == GDOX_MT1887_MEDIA_XGD1) {
        identified = gdox_live_disc_identify(&source, &live_info, error);
        if (identified) {
            info->platform = GDOX_ANDROID_DISC_XBOX;
            memcpy(info->title, live_info.title, sizeof(info->title));
            info->title_id_present = live_info.title_id_present;
            info->title_id = live_info.title_id;
        }
    } else if (identified
        && (media->kind == GDOX_MT1887_MEDIA_GP63_XGD2
            || media->kind == GDOX_MT1887_MEDIA_GP63_XGD3)) {
        info->platform = GDOX_ANDROID_DISC_XBOX_360;
    } else if (identified) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "detected Android disc has no supported platform identity"
        );
        identified = false;
    }
    if (gdox_source_is_valid(&source)
        && !gdox_source_close(&source, &close_error)) {
        *error = close_error;
        return false;
    }
    return identified;
}

bool gdox_android_disc_open(
    int file_descriptor,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_android_disc **output,
    gdox_android_disc_info *info,
    gdox_error *error
)
{
    gdox_android_disc *disc;
    gdox_sector_source source = {0};
    gdox_live_disc_info live_info;
    const gdox_live_disc_options live_options = {UINT32_C(128)};

    gdox_error_clear(error);
    if (file_descriptor < 0 || output == NULL || *output != NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an Android USB file descriptor and empty disc output are required"
        );
        return false;
    }
    disc = calloc(1U, sizeof(*disc));
    if (disc == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate Android live-disc session"
        );
        return false;
    }
    if (!open_android_source(
            file_descriptor,
            read_retries,
            ready_timeout_ms,
            &source,
            error
        )
        || !gdox_live_disc_build_configured(
            &source,
            &live_options,
            &disc->live,
            &live_info,
            error
        )) {
        gdox_source_destroy(&source);
        free(disc);
        return false;
    }
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->platform = GDOX_ANDROID_DISC_XBOX;
        memcpy(info->title, live_info.title, sizeof(info->title));
        info->title_id_present = live_info.title_id_present;
        info->title_id = live_info.title_id;
        info->input_sectors = live_info.input_sectors;
        info->output_sectors = live_info.output_sectors;
    }
    *output = disc;
    return true;
}

uint64_t gdox_android_disc_length(const gdox_android_disc *disc)
{
    return disc != NULL ? gdox_disc_length(&disc->live) : 0U;
}

bool gdox_android_disc_read_at(
    gdox_android_disc *disc,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (disc == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "disc is not open");
        return false;
    }
    return gdox_disc_read_at(
        &disc->live,
        offset,
        output,
        output_bytes,
        read_bytes,
        error
    );
}

bool gdox_android_disc_observe_media(
    const gdox_android_disc *disc,
    gdox_media_observation *output
)
{
    if (output == NULL) {
        return false;
    }
    *output = (gdox_media_observation){0};
    return disc != NULL && gdox_disc_observe_media(&disc->live, output);
}

bool gdox_android_disc_physical_read_stats(
    const gdox_android_disc *disc,
    gdox_physical_read_stats *output
)
{
    return disc != NULL
        && gdox_disc_physical_read_stats(&disc->live, output);
}

bool gdox_android_disc_close(gdox_android_disc *disc, gdox_error *error)
{
    bool closed;

    gdox_error_clear(error);
    if (disc == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "disc is not open");
        return false;
    }
    closed = gdox_disc_close(&disc->live, error);
    free(disc);
    return closed;
}
