#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    MMCDeviceInterface **mmc;
    SCSITaskDeviceInterface **scsi;
} GdoxMacScsiDevice;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int completed;
    int status;
    unsigned references;
} GdoxMacUnmountContext;

enum {
    kGdoxMacScsiOpenMountBusy = 7,
    kGdoxMacScsiOpenExclusive = 8,
    kGdoxMacScsiOpenNoMemory = 9,
};

enum {
    kGdoxMacUnmountTimeoutSeconds = 5,
};

static pthread_once_t mount_guard_once = PTHREAD_ONCE_INIT;
static DASessionRef mount_guard_session;
static dispatch_queue_t mount_guard_queue;

static void set_error(char *output, size_t capacity, const char *message)
{
    if (output != NULL && capacity > 0) {
        snprintf(output, capacity, "%s", message);
    }
}

static int dictionary_string_equals(
    CFDictionaryRef dictionary,
    CFStringRef key,
    CFStringRef expected)
{
    CFTypeRef value = CFDictionaryGetValue(dictionary, key);
    return value != NULL
        && CFGetTypeID(value) == CFStringGetTypeID()
        && CFStringCompare((CFStringRef)value, expected, 0) == kCFCompareEqualTo;
}

static int service_dictionary_string_equals(
    io_service_t service,
    CFStringRef dictionary_key,
    CFStringRef value_key,
    CFStringRef expected)
{
    CFTypeRef properties = IORegistryEntryCreateCFProperty(
        service,
        dictionary_key,
        kCFAllocatorDefault,
        0);
    if (properties == NULL || CFGetTypeID(properties) != CFDictionaryGetTypeID()) {
        if (properties != NULL) CFRelease(properties);
        return 0;
    }
    int matches = dictionary_string_equals(
        (CFDictionaryRef)properties,
        value_key,
        expected);
    CFRelease(properties);
    return matches;
}

static int service_ancestor_number_equals(
    io_service_t service,
    CFStringRef key,
    int expected)
{
    CFTypeRef value = IORegistryEntrySearchCFProperty(
        service,
        kIOServicePlane,
        key,
        kCFAllocatorDefault,
        kIORegistryIterateRecursively | kIORegistryIterateParents);
    if (value == NULL || CFGetTypeID(value) != CFNumberGetTypeID()) {
        if (value != NULL) CFRelease(value);
        return 0;
    }
    int actual = 0;
    CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &actual);
    CFRelease(value);
    return actual == expected;
}

static int service_is_supported_gp63(io_service_t service)
{
    return service_ancestor_number_equals(service, CFSTR("idVendor"), 0x0e8d)
        && service_ancestor_number_equals(service, CFSTR("idProduct"), 0x1887)
        && service_dictionary_string_equals(
               service,
               CFSTR("Device Characteristics"),
               CFSTR("Vendor Name"),
               CFSTR("HL-DT-ST"))
        && service_dictionary_string_equals(
               service,
               CFSTR("Device Characteristics"),
               CFSTR("Product Name"),
               CFSTR("DVDRAM GP63EX70"))
        && service_dictionary_string_equals(
               service,
               CFSTR("Device Characteristics"),
               CFSTR("Product Revision Level"),
               CFSTR("RF02"))
        && service_dictionary_string_equals(
               service,
               CFSTR("Protocol Characteristics"),
               CFSTR("Physical Interconnect"),
               CFSTR("USB"));
}

static io_service_t find_supported_gp63(void)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t result = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("IODVDServices"),
        &iterator);
    if (result != KERN_SUCCESS) return IO_OBJECT_NULL;

    io_service_t selected = IO_OBJECT_NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        if (service_is_supported_gp63(service)) {
            selected = service;
            break;
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return selected;
}

static io_service_t find_supported_gp63_media(void)
{
    io_service_t drive = find_supported_gp63();
    if (drive == IO_OBJECT_NULL) return IO_OBJECT_NULL;

    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t result = IORegistryEntryCreateIterator(
        drive,
        kIOServicePlane,
        kIORegistryIterateRecursively,
        &iterator);
    IOObjectRelease(drive);
    if (result != KERN_SUCCESS) return IO_OBJECT_NULL;

    io_service_t selected = IO_OBJECT_NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(service, "IOMedia")) {
            selected = service;
            break;
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return selected;
}

