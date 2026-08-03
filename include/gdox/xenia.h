#ifndef GDOX_XENIA_H
#define GDOX_XENIA_H

#include "gdox/emulator.h"
#include "gdox/error.h"
#include "gdox/xenia_policy.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gdox_xenia_runtime_origin {
    GDOX_XENIA_RUNTIME_BUNDLED = 0,
    GDOX_XENIA_RUNTIME_OVERRIDE
} gdox_xenia_runtime_origin;

typedef struct gdox_xenia_runtime_descriptor {
    const gdox_xenia_runtime *definition;
    gdox_xenia_runtime_origin origin;
    char launcher[GDOX_EMULATOR_PATH_CAPACITY];
    char payload[GDOX_EMULATOR_PATH_CAPACITY];
} gdox_xenia_runtime_descriptor;

typedef enum gdox_xenia_target_kind {
    GDOX_XENIA_TARGET_IMAGE = 0,
    GDOX_XENIA_TARGET_PRIVATE_NBD
} gdox_xenia_target_kind;

typedef struct gdox_xenia_target {
    gdox_xenia_target_kind kind;
    const char *location;
    uint64_t length;
} gdox_xenia_target;

/* Host performance policy is independent of per-title compatibility. */
typedef enum gdox_xenia_performance_profile {
    GDOX_XENIA_PERFORMANCE_DESKTOP = 0,
    GDOX_XENIA_PERFORMANCE_HANDHELD
} gdox_xenia_performance_profile;

typedef struct gdox_xenia_options {
    const gdox_xenia_runtime_descriptor *runtime;
    const gdox_xenia_launch_policy *policy;
    gdox_xenia_performance_profile performance_profile;
    const char *storage_root;
    const char *content_root;
    const char *cache_root;
    const char *log_file;
    bool console_output;
    bool fullscreen;
} gdox_xenia_options;

typedef struct gdox_xenia_process gdox_xenia_process;

/* Reports whether the current platform implements a target kind. */
bool gdox_xenia_target_supported(gdox_xenia_target_kind kind);
/* Reports whether one reviewed runtime can consume a platform target. */
bool gdox_xenia_runtime_target_supported(
    const gdox_xenia_runtime *runtime,
    gdox_xenia_target_kind kind
);
/* Checks required host tools without creating a bridge or launching Xenia. */
bool gdox_xenia_target_preflight(
    gdox_xenia_target_kind kind,
    gdox_error *error
);

/* Resolves and verifies one exact reviewed runtime. An override is accepted
 * only when its payload matches the requested size and SHA-256. */
bool gdox_xenia_resolve_runtime(
    const gdox_xenia_runtime *runtime,
    const char *override,
    gdox_xenia_runtime_descriptor *output,
    gdox_error *error
);

bool gdox_xenia_validate_disc_uri(const char *disc_uri, gdox_error *error);
bool gdox_xenia_verify_payload(
    const char *path,
    const gdox_xenia_runtime *runtime,
    gdox_error *error
);

bool gdox_xenia_launch(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_process **process,
    gdox_error *error
);
bool gdox_xenia_poll(
    gdox_xenia_process *process,
    bool *running,
    int *exit_code,
    gdox_error *error
);
bool gdox_xenia_stop(
    gdox_xenia_process *process,
    uint32_t grace_ms,
    int *exit_code,
    gdox_error *error
);
void gdox_xenia_process_destroy(gdox_xenia_process *process);

#ifdef __cplusplus
}
#endif

#endif
