#include "platform/usb_bot.h"
#include "platform/usb_bot_identity.h"

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

typedef enum {
    kGdoxMacDriveUnknown = 0,
    kGdoxMacDriveGp63,
    kGdoxMacDriveGp65,
    kGdoxMacDriveGp08,
    kGdoxMacDriveAsusNr09,
} GdoxMacDriveIdentity;

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

static int dictionary_string_copy(
    CFDictionaryRef dictionary,
    CFStringRef key,
    char *output,
    size_t output_capacity)
{
    CFTypeRef value = CFDictionaryGetValue(dictionary, key);
    return value != NULL
        && CFGetTypeID(value) == CFStringGetTypeID()
        && CFStringGetCString(
            (CFStringRef)value,
            output,
            (CFIndex)output_capacity,
            kCFStringEncodingUTF8);
}

static int service_dictionary_string_copy(
    io_service_t service,
    CFStringRef dictionary_key,
    CFStringRef value_key,
    char *output,
    size_t output_capacity)
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
    int copied = dictionary_string_copy(
        (CFDictionaryRef)properties,
        value_key,
        output,
        output_capacity);
    CFRelease(properties);
    return copied;
}

static int service_ancestor_number(
    io_service_t service,
    CFStringRef key,
    int *output)
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
    int copied = CFNumberGetValue(
        (CFNumberRef)value,
        kCFNumberIntType,
        output);
    CFRelease(value);
    return copied;
}

static GdoxMacDriveIdentity requested_identity(
    int requested)
{
    if (requested == GDOX_USB_BOT_GP63) {
        return kGdoxMacDriveGp63;
    }
    if (requested == GDOX_USB_BOT_GP65) {
        return kGdoxMacDriveGp65;
    }
    if (requested == GDOX_USB_BOT_GP08) {
        return kGdoxMacDriveGp08;
    }
    if (requested == GDOX_USB_BOT_ASUS_NR09) {
        return kGdoxMacDriveAsusNr09;
    }
    return kGdoxMacDriveUnknown;
}

static int usb_identity(
    GdoxMacDriveIdentity identity,
    gdox_usb_bot_identity *output)
{
    if (identity == kGdoxMacDriveGp63) {
        *output = GDOX_USB_BOT_GP63;
        return 1;
    }
    if (identity == kGdoxMacDriveGp65) {
        *output = GDOX_USB_BOT_GP65;
        return 1;
    }
    if (identity == kGdoxMacDriveGp08) {
        *output = GDOX_USB_BOT_GP08;
        return 1;
    }
    if (identity == kGdoxMacDriveAsusNr09) {
        *output = GDOX_USB_BOT_ASUS_NR09;
        return 1;
    }
    return 0;
}

static int service_supported_identity(
    io_service_t service,
    gdox_usb_bot_identity *identity)
{
    int vendor_id = 0;
    int product_id = 0;
    char vendor[32];
    char model[32];
    char revision[32];
    char interconnect[32];
    gdox_usb_bot_observed_identity observed;
    size_t identity_index;

    if (identity == NULL
        || !service_ancestor_number(
            service,
            CFSTR("idVendor"),
            &vendor_id)
        || !service_ancestor_number(
            service,
            CFSTR("idProduct"),
            &product_id)
        || vendor_id < 0 || vendor_id > UINT16_MAX
        || product_id < 0 || product_id > UINT16_MAX
        || !service_dictionary_string_copy(
            service,
            CFSTR("Device Characteristics"),
            CFSTR("Vendor Name"),
            vendor,
            sizeof(vendor))
        || !service_dictionary_string_copy(
            service,
            CFSTR("Device Characteristics"),
            CFSTR("Product Name"),
            model,
            sizeof(model))
        || !service_dictionary_string_copy(
            service,
            CFSTR("Device Characteristics"),
            CFSTR("Product Revision Level"),
            revision,
            sizeof(revision))
        || !service_dictionary_string_copy(
            service,
            CFSTR("Protocol Characteristics"),
            CFSTR("Physical Interconnect"),
            interconnect,
            sizeof(interconnect))) {
        return 0;
    }
    observed = (gdox_usb_bot_observed_identity){
        (uint16_t)vendor_id,
        (uint16_t)product_id,
        vendor,
        model,
        revision,
    };
    if (strcmp(interconnect, "USB") != 0) {
        return 0;
    }
    for (identity_index = 0U;
         identity_index < GDOX_USB_BOT_IDENTITY_COUNT;
         ++identity_index) {
        const gdox_usb_bot_identity candidate =
            (gdox_usb_bot_identity)identity_index;
        if (gdox_usb_bot_identity_matches(candidate, &observed)) {
            *identity = candidate;
            return 1;
        }
    }
    return 0;
}

static int service_is_supported_drive(
    io_service_t service,
    GdoxMacDriveIdentity identity)
{
    gdox_usb_bot_identity requested;
    gdox_usb_bot_identity observed;

    return usb_identity(identity, &requested)
        && service_supported_identity(service, &observed)
        && requested == observed;
}

static io_service_t find_supported_drive(GdoxMacDriveIdentity identity)
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
        if (service_is_supported_drive(service, identity)) {
            selected = service;
            break;
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return selected;
}