int gdox_macos_scsi_observe(int *drive_present, int *media_present)
{
    io_service_t drive;
    io_service_t media;

    if (drive_present == NULL || media_present == NULL) return 1;
    *drive_present = 0;
    *media_present = 0;
    drive = find_supported_gp63();
    if (drive == IO_OBJECT_NULL) return 0;
    *drive_present = 1;
    IOObjectRelease(drive);
    media = find_supported_gp63_media();
    if (media != IO_OBJECT_NULL) {
        *media_present = 1;
        IOObjectRelease(media);
    }
    return 0;
}

static int open_mmc(
    MMCDeviceInterface ***output,
    char *error,
    size_t error_capacity)
{
    io_service_t service = find_supported_gp63();
    if (service == IO_OBJECT_NULL) {
        set_error(error, error_capacity, "the supported GP63 optical service is not available");
        return 1;
    }

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    IOReturn result = IOCreatePlugInInterfaceForService(
        service,
        kIOMMCDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID,
        &plugin,
        &score);
    IOObjectRelease(service);
    if (result != kIOReturnSuccess || plugin == NULL) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "could not open the macOS MMC service (0x%08x)", result);
        }
        return 2;
    }

    MMCDeviceInterface **mmc = NULL;
    HRESULT query = (*plugin)->QueryInterface(
        plugin,
        CFUUIDGetUUIDBytes(kIOMMCDeviceInterfaceID),
        (LPVOID *)&mmc);
    (*plugin)->Release(plugin);
    if (query != S_OK || mmc == NULL) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "could not create the macOS MMC interface (0x%08x)", query);
        }
        return 3;
    }
    *output = mmc;
    return 0;
}

static int description_string_contains(
    CFDictionaryRef description,
    CFStringRef key,
    CFStringRef expected)
{
    CFTypeRef value = CFDictionaryGetValue(description, key);
    return value != NULL
        && CFGetTypeID(value) == CFStringGetTypeID()
        && CFStringFind(
               (CFStringRef)value,
               expected,
               kCFCompareCaseInsensitive).location != kCFNotFound;
}

static int disk_is_supported_gp63(DADiskRef disk)
{
    CFDictionaryRef description = DADiskCopyDescription(disk);
    if (description == NULL) return 0;
    int matches =
        description_string_contains(
            description,
            kDADiskDescriptionDeviceVendorKey,
            CFSTR("HL-DT-ST"))
        && description_string_contains(
            description,
            kDADiskDescriptionDeviceModelKey,
            CFSTR("GP63EX70"))
        && description_string_contains(
            description,
            kDADiskDescriptionDeviceProtocolKey,
            CFSTR("USB"));
    CFRelease(description);
    return matches;
}

static DADissenterRef prevent_gp63_mount(DADiskRef disk, void *context)
{
    (void)context;
    if (!disk_is_supported_gp63(disk)) return NULL;
    return DADissenterCreate(
        kCFAllocatorDefault,
        kDAReturnExclusiveAccess,
        CFSTR("GDOX is using this Xbox game disc"));
}

static void initialize_mount_guard(void)
{
    mount_guard_session = DASessionCreate(kCFAllocatorDefault);
    if (mount_guard_session == NULL) return;
    mount_guard_queue = dispatch_queue_create(
        "org.gdox.gdox.disk-arbitration",
        DISPATCH_QUEUE_SERIAL);
    if (mount_guard_queue == NULL) {
        CFRelease(mount_guard_session);
        mount_guard_session = NULL;
        return;
    }
    DARegisterDiskMountApprovalCallback(
        mount_guard_session,
        NULL,
        prevent_gp63_mount,
        NULL);
    DASessionSetDispatchQueue(mount_guard_session, mount_guard_queue);
}

int gdox_macos_scsi_start_mount_guard(char *error, size_t error_capacity)
{
    pthread_once(&mount_guard_once, initialize_mount_guard);
    if (mount_guard_session == NULL || mount_guard_queue == NULL) {
        set_error(error, error_capacity, "could not initialize macOS Disk Arbitration");
        return 1;
    }
    return 0;
}

