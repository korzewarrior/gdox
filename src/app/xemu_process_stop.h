#ifndef GDOX_APP_XEMU_PROCESS_STOP_H
#define GDOX_APP_XEMU_PROCESS_STOP_H

#include "gdox/emulator.h"
#include "gdox/error.h"

#include <stdbool.h>
#include <stdint.h>

#define GDOX_XEMU_ORDERLY_STOP_GRACE_MS UINT32_C(15000)

bool gdox_xemu_process_stop_orderly(
    gdox_emulator_process **process,
    gdox_error *error
);

#endif
