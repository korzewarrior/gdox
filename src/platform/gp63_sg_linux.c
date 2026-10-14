#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "platform/gp63_sg_linux.h"
#include "platform/usb_bot_linux_devices.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <scsi/sg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define GDOX_GP63_SG_PATH_BYTES 4096U
#define GDOX_GP63_SG_SENSE_BYTES 64U
#define GDOX_GP63_SG_DISPLACEMENT_BYTES UINT32_C(65536)
#define GDOX_GP63_SG_STREAM_BYTES UINT32_C(55728)
#define GDOX_GP63_SG_CACHE_BYTES UINT32_C(0x0c0000)

typedef struct gdox_gp63_sg_context {
    int descriptor;
    uint8_t sense[64];
    size_t sense_bytes;
    atomic_bool disconnected;
} gdox_gp63_sg_context;

static bool make_path(
    char output[GDOX_GP63_SG_PATH_BYTES],
    const char *format,
    const char *value
)
{
    const int result = snprintf(
        output,
        GDOX_GP63_SG_PATH_BYTES,
        format,
        value
    );
    return result >= 0 && (size_t)result < GDOX_GP63_SG_PATH_BYTES;
}

static bool read_trimmed(
    const char *path,
    char *output,
    size_t output_bytes
)
{
    size_t used = 0U;
    int descriptor;

    if (output_bytes < 2U) {
        return false;
    }
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return false;
    }
    while (used + 1U < output_bytes) {
        const ssize_t received = read(
            descriptor,
            output + used,
            output_bytes - used - 1U
        );
        if (received > 0) {
            used += (size_t)received;
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            (void)close(descriptor);
            return false;
        }
        break;
    }
    {
        uint8_t extra;
        ssize_t received;
        do {
            received = read(descriptor, &extra, sizeof(extra));
        } while (received < 0 && errno == EINTR);
        if (received != 0) {
            (void)close(descriptor);
            return false;
        }
    }
    if (close(descriptor) != 0) {
        return false;
    }
    output[used] = '\0';
    while (used > 0U
        && (output[used - 1U] == ' '
            || output[used - 1U] == '\t'
            || output[used - 1U] == '\r'
            || output[used - 1U] == '\n')) {
        output[--used] = '\0';
    }
    return used != 0U;
}

static bool sr_name_is_canonical(const char *name)
{
    size_t index;

    if (name == NULL || name[0] != 's' || name[1] != 'r'
        || name[2] < '0' || name[2] > '9') {
        return false;
    }
    for (index = 3U; name[index] != '\0'; ++index) {
        if (name[index] < '0' || name[index] > '9') {
            return false;
        }
    }
    return true;
}

static bool scsi_identity_matches(const char *name)
{
    char path[GDOX_GP63_SG_PATH_BYTES];
    char vendor[32];
    char model[32];
    char revision[32];
    char type[16];

    return make_path(
            path,
            "/sys/class/block/%s/device/vendor",
            name
        )
        && read_trimmed(path, vendor, sizeof(vendor))
        && make_path(
            path,
            "/sys/class/block/%s/device/model",
            name
        )
        && read_trimmed(path, model, sizeof(model))
        && make_path(
            path,
            "/sys/class/block/%s/device/rev",
            name
        )
        && read_trimmed(path, revision, sizeof(revision))
        && make_path(
            path,
            "/sys/class/block/%s/device/type",
            name
        )
        && read_trimmed(path, type, sizeof(type))
        && strcmp(vendor, "HL-DT-ST") == 0
        && strcmp(model, "DVDRAM GP63EX70") == 0
        && strcmp(revision, "RF02") == 0
        && strcmp(type, "5") == 0;
}