static io_service_t find_supported_media(GdoxMacDriveIdentity identity)
{
    io_service_t drive = find_supported_drive(identity);
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

static int service_has_media(io_service_t drive)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service;
    kern_return_t result = IORegistryEntryCreateIterator(
        drive,
        kIOServicePlane,
        kIORegistryIterateRecursively,
        &iterator);

    if (result != KERN_SUCCESS) return 0;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        const int is_media = IOObjectConformsTo(service, "IOMedia") != 0U;
        IOObjectRelease(service);
        if (is_media) {
            IOObjectRelease(iterator);
            return 1;
        }
    }
    IOObjectRelease(iterator);
    return 0;
}

int gdox_macos_scsi_observe_all(
    int drive_present[GDOX_USB_BOT_IDENTITY_COUNT],
    int media_present[GDOX_USB_BOT_IDENTITY_COUNT])
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service;
    size_t index;
    kern_return_t result;

    if (drive_present == NULL) return 1;
    for (index = 0U; index < GDOX_USB_BOT_IDENTITY_COUNT; ++index) {
        drive_present[index] = 0;
        if (media_present != NULL) media_present[index] = 0;
    }
    result = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("IODVDServices"),
        &iterator);
    if (result != KERN_SUCCESS) return 1;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        gdox_usb_bot_identity identity;
        if (service_supported_identity(service, &identity)) {
            const size_t identity_index = (size_t)identity;
            drive_present[identity_index] = 1;
            if (media_present != NULL) {
                media_present[identity_index] = service_has_media(service);
            }
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return 0;
}

static int open_mmc(
    GdoxMacDriveIdentity identity,
    MMCDeviceInterface ***output,
    char *error,
    size_t error_capacity)
{
    io_service_t service = find_supported_drive(identity);
    if (service == IO_OBJECT_NULL) {
        set_error(error, error_capacity, "the requested optical service is not available");
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

static io_service_t find_dvd_service_ancestor(io_service_t service)
{
    io_service_t current = service;

    if (IOObjectRetain(current) != KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }
    for (;;) {
        io_service_t parent = IO_OBJECT_NULL;
        if (IOObjectConformsTo(current, "IODVDServices")) {
            return current;
        }
        if (IORegistryEntryGetParentEntry(
                current,
                kIOServicePlane,
                &parent) != KERN_SUCCESS) {
            IOObjectRelease(current);
            return IO_OBJECT_NULL;
        }
        IOObjectRelease(current);
        current = parent;
    }
}

static int disk_is_supported_drive(DADiskRef disk)
{
    io_service_t media = DADiskCopyIOMedia(disk);
    io_service_t drive;
    gdox_usb_bot_identity identity;
    int matches;

    if (media == IO_OBJECT_NULL) return 0;
    drive = find_dvd_service_ancestor(media);
    IOObjectRelease(media);
    if (drive == IO_OBJECT_NULL) return 0;
    matches = service_supported_identity(drive, &identity);
    IOObjectRelease(drive);
    return matches;
}

static DADissenterRef prevent_supported_mount(DADiskRef disk, void *context)
{
    (void)context;
    if (!disk_is_supported_drive(disk)) return NULL;
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
        prevent_supported_mount,
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

int gdox_macos_scsi_release_system_media(
    int requested,
    char *error,
    size_t error_capacity)
{
    GdoxMacDriveIdentity identity =
        requested_identity(requested);
    if (identity == kGdoxMacDriveUnknown) {
        set_error(error, error_capacity, "the requested optical service is unsupported");
        return 1;
    }
    int guard = gdox_macos_scsi_start_mount_guard(error, error_capacity);
    if (guard != 0) return guard;

    io_service_t media = find_supported_media(identity);
    if (media == IO_OBJECT_NULL) return 0;
    DADiskRef disk = DADiskCreateFromIOMedia(
        kCFAllocatorDefault,
        mount_guard_session,
        media);
    IOObjectRelease(media);
    if (disk == NULL) {
        set_error(error, error_capacity, "could not identify the published optical media");
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

int gdox_macos_scsi_open(
    int requested,
    GdoxMacScsiDevice **output,
    char *error,
    size_t error_capacity)
{
    GdoxMacDriveIdentity identity =
        requested_identity(requested);
    if (identity == kGdoxMacDriveUnknown) {
        set_error(error, error_capacity, "the requested optical service is unsupported");
        return 1;
    }
    if (output == NULL) {
        set_error(error, error_capacity, "the native-device output is missing");
        return 1;
    }
    *output = NULL;
    int guard = gdox_macos_scsi_start_mount_guard(error, error_capacity);
    if (guard != 0) return guard;

    MMCDeviceInterface **mmc = NULL;
    int opened = open_mmc(identity, &mmc, error, error_capacity);
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
    int data_out,
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
            data_out
                ? kSCSIDataTransfer_FromInitiatorToTarget
                : kSCSIDataTransfer_FromTargetToInitiator);
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
        0,
        timeout_ms,
        sense,
        sense_capacity,
        transferred,
        error,
        error_capacity);
}

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
        (uint8_t *)data,
        data_length,
        1,
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
