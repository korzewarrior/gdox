#ifndef GDOX_APP_XENIA_PROCESS_STOP_H
#define GDOX_APP_XENIA_PROCESS_STOP_H

#include "gdox/xenia.h"

#define GDOX_XENIA_ORDERLY_STOP_GRACE_MS UINT32_C(15000)

bool gdox_xenia_process_stop_orderly(
    gdox_xenia_process **process,
    gdox_error *error
);

#endif