static bool usb_identity_matches(const char *name)
{
    char device_path[GDOX_GP63_SG_PATH_BYTES];
    char resolved[GDOX_GP63_SG_PATH_BYTES];
    char vendor_path[GDOX_GP63_SG_PATH_BYTES];
    char product_path[GDOX_GP63_SG_PATH_BYTES];

    if (!make_path(
            device_path,
            "/sys/class/block/%s/device",
            name
        )
        || realpath(device_path, resolved) == NULL) {
        return false;
    }
    for (;;) {
        char vendor[16];
        char product[16];
        char *slash;

        if (snprintf(
                vendor_path,
                sizeof(vendor_path),
                "%s/idVendor",
                resolved
            ) >= 0
            && strlen(resolved) + sizeof("/idVendor")
                <= sizeof(vendor_path)
            && snprintf(
                product_path,
                sizeof(product_path),
                "%s/idProduct",
                resolved
            ) >= 0
            && strlen(resolved) + sizeof("/idProduct")
                <= sizeof(product_path)
            && read_trimmed(vendor_path, vendor, sizeof(vendor))
            && read_trimmed(product_path, product, sizeof(product))) {
            return strcmp(vendor, "0e8d") == 0
                && strcmp(product, "1887") == 0;
        }
        slash = strrchr(resolved, '/');
        if (slash == NULL || slash == resolved) {
            break;
        }
        *slash = '\0';
    }
    return false;
}

static bool parse_device_number(
    const char *name,
    unsigned int *device_major,
    unsigned int *device_minor
)
{
    char path[GDOX_GP63_SG_PATH_BYTES];
    char value[64];
    char tail;

    return make_path(path, "/sys/class/block/%s/device/generic/dev", name)
        && read_trimmed(path, value, sizeof(value))
        && sscanf(
            value,
            "%u:%u%c",
            device_major,
            device_minor,
            &tail
        ) == 2;
}

static bool descriptor_matches_sysfs(
    int descriptor,
    const char *name,
    gdox_error *error
)
{
    char class_path[GDOX_GP63_SG_PATH_BYTES];
    char class_resolved[GDOX_GP63_SG_PATH_BYTES];
    char device_path[GDOX_GP63_SG_PATH_BYTES];
    char device_resolved[GDOX_GP63_SG_PATH_BYTES];
    struct stat status;
    unsigned int device_major;
    unsigned int device_minor;

    if (fstat(descriptor, &status) != 0
        || !S_ISCHR(status.st_mode)
        || !parse_device_number(name, &device_major, &device_minor)
        || major(status.st_rdev) != device_major
        || minor(status.st_rdev) != device_minor
        || !make_path(class_path, "/sys/class/block/%s/device/generic", name)
        || realpath(class_path, class_resolved) == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Linux optical descriptor did not match the selected GP63 sysfs device"
        );
        return false;
    }
    {
        const int result = snprintf(
            device_path,
            sizeof(device_path),
            "/sys/dev/char/%u:%u",
            device_major,
            device_minor
        );
        if (result < 0 || (size_t)result >= sizeof(device_path)
            || realpath(device_path, device_resolved) == NULL
            || strcmp(class_resolved, device_resolved) != 0) {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "Linux optical descriptor lost its canonical GP63 sysfs binding"
            );
            return false;
        }
    }
    if (!scsi_identity_matches(name) || !usb_identity_matches(name)) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "Linux optical identity changed while opening the GP63"
        );
        return false;
    }
    return true;
}

bool gdox_gp63_sg_linux_parse_deferred_sense(
    const uint8_t *sense,
    size_t sense_bytes,
    uint8_t *key,
    uint8_t *asc,
    uint8_t *ascq
)
{
    static const uint8_t expected[18] = {
        0x71U, 0U, 0x02U, 0U, 0U, 0U, 0U, 0x0aU,
        0U, 0U, 0U, 0U, 0x30U, 0U, 0U, 0U, 0U, 0U,
    };

    if (sense != NULL
        && key != NULL
        && asc != NULL
        && ascq != NULL
        && sense_bytes == sizeof(expected)
        && (memcmp(sense, expected, sizeof(expected)) == 0
            || memcmp(sense, (uint8_t[18]){
                0x71U, 0U, 3U, 0U, 0U, 0U, 0U, 10U,
                0U, 0U, 0U, 0U, 0x11U, 0U, 0U, 0U, 0U, 0U}, 18U) == 0)) {
        *key = sense[2];
        *asc = sense[12];
        *ascq = 0x00U;
        return true;
    }
    return false;
}

