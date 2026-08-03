#include "platform/usb_bot.h"

#include "gdox/optical.h"
#include "platform/portable_sync.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_MACOS_ERROR_CAPACITY 512U
#define GDOX_MACOS_OPEN_MOUNT_BUSY 7
#define GDOX_MACOS_OPEN_ATTEMPTS 50U

typedef struct GdoxMacScsiDevice GdoxMacScsiDevice;

int gdox_macos_scsi_start_mount_guard(char *error, size_t error_capacity);
int gdox_macos_scsi_release_system_media(
    int identity,
    char *error,
    size_t error_capacity
);
int gdox_macos_scsi_observe_all(
    int drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    int media_present[GDOX_USB_BOT_IDENTITY_COUNT]
);
int gdox_macos_scsi_open(
    int identity,
    GdoxMacScsiDevice **output,
    char *error,
    size_t error_capacity
);
int gdox_macos_scsi_command_in(
    GdoxMacScsiDevice *device,
    const uint8_t *cdb,
    size_t cdb_length,
    uint8_t *data,
    size_t data_length,
    uint32_t timeout_ms,
    uint8_t *sense,
    size_t sense_capacity,
    size_t *transferred,
    char *error,
    size_t error_capacity
);
int gdox_macos_scsi_command_out(
    GdoxMacScsiDevice *device,
    const uint8_t *cdb,
    size_t cdb_length,
    const uint8_t *data,
    size_t data_length,
    uint32_t timeout_ms,
    uint8_t *sense,
    size_t sense_capacity,
    size_t *transferred,
    char *error,
    size_t error_capacity
);
int gdox_macos_scsi_command_none(
    GdoxMacScsiDevice *device,
    const uint8_t *cdb,
    size_t cdb_length,
    uint32_t timeout_ms,
    uint8_t *sense,
    size_t sense_capacity,
    char *error,
    size_t error_capacity
);
void gdox_macos_scsi_close(GdoxMacScsiDevice *device);

typedef struct gdox_macos_scsi_context {
    GdoxMacScsiDevice *device;
    uint8_t last_sense[18];
    size_t last_sense_bytes;
} gdox_macos_scsi_context;

static void cache_command_sense(
    gdox_macos_scsi_context *context,
    int status,
    const uint8_t sense[18]
)
{
    const uint8_t response_code = (uint8_t)(sense[0] & 0x7fU);

    context->last_sense_bytes = 0U;
    if (status != 0
        && (response_code == 0x70U || response_code == 0x71U
            || response_code == 0x72U || response_code == 0x73U)) {
        memcpy(context->last_sense, sense, sizeof(context->last_sense));
        context->last_sense_bytes = sizeof(context->last_sense);
    }
}

static void set_native_error(
    gdox_error *error,
    const char *operation,
    const char *detail
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    (void)snprintf(
        message,
        sizeof(message),
        "%s: %s",
        operation,
        detail[0] != '\0' ? detail : "macOS optical transport failed"
    );
    gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
}

static bool macos_command_in(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    gdox_macos_scsi_context *context = raw_context;
    uint8_t sense[18] = {0};
    char detail[GDOX_MACOS_ERROR_CAPACITY] = {0};
    const int status = gdox_macos_scsi_command_in(
        context->device,
        cdb,
        cdb_bytes,
        output,
        output_bytes,
        timeout_ms,
        sense,
        sizeof(sense),
        transferred,
        detail,
        sizeof(detail)
    );
    cache_command_sense(context, status, sense);
    if (status != 0) {
        set_native_error(error, name, detail);
        return false;
    }
    return true;
}

static bool macos_command_out(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    const uint8_t *input,
    size_t input_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
)
{
    gdox_macos_scsi_context *context = raw_context;
    uint8_t sense[18] = {0};
    char detail[GDOX_MACOS_ERROR_CAPACITY] = {0};
    const int status = gdox_macos_scsi_command_out(
        context->device,
        cdb,
        cdb_bytes,
        input,
        input_bytes,
        timeout_ms,
        sense,
        sizeof(sense),
        transferred,
        detail,
        sizeof(detail)
    );
    cache_command_sense(context, status, sense);
    if (status != 0) {
        set_native_error(error, name, detail);
        return false;
    }
    return true;
}

static bool macos_command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    gdox_macos_scsi_context *context = raw_context;
    uint8_t sense[18] = {0};
    char detail[GDOX_MACOS_ERROR_CAPACITY] = {0};
    const int status = gdox_macos_scsi_command_none(
        context->device,
        cdb,
        cdb_bytes,
        timeout_ms,
        sense,
        sizeof(sense),
        detail,
        sizeof(detail)
    );
    cache_command_sense(context, status, sense);
    if (status != 0) {
        set_native_error(error, name, detail);
        return false;
    }
    return true;
}

static bool macos_reset(void *raw_context, gdox_error *error)
{
    (void)raw_context;
    gdox_error_clear(error);
    /*
     * SCSITaskLib does not expose the USB Bulk-Only reset used on Linux.
     * The caller's bounded recovery sequence clears sense and restarts the
     * optical unit, which is the appropriate macOS recovery path.
     */
    return true;
}

