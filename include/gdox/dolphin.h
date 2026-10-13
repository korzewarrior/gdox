#ifndef GDOX_DOLPHIN_H
#define GDOX_DOLPHIN_H

#include "gdox/emulator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdox_dolphin_process gdox_dolphin_process;
typedef struct gdox_dolphin_runtime {
    char launcher[GDOX_EMULATOR_PATH_CAPACITY];
    char payload[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_dolphin_runtime;

bool gdox_dolphin_resolve(gdox_dolphin_runtime *runtime, gdox_error *error);
/* A failed launch may retain a process for cleanup. Stop it before closing media. */
bool gdox_dolphin_launch(const gdox_dolphin_runtime *runtime,
    const char *user_directory, const char *disc_uri, uint64_t length,
    bool fullscreen, gdox_dolphin_process **output, gdox_error *error);
bool gdox_dolphin_poll(gdox_dolphin_process *process,
    bool *running, int *exit_code, gdox_error *error);
/* Consumes the process only after both the child and disc bridge have closed. */
bool gdox_dolphin_stop(gdox_dolphin_process **process, gdox_error *error);

#ifdef __cplusplus
}
#endif
#endif