static uint32_t read_be_u24(const uint8_t *input)
{
    return (uint32_t)input[0] << 16U
        | (uint32_t)input[1] << 8U
        | (uint32_t)input[2];
}

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

bool gdox_gp63_sg_linux_command_allowed(
    const uint8_t *cdb,
    size_t cdb_bytes,
    bool data_in,
    size_t data_bytes,
    uint32_t timeout_ms
)
{
    static const uint8_t inquiry[6] = {
        0x12U, 0U, 0U, 0U, 36U, 0U,
    };
    static const uint8_t method4_start[12] = {
        0x1bU, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };

    if (cdb == NULL) {
        return false;
    }
    if (cdb_bytes == 6U && memcmp(cdb, (uint8_t[6]){0}, 6U) == 0) {
        return !data_in && data_bytes == 0U && timeout_ms == UINT32_C(10000);
    }
    if (cdb_bytes == 10U && cdb[0] == 0xf1U
        && cdb[2] == 0U && cdb[3] == 0U && cdb[7] == 0U && cdb[8] == 0U
        && timeout_ms == UINT32_C(10000)) {
        const bool ready = cdb[4] == 3U && cdb[5] == 0x7eU;
        const bool capacity = cdb[4] == 0x85U && cdb[5] >= 0x38U && cdb[5] <= 0x3aU;
        if (!ready && !capacity) {
            return false;
        }
        if (cdb[1] == 2U) {
            return data_in && data_bytes == 4U && cdb[6] == 1U && cdb[9] == 0U;
        }
        if (cdb[1] == 1U) {
            return !data_in && data_bytes == 0U && cdb[6] == 0U
                && (!ready || cdb[9] <= 1U)
                && (cdb[5] != 0x38U || cdb[9] <= 0x26U);
        }
        return false;
    }
    if (!data_in) {
        if (cdb_bytes == 10U && cdb[0] == 0x2bU && cdb[1] == 0U
            && memcmp(cdb + 6U, (uint8_t[4]){0}, 4U) == 0) {
            const uint32_t lba = read_be_u32(cdb + 2U);
            return lba < GDOX_WII_SL_TOTAL_SECTORS && lba % 16U == 0U
                && data_bytes == 0U && timeout_ms == UINT32_C(30000);
        }
        return cdb != NULL
            && cdb_bytes == sizeof(method4_start)
            && memcmp(cdb, method4_start, sizeof(method4_start)) == 0
            && data_bytes == 0U
            && timeout_ms == UINT32_C(100000);
    }
    if (cdb == NULL) {
        return false;
    }
    if (cdb_bytes == sizeof(inquiry)
        && memcmp(cdb, inquiry, sizeof(inquiry)) == 0) {
        return data_bytes == 36U && timeout_ms == UINT32_C(10000);
    }
    if (cdb_bytes == 10U && memcmp(cdb,
            (uint8_t[10]){0x46U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 8U, 0U}, 10U) == 0) {
        return data_bytes == 8U && timeout_ms == UINT32_C(10000);
    }
    if (cdb_bytes == 12U && memcmp(cdb,
            (uint8_t[12]){0xf3U, 0x0eU, 0U, 0U, 0U, 0U, 0U, 0U, 0x10U, 0U, 0U, 0U},
            12U) == 0) {
        return data_bytes == 16U && timeout_ms == UINT32_C(30000);
    }
    if (cdb_bytes == 12U && cdb[0] == 0xa8U && cdb[1] == 0U
        && read_be_u32(cdb + 6U) == 1U && cdb[10] == 0x80U && cdb[11] == 0U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        return lba < GDOX_WII_SL_TOTAL_SECTORS && lba % 16U == 0U
            && data_bytes == 2048U && timeout_ms == UINT32_C(30000);
    }
    if (cdb_bytes == 12U
        && cdb[0] == 0xa8U
        && cdb[1] == 0x08U
        && read_be_u32(cdb + 6U) == UINT32_C(16)
        && cdb[10] == 0U
        && cdb[11] == 0U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        return lba == UINT32_C(0x03e0)
            && data_bytes == GDOX_GP63_SG_DISPLACEMENT_BYTES
            && timeout_ms == UINT32_C(100000);
    }
    if (cdb_bytes == 12U
        && cdb[0] == 0xa8U
        && cdb[1] == 0U
        && read_be_u32(cdb + 6U) == UINT32_C(0x1b)
        && cdb[10] == 0x80U
        && cdb[11] == 0U) {
        const uint32_t lba = read_be_u32(cdb + 2U);
        return lba == 0U
            && data_bytes == GDOX_GP63_SG_STREAM_BYTES
            && timeout_ms == UINT32_C(100000);
    }
    if (cdb_bytes == 12U
        && cdb[0] == 0x3cU
        && cdb[1] == 0x02U
        && cdb[2] == 0U
        && read_be_u24(cdb + 3U) == 0U
        && read_be_u24(cdb + 6U) == GDOX_GP63_SG_STREAM_BYTES
        && cdb[9] == 0U
        && cdb[10] == 0U
        && cdb[11] == 0U) {
        return data_bytes == GDOX_GP63_SG_STREAM_BYTES
            && timeout_ms == UINT32_C(30000);
    }
    if (cdb_bytes == 12U
        && cdb[0] == 0x3cU
        && cdb[1] == 0x02U
        && cdb[2] == 0U
        && cdb[9] == 0U
        && cdb[10] == 0U
        && cdb[11] == 0U) {
        const uint32_t address = read_be_u24(cdb + 3U);
        const uint32_t allocation = read_be_u24(cdb + 6U);

        return timeout_ms == UINT32_C(100000)
            && data_bytes == allocation
            && (
                address % UINT32_C(0x010000) == 0U
                && allocation == UINT32_C(0x010000)
                && (uint64_t)address + allocation
                    <= GDOX_GP63_SG_CACHE_BYTES
            );
    }
    return false;
}