static bool macos_close(void *raw_context, gdox_error *error)
{
    gdox_macos_scsi_context *context = raw_context;
    gdox_macos_scsi_close(context->device);
    free(context);
    gdox_error_clear(error);
    return true;
}

static bool macos_last_sense(
    const void *raw_context,
    uint8_t *output,
    size_t output_bytes,
    size_t *sense_bytes
)
{
    const gdox_macos_scsi_context *context = raw_context;
    const size_t copied = context->last_sense_bytes < output_bytes
        ? context->last_sense_bytes
        : output_bytes;

    if (copied == 0U) {
        *sense_bytes = 0U;
        return false;
    }
    memcpy(output, context->last_sense, copied);
    *sense_bytes = copied;
    return true;
}

static const gdox_scsi_transport_ops macos_ops = {
    macos_command_in,
    macos_command_out,
    macos_command_none,
    macos_reset,
    macos_close,
    NULL,
    macos_last_sense,
};

static bool supported_identity(gdox_usb_bot_identity identity)
{
    return identity == GDOX_USB_BOT_GP63
        || identity == GDOX_USB_BOT_GP65
        || identity == GDOX_USB_BOT_GP08
        || identity == GDOX_USB_BOT_ASUS_NR09;
}

static bool open_native_device(
    gdox_usb_bot_identity identity,
    GdoxMacScsiDevice **device,
    gdox_error *error
)
{
    bool released_system_media = false;
    uint32_t attempt;

    for (attempt = 0U; attempt < GDOX_MACOS_OPEN_ATTEMPTS; ++attempt) {
        char detail[GDOX_MACOS_ERROR_CAPACITY] = {0};
        const int status = gdox_macos_scsi_open(
            (int)identity,
            device,
            detail,
            sizeof(detail)
        );
        if (status == 0 && *device != NULL) {
            return true;
        }
        if (status == GDOX_MACOS_OPEN_MOUNT_BUSY && !released_system_media) {
            char release_detail[GDOX_MACOS_ERROR_CAPACITY] = {0};
            if (gdox_macos_scsi_release_system_media(
                    (int)identity,
                    release_detail,
                    sizeof(release_detail)
                ) != 0) {
                set_native_error(
                    error,
                    "release macOS optical media session",
                    release_detail
                );
                return false;
            }
            released_system_media = true;
        } else if (status != GDOX_MACOS_OPEN_MOUNT_BUSY
            || attempt + 1U == GDOX_MACOS_OPEN_ATTEMPTS) {
            set_native_error(
                error,
                "open the macOS optical command channel",
                detail
            );
            return false;
        }
        gdox_sleep_ms(UINT32_C(100));
    }
    gdox_error_set(
        error,
        GDOX_ERROR_TRANSPORT,
        "macOS optical command channel did not become available"
    );
    return false;
}

bool gdox_usb_bot_open(
    gdox_usb_bot_identity identity,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_macos_scsi_context *context;
    char detail[GDOX_MACOS_ERROR_CAPACITY] = {0};

    gdox_error_clear(error);
    if (transport == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "transport output is required");
        return false;
    }
    transport->context = NULL;
    transport->ops = NULL;
    if (!supported_identity(identity)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "macOS transport does not support this USB optical mechanism"
        );
        return false;
    }
    if (gdox_macos_scsi_start_mount_guard(detail, sizeof(detail)) != 0) {
        set_native_error(error, "start the macOS optical mount guard", detail);
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate macOS optical transport");
        return false;
    }
    if (!open_native_device(
            identity,
            &context->device,
            error
        )) {
        free(context);
        return false;
    }
    transport->context = context;
    transport->ops = &macos_ops;
    return true;
}

bool gdox_usb_bot_observe_all(
    gdox_usb_bot_observation observations[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    int observed_drives[GDOX_USB_BOT_IDENTITY_COUNT];
    int observed_media[GDOX_USB_BOT_IDENTITY_COUNT];
    size_t index;

    gdox_error_clear(error);
    if (observations == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical observations output is required"
        );
        return false;
    }
    if (gdox_macos_scsi_observe_all(
            observed_drives,
            observed_media
        ) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "could not enumerate macOS optical drives"
        );
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        observations[index].drive_present = observed_drives[index] != 0;
        observations[index].media_status_known =
            observations[index].drive_present;
        observations[index].media_present = observed_media[index] != 0;
    }
    return true;
}

bool gdox_usb_bot_present_all(
    bool drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    gdox_error *error
)
{
    int observed_drives[GDOX_USB_BOT_IDENTITY_COUNT];
    size_t index;

    gdox_error_clear(error);
    if (drive_present == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    if (gdox_macos_scsi_observe_all(observed_drives, NULL) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "could not enumerate macOS optical drives"
        );
        return false;
    }
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        drive_present[index] = observed_drives[index] != 0;
    }
    return true;
}
