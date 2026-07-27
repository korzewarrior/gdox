#ifndef GDOX_ERROR_H
#define GDOX_ERROR_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_ERROR_MESSAGE_CAPACITY 384U

typedef enum gdox_error_code {
    GDOX_ERROR_NONE = 0,
    GDOX_ERROR_INVALID_ARGUMENT,
    GDOX_ERROR_OUT_OF_BOUNDS,
    GDOX_ERROR_INVALID_SOURCE,
    GDOX_ERROR_INVALID_VOLUME,
    GDOX_ERROR_NOT_FOUND,
    GDOX_ERROR_PROTOCOL,
    GDOX_ERROR_TRANSPORT,
    GDOX_ERROR_IO,
    GDOX_ERROR_UNSUPPORTED,
    GDOX_ERROR_CANCELLED,
    GDOX_ERROR_INTERNAL
} gdox_error_code;

typedef struct gdox_error {
    gdox_error_code code;
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
} gdox_error;

void gdox_error_clear(gdox_error *error);
void gdox_error_set(gdox_error *error, gdox_error_code code, const char *message);
bool gdox_error_is_set(const gdox_error *error);
const char *gdox_error_code_name(gdox_error_code code);

#ifdef __cplusplus
}
#endif

#endif