static void set_completion_error(
    gdox_error *error,
    const char *name,
    const sg_io_hdr_t *request
)
{
    char message[GDOX_ERROR_MESSAGE_CAPACITY];

    (void)snprintf(
        message,
        sizeof(message),
        "%s failed SG_IO completion validation"
        " (status=%02x host=%04x driver=%04x info=%08x)",
        name,
        request->status,
        request->host_status,
        request->driver_status,
        request->info
    );
    gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
}

static bool submit(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    int direction,
    size_t *transferred,
    gdox_error *error
)
{
    gdox_gp63_sg_context *context = raw_context;
    uint8_t sense[GDOX_GP63_SG_SENSE_BYTES] = {0};
    sg_io_hdr_t request;
    int result;

    if (output_bytes > UINT32_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "SG_IO data-in transfer exceeds the Linux ABI limit"
        );
        return false;
    }
    context->sense_bytes = 0U;
    memset(&request, 0, sizeof(request));
    request.interface_id = 'S';
    request.dxfer_direction = direction;
    request.cmd_len = (unsigned char)cdb_bytes;
    request.mx_sb_len = (unsigned char)sizeof(sense);
    request.dxfer_len = (unsigned int)output_bytes;
    request.dxferp = output;
    request.cmdp = (unsigned char *)(uintptr_t)cdb;
    request.sbp = sense;
    request.timeout = timeout_ms;

    errno = 0;
    result = ioctl(context->descriptor, SG_IO, &request);
    if (result < 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        const int code = errno;
        if (code == ENODEV || code == ENXIO) {
            atomic_store(&context->disconnected, true);
        }
        (void)snprintf(
            message,
            sizeof(message),
            "%s SG_IO failed: %s",
            name,
            strerror(code)
        );
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, message);
        return false;
    }
    if (request.resid < 0
        || (uint64_t)(unsigned int)request.resid > output_bytes) {
        set_completion_error(error, name, &request);
        return false;
    }
    *transferred = output_bytes - (size_t)request.resid;
    if (request.status == 0U
        && request.masked_status == 0U
        && request.msg_status == 0U
        && request.host_status == 0U
        && request.driver_status == 0U
        && request.info == SG_INFO_OK
        && request.sb_len_wr == 0U) {
        gdox_error_clear(error);
        return true;
    }
    if (request.status == 0x02U
        && request.masked_status == 0x01U
        && request.msg_status == 0U
        && request.host_status == 0U
        && request.driver_status == 0x08U
        && request.info == SG_INFO_CHECK) {
        uint8_t key;
        uint8_t asc;
        uint8_t ascq;
        size_t sense_bytes = request.sb_len_wr;

        if (sense_bytes > sizeof(sense)) {
            sense_bytes = sizeof(sense);
        }
        if (gdox_gp63_sg_linux_parse_deferred_sense(
                sense,
                sense_bytes,
                &key,
                &asc,
                &ascq
            )) {
            memcpy(context->sense, sense, sense_bytes);
            context->sense_bytes = sense_bytes;
        }
        gdox_error_set(error, GDOX_ERROR_IO, name);
        return false;
    }
    set_completion_error(error, name, &request);
    return false;
}

