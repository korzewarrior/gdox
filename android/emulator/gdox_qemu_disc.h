#ifndef GDOX_QEMU_DISC_H
#define GDOX_QEMU_DISC_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_QEMU_DISC_URL "gdox://physical-disc"
#define GDOX_QEMU_DISC_TITLE_CAPACITY 256U

typedef struct gdox_qemu_disc_info {
    char title[GDOX_QEMU_DISC_TITLE_CAPACITY];
    bool title_id_present;
    uint32_t title_id;
} gdox_qemu_disc_info;

/*
 * Prepares the single physical disc consumed when QEMU opens
 * GDOX_QEMU_DISC_URL. The Java UsbDeviceConnection must remain open until
 * QEMU has closed the block node.
 */
bool gdox_qemu_disc_prepare(
    int file_descriptor,
    gdox_qemu_disc_info *info,
    gdox_error *error
);

/*
 * Checks the staged or active physical medium. Safe to call from the Android
 * watchdog while QEMU is reading because the optical transport serializes
 * commands internally.
 */
bool gdox_qemu_disc_media_present(void);

/*
 * Closes any prepared or active disc after the QEMU thread has stopped.
 * This is idempotent.
 */
void gdox_qemu_disc_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
