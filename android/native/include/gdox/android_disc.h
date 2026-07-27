#ifndef GDOX_ANDROID_DISC_H
#define GDOX_ANDROID_DISC_H

#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_ANDROID_DISC_TITLE_CAPACITY 256U

typedef struct gdox_android_disc gdox_android_disc;
typedef struct gdox_android_drive_monitor gdox_android_drive_monitor;

typedef enum gdox_android_media_state {
    GDOX_ANDROID_MEDIA_EMPTY = 0,
    GDOX_ANDROID_MEDIA_READY = 1,
    GDOX_ANDROID_MEDIA_CHANGING = 2
} gdox_android_media_state;

typedef struct gdox_android_disc_info {
    char title[GDOX_ANDROID_DISC_TITLE_CAPACITY];
    bool title_id_present;
    uint32_t title_id;
    uint64_t input_sectors;
    uint64_t output_sectors;
} gdox_android_disc_info;

/*
 * Holds one passive USB transport open while the launcher observes tray and
 * media changes. Closing this monitor releases the interface without a USB
 * reset so ownership can pass directly to the emulator.
 */
bool gdox_android_drive_monitor_open(
    int file_descriptor,
    gdox_android_drive_monitor **output,
    gdox_error *error
);
bool gdox_android_drive_monitor_poll(
    gdox_android_drive_monitor *monitor,
    gdox_android_media_state *state,
    gdox_error *error
);
bool gdox_android_drive_monitor_eject(
    gdox_android_drive_monitor *monitor,
    gdox_error *error
);
bool gdox_android_drive_monitor_close(
    gdox_android_drive_monitor *monitor,
    gdox_error *error
);
bool gdox_android_drive_monitor_handoff(
    gdox_android_drive_monitor *monitor,
    gdox_error *error
);

/*
 * Identifies the inserted Xbox title, then restores the drive to stock mode.
 * The Java UsbDeviceConnection that owns `file_descriptor` remains open.
 */
bool gdox_android_disc_identify(
    int file_descriptor,
    gdox_android_disc_info *info,
    gdox_error *error
);

/*
 * Empties the Xbox HDD's transient X, Y, and Z cache metadata without
 * touching saved games or dashboard data.
 */
bool gdox_android_hdd_reset_cache(
    const char *path,
    bool *changed,
    gdox_error *error
);

/*
 * Opens a live emulator view over a UsbManager-authorized GP63 connection.
 *
 * `file_descriptor` remains owned by the Java UsbDeviceConnection. That
 * connection must stay open until gdox_android_disc_close() returns.
 * GDOX stores no game sectors on the Android device.
 */
bool gdox_android_disc_open(
    int file_descriptor,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    gdox_android_disc **output,
    gdox_android_disc_info *info,
    gdox_error *error
);

uint64_t gdox_android_disc_length(const gdox_android_disc *disc);
bool gdox_android_disc_read_at(
    gdox_android_disc *disc,
    uint64_t offset,
    uint8_t *output,
    size_t output_bytes,
    size_t *read_bytes,
    gdox_error *error
);
bool gdox_android_disc_media_present(const gdox_android_disc *disc);
bool gdox_android_disc_physical_read_stats(
    const gdox_android_disc *disc,
    gdox_physical_read_stats *output
);

/*
 * Call only after the emulator has drained outstanding reads. This restores
 * the drive's volatile SRAM state, releases USB, and destroys `disc`.
 */
bool gdox_android_disc_close(gdox_android_disc *disc, gdox_error *error);

#ifdef __cplusplus
}
#endif

#endif