static bool command_in(
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
    if (!gdox_gp63_sg_linux_command_allowed(
            cdb,
            cdb_bytes,
            true,
            output_bytes,
            timeout_ms
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "the GP63 Nintendo SG_IO transport rejected an unrecognized data-in command"
        );
        return false;
    }
    return submit(
        raw_context,
        name,
        cdb,
        cdb_bytes,
        output,
        output_bytes,
        timeout_ms,
        SG_DXFER_FROM_DEV,
        transferred,
        error
    );
}

static bool command_out(
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
    (void)raw_context;
    (void)name;
    (void)cdb;
    (void)cdb_bytes;
    (void)input;
    (void)input_bytes;
    (void)timeout_ms;
    *transferred = 0U;
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "the GP63 Nintendo SG_IO transport is read-only"
    );
    return false;
}

static bool command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    uint8_t sentinel = 0U;
    size_t transferred;

    if (!gdox_gp63_sg_linux_command_allowed(
            cdb,
            cdb_bytes,
            false,
            0U,
            timeout_ms
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "the GP63 Nintendo SG_IO transport rejected an unrecognized no-data command"
        );
        return false;
    }
    return submit(
        raw_context,
        name,
        cdb,
        cdb_bytes,
        &sentinel,
        0U,
        timeout_ms,
        SG_DXFER_NONE,
        &transferred,
        error
    );
}

static bool reset(void *raw_context, gdox_error *error)
{
    (void)raw_context;
    gdox_error_set(
        error,
        GDOX_ERROR_UNSUPPORTED,
        "the GP63 Nintendo SG_IO transport cannot reset the drive"
    );
    return false;
}

