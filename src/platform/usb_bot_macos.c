#include "platform/usb_bot.h"

#include "platform/portable_sync.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define GDOX_MACOS_ERROR_CAPACITY 512U
#define GDOX_MACOS_OPEN_MOUNT_BUSY 7
#define GDOX_MACOS_OPEN_ATTEMPTS 50U

typedef struct GdoxMacScsiDevice GdoxMacScsiDevice;

int gdox_macos_scsi_start_mount_guard(char *error, size_t error_capacity);
int gdox_macos_scsi_release_system_media(char *error, size_t error_capacity);
int gdox_macos_scsi_observe(int *drive_present, int *media_present);
int gdox_macos_scsi_open(
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
} gdox_macos_scsi_context;

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

static const gdox_scsi_transport_ops macos_ops = {
    macos_command_in,
    macos_command_none,
    macos_reset,
    macos_close,
};

static bool open_native_device(
    GdoxMacScsiDevice **device,
    gdox_error *error
)
{
    bool released_system_media = false;
    uint32_t attempt;

    for (attempt = 0U; attempt < GDOX_MACOS_OPEN_ATTEMPTS; ++attempt) {
        char detail[GDOX_MACOS_ERROR_CAPACITY] = {0};
        const int status = gdox_macos_scsi_open(
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
    uint16_t vendor_id,
    uint16_t product_id,
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
    if (vendor_id != UINT16_C(0x0e8d)
        || product_id != UINT16_C(0x1887)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "macOS transport only supports the validated GP63 optical mechanism"
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
    if (!open_native_device(&context->device, error)) {
        free(context);
        return false;
    }
    transport->context = context;
    transport->ops = &macos_ops;
    return true;
}

bool gdox_usb_bot_present(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    gdox_error *error
)
{
    int observed_drive = 0;
    int observed_media = 0;

    gdox_error_clear(error);
    if (drive_present == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical presence output is required"
        );
        return false;
    }
    *drive_present = false;
    if (vendor_id != UINT16_C(0x0e8d)
        || product_id != UINT16_C(0x1887)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "macOS observer only supports the validated GP63 optical mechanism"
        );
        return false;
    }
    (void)gdox_macos_scsi_observe(&observed_drive, &observed_media);
    *drive_present = observed_drive != 0;
    return true;
}

bool gdox_usb_bot_observe(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *drive_present,
    bool *media_status_known,
    bool *media_present,
    gdox_error *error
)
{
    int observed_drive = 0;
    int observed_media = 0;

    gdox_error_clear(error);
    if (drive_present == NULL || media_status_known == NULL
        || media_present == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "optical observation outputs are required"
        );
        return false;
    }
    *drive_present = false;
    *media_status_known = false;
    *media_present = false;
    if (vendor_id != UINT16_C(0x0e8d)
        || product_id != UINT16_C(0x1887)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "macOS observer only supports the validated GP63 optical mechanism"
        );
        return false;
    }
    (void)gdox_macos_scsi_observe(&observed_drive, &observed_media);
    *drive_present = observed_drive != 0;
    *media_status_known = *drive_present;
    *media_present = observed_media != 0;
    return true;
}

bool gdox_usb_bot_restore_kernel_driver(
    uint16_t vendor_id,
    uint16_t product_id,
    bool *reattached,
    gdox_error *error
)
{
    (void)vendor_id;
    (void)product_id;
    gdox_error_clear(error);
    if (reattached == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "reattachment output is required");
        return false;
    }
    *reattached = false;
    return true;
}