static GdoxMacUnmountContext *unmount_context_create(void)
{
    GdoxMacUnmountContext *context = calloc(1, sizeof(*context));
    if (context == NULL) return NULL;
    if (pthread_mutex_init(&context->mutex, NULL) != 0) {
        free(context);
        return NULL;
    }
    if (pthread_cond_init(&context->condition, NULL) != 0) {
        pthread_mutex_destroy(&context->mutex);
        free(context);
        return NULL;
    }
    context->references = 2;
    return context;
}

static void unmount_context_release(GdoxMacUnmountContext *context)
{
    int destroy;
    pthread_mutex_lock(&context->mutex);
    context->references -= 1;
    destroy = context->references == 0;
    pthread_mutex_unlock(&context->mutex);
    if (destroy) {
        pthread_cond_destroy(&context->condition);
        pthread_mutex_destroy(&context->mutex);
        free(context);
    }
}

static void unmount_complete(
    DADiskRef disk,
    DADissenterRef dissenter,
    void *opaque_context)
{
    (void)disk;
    GdoxMacUnmountContext *context = opaque_context;
    pthread_mutex_lock(&context->mutex);
    context->status = dissenter == NULL ? 0 : DADissenterGetStatus(dissenter);
    context->completed = 1;
    pthread_cond_signal(&context->condition);
    pthread_mutex_unlock(&context->mutex);
    unmount_context_release(context);
}

int gdox_macos_scsi_release_system_media(char *error, size_t error_capacity)
{
    int guard = gdox_macos_scsi_start_mount_guard(error, error_capacity);
    if (guard != 0) return guard;

    io_service_t media = find_supported_gp63_media();
    if (media == IO_OBJECT_NULL) return 0;
    DADiskRef disk = DADiskCreateFromIOMedia(
        kCFAllocatorDefault,
        mount_guard_session,
        media);
    IOObjectRelease(media);
    if (disk == NULL) {
        set_error(error, error_capacity, "could not identify the published GP63 media");
        return 2;
    }
    DADiskRef whole_disk = DADiskCopyWholeDisk(disk);
    if (whole_disk == NULL) whole_disk = (DADiskRef)CFRetain(disk);

    GdoxMacUnmountContext *context = unmount_context_create();
    if (context == NULL) {
        CFRelease(whole_disk);
        CFRelease(disk);
        set_error(error, error_capacity, "could not allocate the macOS unmount operation");
        return 3;
    }
    DADiskUnmount(
        whole_disk,
        kDADiskUnmountOptionWhole,
        unmount_complete,
        context);
    CFRelease(whole_disk);
    CFRelease(disk);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += kGdoxMacUnmountTimeoutSeconds;
    pthread_mutex_lock(&context->mutex);
    int wait_result = 0;
    while (!context->completed && wait_result == 0) {
        wait_result = pthread_cond_timedwait(
            &context->condition,
            &context->mutex,
            &deadline);
    }
    int completed = context->completed;
    int status = context->status;
    pthread_mutex_unlock(&context->mutex);
    unmount_context_release(context);

    if (!completed) {
        set_error(error, error_capacity, "timed out while releasing macOS's published disc media");
        return 4;
    }
    if (status != 0) {
        if (error != NULL && error_capacity > 0) {
            snprintf(
                error,
                error_capacity,
                "macOS could not release the published disc media (0x%08x)",
                status);
        }
        return 5;
    }
    return 0;
}

int gdox_macos_scsi_inquiry(
    uint8_t *output,
    size_t output_length,
    char *error,
    size_t error_capacity)
{
    if (output == NULL || output_length < 36) {
        set_error(error, error_capacity, "the INQUIRY buffer is too small");
        return 1;
    }
    int guard = gdox_macos_scsi_start_mount_guard(error, error_capacity);
    if (guard != 0) return guard;
    MMCDeviceInterface **mmc = NULL;
    int opened = open_mmc(&mmc, error, error_capacity);
    if (opened != 0) return opened;

    SCSITaskStatus status = 0;
    SCSI_Sense_Data sense = {0};
    memset(output, 0, output_length);
    IOReturn result = (*mmc)->Inquiry(
        mmc,
        (SCSICmd_INQUIRY_StandardData *)output,
        36,
        &status,
        &sense);
    (*mmc)->Release(mmc);
    if (result != kIOReturnSuccess) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "macOS INQUIRY failed (0x%08x)", result);
        }
        return 4;
    }
    if (status != kSCSITaskStatus_GOOD) {
        if (error != NULL && error_capacity > 0) {
            snprintf(
                error,
                error_capacity,
                "INQUIRY returned SCSI status 0x%02x (%02x/%02x/%02x)",
                status,
                sense.SENSE_KEY & 0x0f,
                sense.ADDITIONAL_SENSE_CODE,
                sense.ADDITIONAL_SENSE_CODE_QUALIFIER);
        }
        return 5;
    }
    return 0;
}

