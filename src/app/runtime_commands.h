#ifndef GDOX_APP_RUNTIME_COMMANDS_H
#define GDOX_APP_RUNTIME_COMMANDS_H

#include "gdox/emulator.h"
#include "gdox/media.h"
#include "gdox/preserve.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum gdox_runtime_request_kind {
    GDOX_RUNTIME_REQUEST_NONE = 0,
    GDOX_RUNTIME_REQUEST_START,
    GDOX_RUNTIME_REQUEST_RESTART,
    GDOX_RUNTIME_REQUEST_CLOSE,
    GDOX_RUNTIME_REQUEST_EJECT,
    GDOX_RUNTIME_REQUEST_PRESERVE,
    GDOX_RUNTIME_REQUEST_APPLY_DISPLAY,
    GDOX_RUNTIME_REQUEST_OPEN_IMAGE,
    GDOX_RUNTIME_REQUEST_USE_PHYSICAL
} gdox_runtime_request_kind;

typedef struct gdox_runtime_request_entry {
    gdox_runtime_request_kind kind;
    gdox_preservation_format preservation_format;
    bool preservation_verify;
    char path[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_runtime_request_entry;

enum { GDOX_RUNTIME_REQUEST_CAPACITY = 16U };

typedef struct gdox_runtime_request_queue {
    gdox_runtime_request_entry entries[GDOX_RUNTIME_REQUEST_CAPACITY];
    size_t head;
    size_t count;
} gdox_runtime_request_queue;

typedef struct gdox_runtime_command_state {
    gdox_media_source media_source;
    bool has_session;
    bool emulator_running;
    bool has_saved_image;
} gdox_runtime_command_state;

typedef enum gdox_runtime_action {
    GDOX_RUNTIME_ACTION_NONE = 0,
    GDOX_RUNTIME_ACTION_START_EMULATOR,
    GDOX_RUNTIME_ACTION_RESTART_EMULATOR,
    GDOX_RUNTIME_ACTION_STOP_EMULATOR,
    GDOX_RUNTIME_ACTION_EJECT_PHYSICAL,
    GDOX_RUNTIME_ACTION_PRESERVE_PHYSICAL,
    GDOX_RUNTIME_ACTION_APPLY_DISPLAY,
    GDOX_RUNTIME_ACTION_OPEN_IMAGE,
    GDOX_RUNTIME_ACTION_REOPEN_IMAGE,
    GDOX_RUNTIME_ACTION_DISCOVER_PHYSICAL,
    GDOX_RUNTIME_ACTION_USE_PHYSICAL
} gdox_runtime_action;

bool gdox_runtime_request_enqueue(
    gdox_runtime_request_queue *queue,
    const gdox_runtime_request_entry *request
);
bool gdox_runtime_request_dequeue(
    gdox_runtime_request_queue *queue,
    gdox_runtime_request_entry *request
);
gdox_runtime_action gdox_runtime_plan_request(
    const gdox_runtime_request_entry *request,
    const gdox_runtime_command_state *state
);

#endif
