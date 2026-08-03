#ifndef GDOX_XENIA_POLICY_H
#define GDOX_XENIA_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_XENIA_LAUNCH_MODULE_CAPACITY 96U

typedef enum gdox_xenia_gpu_backend {
    GDOX_XENIA_GPU_D3D12 = 0,
    GDOX_XENIA_GPU_VULKAN
} gdox_xenia_gpu_backend;

typedef enum gdox_xenia_occlusion_query_mode {
    GDOX_XENIA_OCCLUSION_DEFAULT = 0,
    GDOX_XENIA_OCCLUSION_STRICT,
    GDOX_XENIA_OCCLUSION_FAST_ALT
} gdox_xenia_occlusion_query_mode;

typedef enum gdox_xenia_readback_resolve_mode {
    GDOX_XENIA_READBACK_NONE = 0,
    GDOX_XENIA_READBACK_FAST,
    GDOX_XENIA_READBACK_FULL
} gdox_xenia_readback_resolve_mode;

typedef enum gdox_xenia_patch_set {
    GDOX_XENIA_PATCH_SET_NONE = 0,
    GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1
} gdox_xenia_patch_set;

typedef struct gdox_xenia_runtime {
    const char *revision;
    const char *payload_name;
    const char *payload_sha256;
    uint64_t payload_size;
    gdox_xenia_gpu_backend gpu;
    bool requires_proton;
    bool supports_max_queued_frames;
    bool supports_custom_internal_display_resolution;
    bool supports_host_performance_profile;
    bool supports_storage_isolation;
    bool supports_private_nbd;
    bool supports_managed_disclaimer_acknowledgement;
} gdox_xenia_runtime;

typedef struct gdox_xenia_title_identity {
    uint32_t title_id;
    uint32_t media_id;
    uint8_t disc_number;
    uint8_t disc_count;
} gdox_xenia_title_identity;

typedef struct gdox_xenia_settings {
    bool allow_tearing;
    bool vsync;
    uint16_t max_queued_frames;
    bool allow_invalid_fetch_constants;
    gdox_xenia_occlusion_query_mode occlusion_query;
    uint16_t occlusion_query_saturation_basis_points;
    gdox_xenia_readback_resolve_mode readback_resolve;
    bool protect_zero;
    bool new_xma_decoder;
    bool use_dedicated_xma_thread;
    bool async_shader_compilation;
    bool use_handheld_custom_resolution;
} gdox_xenia_settings;

typedef struct gdox_xenia_launch_policy {
    const gdox_xenia_runtime *runtime;
    const char *launch_module;
    gdox_xenia_patch_set patch_set;
    gdox_xenia_settings settings;
} gdox_xenia_launch_policy;

/*
 * Policy data is generated from packaging/xenia-compatibility.json. The
 * application does not parse or depend on that build-time input at runtime.
 */
const gdox_xenia_launch_policy *gdox_xenia_default_policy(void);
const gdox_xenia_launch_policy *gdox_xenia_select_policy(
    const gdox_xenia_title_identity *identity
);

size_t gdox_xenia_runtime_count(void);
const gdox_xenia_runtime *gdox_xenia_runtime_at(size_t index);
const gdox_xenia_runtime *gdox_xenia_runtime_by_revision(
    const char *revision
);

#ifdef __cplusplus
}
#endif

#endif