static bool close_transport(void *raw_context, gdox_error *error)
{
    gdox_gp63_sg_context *context = raw_context;
    const int result = close(context->descriptor);
    const int code = errno;

    free(context);
    if (result != 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        (void)snprintf(
            message,
            sizeof(message),
            "could not close the GP63 SG_IO descriptor: %s",
            strerror(code)
        );
        gdox_error_set(error, GDOX_ERROR_IO, message);
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static bool last_sense(const void *raw_context, uint8_t *output, size_t capacity, size_t *count)
{
    const gdox_gp63_sg_context *context = raw_context;
    if (context->sense_bytes == 0U || context->sense_bytes > capacity) { return false; }
    memcpy(output, context->sense, context->sense_bytes);
    *count = context->sense_bytes;
    return true;
}

static bool device_present(const void *raw_context, bool *present, gdox_error *error)
{
    const gdox_gp63_sg_context *context = raw_context;
    gdox_error_clear(error);
    *present = !atomic_load(&context->disconnected);
    return true;
}

static const gdox_scsi_transport_ops sg_ops = {
    command_in,
    command_out,
    command_none,
    reset,
    close_transport,
    NULL,
    last_sense,
    device_present,
};

bool gdox_gp63_sg_linux_open_device(
    const gdox_optical_device *device,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    char selected[NAME_MAX + 1U] = {0};
    DIR *directory;
    struct dirent *entry;
    unsigned int matches = 0U;
    int descriptor;
    gdox_gp63_sg_context *context;

    gdox_error_clear(error);
    if (transport == NULL || gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "an empty SCSI transport is required"
        );
        return false;
    }
    directory = opendir("/sys/class/block");
    if (directory == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not enumerate Linux optical block devices"
        );
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char identity[GDOX_OPTICAL_DEVICE_ID_CAPACITY];
        if (sr_name_is_canonical(entry->d_name)
            && scsi_identity_matches(entry->d_name)
            && usb_identity_matches(entry->d_name)
            && (device == NULL
                || (device->drive == GDOX_OPTICAL_DRIVE_GP63
                    && gdox_usb_bot_linux_block_device_id(entry->d_name, identity)
                    && strcmp(device->id, identity) == 0))) {
            ++matches;
            if (matches == 1U) {
                (void)snprintf(
                    selected,
                    sizeof(selected),
                    "%s",
                    entry->d_name
                );
            }
        }
    }
    if (closedir(directory) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not finish Linux optical device enumeration"
        );
        return false;
    }
    if (matches == 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "the exact HL-DT-ST GP63EX70 RF02 USB mechanism was not found"
        );
        return false;
    }
    if (matches != 1U) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "multiple exact GP63 RF02 mechanisms are present"
        );
        return false;
    }
    {
        char device_path[GDOX_GP63_SG_PATH_BYTES];
        char generic_path[GDOX_GP63_SG_PATH_BYTES];
        char resolved[GDOX_GP63_SG_PATH_BYTES];
        const char *generic_name;
        if (!make_path(generic_path, "/sys/class/block/%s/device/generic", selected)
            || realpath(generic_path, resolved) == NULL
            || (generic_name = strrchr(resolved, '/')) == NULL
            || strncmp(++generic_name, "sg", 2U) != 0
            || generic_name[2] == '\0'
            || strspn(generic_name + 2U, "0123456789") != strlen(generic_name + 2U)
            || !make_path(device_path, "/dev/%s", generic_name)) {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "the selected GP63 has no canonical SCSI generic device"
            );
            return false;
        }
        descriptor = open(
            device_path,
            /* Use the matching SG character device for vendor commands;
             * block SG_IO filters them. The allowlist forbids flash writes. */
            O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW
        );
    }
    if (descriptor < 0) {
        char message[GDOX_ERROR_MESSAGE_CAPACITY];
        const int code = errno;
        (void)snprintf(
            message,
            sizeof(message),
            "could not open the exact GP63 optical device: %s",
            strerror(code)
        );
        gdox_error_set(error, GDOX_ERROR_IO, message);
        return false;
    }
    if (!descriptor_matches_sysfs(descriptor, selected, error)) {
        (void)close(descriptor);
        return false;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        (void)close(descriptor);
        gdox_error_set(error, GDOX_ERROR_IO, "the GP63 is already owned by another direct-disc reader");
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        (void)close(descriptor);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate the GP63 SG_IO transport"
        );
        return false;
    }
    context->descriptor = descriptor;
    atomic_init(&context->disconnected, false);
    transport->context = context;
    transport->ops = &sg_ops;
    return true;
}

bool gdox_gp63_sg_linux_open(gdox_scsi_transport *transport, gdox_error *error)
{
    return gdox_gp63_sg_linux_open_device(NULL, transport, error);
}