int gdox_macos_scsi_media_present(
    int *present,
    char *error,
    size_t error_capacity)
{
    if (present == NULL) {
        set_error(error, error_capacity, "the media-status output is missing");
        return 1;
    }
    int guard = gdox_macos_scsi_start_mount_guard(error, error_capacity);
    if (guard != 0) return guard;
    MMCDeviceInterface **mmc = NULL;
    int opened = open_mmc(&mmc, error, error_capacity);
    if (opened != 0) return opened;

    SCSITaskStatus status = 0;
    SCSI_Sense_Data sense = {0};
    IOReturn result = (*mmc)->TestUnitReady(mmc, &status, &sense);
    (*mmc)->Release(mmc);
    if (result != kIOReturnSuccess) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "macOS TEST UNIT READY failed (0x%08x)", result);
        }
        return 4;
    }
    if (status == kSCSITaskStatus_GOOD) {
        *present = 1;
        return 0;
    }
    if ((sense.SENSE_KEY & 0x0f) == 0x02
        && sense.ADDITIONAL_SENSE_CODE == 0x3a) {
        *present = 0;
        return 0;
    }
    if ((sense.SENSE_KEY & 0x0f) == 0x02
        && sense.ADDITIONAL_SENSE_CODE == 0x04
        && sense.ADDITIONAL_SENSE_CODE_QUALIFIER == 0x01) {
        /*
         * The optical mechanism is becoming ready. This is an ordinary
         * transition after startup, insertion, or releasing macOS's media
         * session; the runtime will poll again.
         */
        *present = 0;
        return 0;
    }
    if (error != NULL && error_capacity > 0) {
        snprintf(
            error,
            error_capacity,
            "TEST UNIT READY returned SCSI status 0x%02x (%02x/%02x/%02x)",
            status,
            sense.SENSE_KEY & 0x0f,
            sense.ADDITIONAL_SENSE_CODE,
            sense.ADDITIONAL_SENSE_CODE_QUALIFIER);
    }
    return 5;
}

int gdox_macos_scsi_open(
    GdoxMacScsiDevice **output,
    char *error,
    size_t error_capacity)
{
    if (output == NULL) {
        set_error(error, error_capacity, "the native-device output is missing");
        return 1;
    }
    *output = NULL;
    int guard = gdox_macos_scsi_start_mount_guard(error, error_capacity);
    if (guard != 0) return guard;

    MMCDeviceInterface **mmc = NULL;
    int opened = open_mmc(&mmc, error, error_capacity);
    if (opened != 0) return opened;
    SCSITaskDeviceInterface **scsi = (*mmc)->GetSCSITaskDeviceInterface(mmc);
    if (scsi == NULL) {
        (*mmc)->Release(mmc);
        set_error(error, error_capacity, "macOS did not expose a SCSI task interface");
        return 6;
    }
    IOReturn result = (*scsi)->ObtainExclusiveAccess(scsi);
    if (result != kIOReturnSuccess) {
        (*mmc)->Release(mmc);
        if (error != NULL && error_capacity > 0) {
            if (result == kIOReturnBusy) {
                snprintf(
                    error,
                    error_capacity,
                    "macOS still owns the published disc session");
            } else if (result == kIOReturnExclusiveAccess) {
                snprintf(
                    error,
                    error_capacity,
                    "another optical session currently owns the macOS SCSI command channel");
            } else {
                snprintf(
                    error,
                    error_capacity,
                    "could not obtain exclusive macOS SCSI access (0x%08x)",
                    result);
            }
        }
        return result == kIOReturnBusy
            ? kGdoxMacScsiOpenMountBusy
            : kGdoxMacScsiOpenExclusive;
    }

    GdoxMacScsiDevice *device = calloc(1, sizeof(*device));
    if (device == NULL) {
        (*scsi)->ReleaseExclusiveAccess(scsi);
        (*mmc)->Release(mmc);
        set_error(error, error_capacity, "could not allocate the native SCSI session");
        return kGdoxMacScsiOpenNoMemory;
    }
    device->mmc = mmc;
    device->scsi = scsi;
    *output = device;
    return 0;
}

