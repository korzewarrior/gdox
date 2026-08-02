#include "app/runtime_commands.h"

bool gdox_runtime_request_enqueue(
    gdox_runtime_request_queue *queue,
    const gdox_runtime_request_entry *request
)
{
    size_t insertion;

    if (queue == NULL || request == NULL
        || request->kind <= GDOX_RUNTIME_REQUEST_NONE
        || request->kind > GDOX_RUNTIME_REQUEST_USE_PHYSICAL) {
        return false;
    }
    insertion = (queue->head + queue->count)
        % GDOX_RUNTIME_REQUEST_CAPACITY;
    if (queue->count > 0U) {
        const size_t tail = insertion == 0U
            ? GDOX_RUNTIME_REQUEST_CAPACITY - 1U
            : insertion - 1U;
        if (queue->entries[tail].kind == request->kind) {
            queue->entries[tail] = *request;
            return true;
        }
    }
    if (queue->count >= GDOX_RUNTIME_REQUEST_CAPACITY) {
        return false;
    }
    queue->entries[insertion] = *request;
    ++queue->count;
    return true;
}

bool gdox_runtime_request_dequeue(
    gdox_runtime_request_queue *queue,
    gdox_runtime_request_entry *request
)
{
    if (queue == NULL || request == NULL || queue->count == 0U) {
        return false;
    }
    *request = queue->entries[queue->head];
    queue->head = (queue->head + 1U) % GDOX_RUNTIME_REQUEST_CAPACITY;
    --queue->count;
    if (queue->count == 0U) {
        queue->head = 0U;
    }
    return true;
}

gdox_runtime_action gdox_runtime_plan_request(
    const gdox_runtime_request_entry *request,
    const gdox_runtime_command_state *state
)
{
    if (request == NULL || state == NULL) {
        return GDOX_RUNTIME_ACTION_NONE;
    }
    switch (request->kind) {
        case GDOX_RUNTIME_REQUEST_START:
            if (state->has_session) {
                return state->emulator_running
                    ? GDOX_RUNTIME_ACTION_NONE
                    : GDOX_RUNTIME_ACTION_START_EMULATOR;
            }
            if (state->media_source == GDOX_MEDIA_DISC_IMAGE) {
                return state->has_saved_image
                    ? GDOX_RUNTIME_ACTION_REOPEN_IMAGE
                    : GDOX_RUNTIME_ACTION_NONE;
            }
            return GDOX_RUNTIME_ACTION_DISCOVER_PHYSICAL;
        case GDOX_RUNTIME_REQUEST_RESTART:
            return state->has_session
                ? GDOX_RUNTIME_ACTION_RESTART_EMULATOR
                : GDOX_RUNTIME_ACTION_NONE;
        case GDOX_RUNTIME_REQUEST_CLOSE:
            return state->emulator_running
                ? GDOX_RUNTIME_ACTION_STOP_EMULATOR
                : GDOX_RUNTIME_ACTION_NONE;
        case GDOX_RUNTIME_REQUEST_EJECT:
            return state->media_source == GDOX_MEDIA_PHYSICAL_DISC
                ? GDOX_RUNTIME_ACTION_EJECT_PHYSICAL
                : GDOX_RUNTIME_ACTION_NONE;
        case GDOX_RUNTIME_REQUEST_PRESERVE:
            return state->media_source == GDOX_MEDIA_PHYSICAL_DISC
                    && state->has_session
                ? GDOX_RUNTIME_ACTION_PRESERVE_PHYSICAL
                : GDOX_RUNTIME_ACTION_NONE;
        case GDOX_RUNTIME_REQUEST_APPLY_DISPLAY:
            return state->has_session && state->emulator_running
                ? GDOX_RUNTIME_ACTION_APPLY_DISPLAY
                : GDOX_RUNTIME_ACTION_NONE;
        case GDOX_RUNTIME_REQUEST_OPEN_IMAGE:
            return GDOX_RUNTIME_ACTION_OPEN_IMAGE;
        case GDOX_RUNTIME_REQUEST_USE_PHYSICAL:
            return GDOX_RUNTIME_ACTION_USE_PHYSICAL;
        case GDOX_RUNTIME_REQUEST_NONE:
            break;
    }
    return GDOX_RUNTIME_ACTION_NONE;
}