static int execute(
    GdoxMacScsiDevice *device,
    const uint8_t *cdb,
    size_t cdb_length,
    uint8_t *data,
    size_t data_length,
    uint32_t timeout_ms,
    uint8_t *sense_output,
    size_t sense_capacity,
    size_t *transferred,
    char *error,
    size_t error_capacity)
{
    if (device == NULL || cdb == NULL
        || (cdb_length != 6 && cdb_length != 10
            && cdb_length != 12 && cdb_length != 16)) {
        set_error(error, error_capacity, "invalid native SCSI command arguments");
        return 1;
    }
    if (data_length > (size_t)UINT32_MAX) {
        set_error(error, error_capacity, "native SCSI transfer exceeds the macOS API limit");
        return 1;
    }
    SCSITaskInterface **task = (*device->scsi)->CreateSCSITask(device->scsi);
    if (task == NULL) {
        set_error(error, error_capacity, "macOS could not allocate a SCSI task");
        return 2;
    }

    IOReturn result = (*task)->SetCommandDescriptorBlock(
        task,
        (UInt8 *)cdb,
        (UInt8)cdb_length);
    if (result == kIOReturnSuccess && data_length > 0) {
        SCSITaskSGElement range = {
            .address = (IOVirtualAddress)data,
            .length = (IOByteCount)data_length,
        };
        result = (*task)->SetScatterGatherEntries(
            task,
            &range,
            1,
            data_length,
            kSCSIDataTransfer_FromTargetToInitiator);
    }
    if (result == kIOReturnSuccess) {
        result = (*task)->SetTimeoutDuration(task, timeout_ms);
    }

    SCSI_Sense_Data sense = {0};
    SCSITaskStatus status = 0;
    UInt64 realized = 0;
    if (result == kIOReturnSuccess) {
        result = (*task)->ExecuteTaskSync(
            task,
            &sense,
            &status,
            &realized);
    }
    (*task)->Release(task);
    if (sense_output != NULL && sense_capacity > 0) {
        size_t sense_length = sizeof(sense);
        if (sense_length > sense_capacity) sense_length = sense_capacity;
        memcpy(sense_output, &sense, sense_length);
    }
    if (transferred != NULL) *transferred = (size_t)realized;
    if (result != kIOReturnSuccess) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "macOS SCSI task failed (0x%08x)", result);
        }
        return 3;
    }
    if (status != kSCSITaskStatus_GOOD) {
        if (error != NULL && error_capacity > 0) {
            snprintf(
                error,
                error_capacity,
                "SCSI status 0x%02x, sense %02x/%02x/%02x",
                status,
                sense.SENSE_KEY & 0x0f,
                sense.ADDITIONAL_SENSE_CODE,
                sense.ADDITIONAL_SENSE_CODE_QUALIFIER);
        }
        return 4;
    }
    return 0;
}

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
    size_t error_capacity)
{
    if (data == NULL || data_length == 0) {
        set_error(error, error_capacity, "the native SCSI data buffer is empty");
        return 1;
    }
    return execute(
        device,
        cdb,
        cdb_length,
        data,
        data_length,
        timeout_ms,
        sense,
        sense_capacity,
        transferred,
        error,
        error_capacity);
}

int gdox_macos_scsi_command_none(
    GdoxMacScsiDevice *device,
    const uint8_t *cdb,
    size_t cdb_length,
    uint32_t timeout_ms,
    uint8_t *sense,
    size_t sense_capacity,
    char *error,
    size_t error_capacity)
{
    size_t transferred = 0;
    return execute(
        device,
        cdb,
        cdb_length,
        NULL,
        0,
        timeout_ms,
        sense,
        sense_capacity,
        &transferred,
        error,
        error_capacity);
}

void gdox_macos_scsi_close(GdoxMacScsiDevice *device)
{
    if (device == NULL) return;
    if (device->scsi != NULL) {
        (*device->scsi)->ReleaseExclusiveAccess(device->scsi);
    }
    if (device->mmc != NULL) {
        (*device->mmc)->Release(device->mmc);
    }
    free(device);
}
