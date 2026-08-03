#include "core/xenia_launch.h"

#include "gdox/hash.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GDOX_XENIA_TOKEN_BYTES 32U

static bool lower_hex(char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'a' && character <= 'f');
}

static bool valid_sha256(const char *digest)
{
    size_t index;

    if (digest == NULL || strlen(digest) != GDOX_SHA256_BYTES * 2U) {
        return false;
    }
    for (index = 0U; index < GDOX_SHA256_BYTES * 2U; ++index) {
        if (!lower_hex(digest[index])) {
            return false;
        }
    }
    return true;
}

bool gdox_xenia_verify_payload(
    const char *path,
    const gdox_xenia_runtime *runtime,
    gdox_error *error
)
{
    gdox_hashes hashes;
    char actual[GDOX_SHA256_BYTES * 2U + 1U];
    uint64_t length;

    gdox_error_clear(error);
    if (runtime == NULL || runtime->revision == NULL
        || runtime->revision[0] == '\0'
        || runtime->payload_name == NULL || runtime->payload_name[0] == '\0'
        || runtime->payload_size == 0U
        || !valid_sha256(runtime->payload_sha256)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a complete reviewed Xenia runtime definition is required"
        );
        return false;
    }
    if (!gdox_hash_file(path, &hashes, &length, error)) {
        return false;
    }
    if (length != runtime->payload_size) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "Xenia payload size does not match the reviewed runtime"
        );
        return false;
    }
    gdox_hash_hex(hashes.sha256, sizeof(hashes.sha256), false, actual);
    if (strcmp(actual, runtime->payload_sha256) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "Xenia payload SHA-256 does not match the reviewed runtime"
        );
        return false;
    }
    return true;
}

bool gdox_xenia_validate_disc_uri(const char *disc_uri, gdox_error *error)
{
    static const char prefix[] = "nbd://127.0.0.1:";
    const char *cursor;
    uint32_t port = 0U;
    size_t digits = 0U;
    size_t token_bytes = 0U;

    gdox_error_clear(error);
    if (disc_uri == NULL
        || strncmp(disc_uri, prefix, sizeof(prefix) - 1U) != 0) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a private GDOX loopback disc URI is required"
        );
        return false;
    }
    cursor = disc_uri + sizeof(prefix) - 1U;
    if (*cursor < '1' || *cursor > '9') {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "GDOX disc URI port is invalid"
        );
        return false;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        if (digits == 5U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "GDOX disc URI port is invalid"
            );
            return false;
        }
        port = port * 10U + (uint32_t)(*cursor - '0');
        ++cursor;
        ++digits;
    }
    if (*cursor != '/' || port == 0U || port > UINT16_MAX) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "GDOX disc URI port is invalid"
        );
        return false;
    }
    ++cursor;
    while (lower_hex(*cursor)) {
        ++cursor;
        ++token_bytes;
    }
    if (*cursor != '\0' || token_bytes != GDOX_XENIA_TOKEN_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "GDOX disc URI session token is invalid"
        );
        return false;
    }
    return true;
}

static bool valid_launch_module(const char *module)
{
    size_t index;
    const size_t bytes = module != NULL ? strlen(module) : 0U;

    if (bytes == 0U || bytes >= GDOX_XENIA_LAUNCH_MODULE_CAPACITY) {
        return false;
    }
    for (index = 0U; index < bytes; ++index) {
        const char character = module[index];
        if (!((character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '_' || character == '-' || character == '.')) {
            return false;
        }
    }
    return true;
}

static bool format_argument(
    char *output,
    size_t capacity,
    const char *name,
    const char *value,
    gdox_error *error
)
{
    const int written = snprintf(output, capacity, "--%s=%s", name, value);

    if (written < 0 || (size_t)written >= capacity) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia launch argument is too long"
        );
        return false;
    }
    return true;
}

static bool format_unsigned_argument(
    char *output,
    size_t capacity,
    const char *name,
    uint32_t value,
    gdox_error *error
)
{
    const int written = snprintf(
        output,
        capacity,
        "--%s=%u",
        name,
        (unsigned int)value
    );

    if (written < 0 || (size_t)written >= capacity) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia launch argument is too long"
        );
        return false;
    }
    return true;
}

static bool format_u64_argument(
    char *output,
    size_t capacity,
    const char *name,
    uint64_t value,
    gdox_error *error
)
{
    const int written = snprintf(
        output, capacity, "--%s=%" PRIu64, name, value
    );

    if (written < 0 || (size_t)written >= capacity) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia launch argument is too long"
        );
        return false;
    }
    return true;
}

static bool format_saturation_argument(
    char *output,
    size_t capacity,
    uint16_t basis_points,
    gdox_error *error
)
{
    const uint32_t whole = basis_points / UINT32_C(10000);
    uint32_t fraction = basis_points % UINT32_C(10000);
    unsigned int fraction_digits = 4U;
    int written;

    if (basis_points == 0U || basis_points > 10000U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia occlusion-query saturation is outside its reviewed range"
        );
        return false;
    }
    if (fraction == 0U) {
        written = snprintf(
            output,
            capacity,
            "--occlusion_query_saturation=%u.0",
            (unsigned int)whole
        );
    } else {
        while (fraction_digits > 1U && fraction % 10U == 0U) {
            fraction /= 10U;
            --fraction_digits;
        }
        written = snprintf(
            output,
            capacity,
            "--occlusion_query_saturation=%u.%0*u",
            (unsigned int)whole,
            (int)fraction_digits,
            (unsigned int)fraction
        );
    }
    if (written < 0 || (size_t)written >= capacity) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia launch argument is too long"
        );
        return false;
    }
    return true;
}

static bool append_argument(
    gdox_xenia_launch_plan *plan,
    const char *argument,
    gdox_error *error
)
{
    if (plan->count + 1U >= GDOX_XENIA_MAX_ARGUMENTS) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "Xenia launch plan exceeds its bounded argument capacity"
        );
        return false;
    }
    plan->arguments[plan->count++] = argument;
    plan->arguments[plan->count] = NULL;
    return true;
}

static bool absolute_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (path[0] == '/') {
        return true;
    }
    if (path[0] == '\\' && path[1] == '\\') {
        return true;
    }
    return ((path[0] >= 'A' && path[0] <= 'Z')
            || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

typedef struct gdox_xenia_launch_arguments {
    const char *backend;
    const char *tearing;
    const char *occlusion;
    const char *readback;
    uint32_t display_resolution_x;
    uint32_t display_resolution_y;
} gdox_xenia_launch_arguments;

static bool validate_launch_inputs(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    if (options == NULL || options->runtime == NULL
        || options->runtime->definition == NULL || options->policy == NULL
        || options->policy->runtime != options->runtime->definition
        || !absolute_path(options->storage_root)
        || !absolute_path(options->content_root)
        || !absolute_path(options->cache_root)
        || !absolute_path(options->log_file)
        || target == NULL || target->location == NULL
        || target->location[0] == '\0' || plan == NULL
        || !valid_launch_module(options->policy->launch_module)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "complete verified Xenia launch options are required"
        );
        return false;
    }
    return true;
}

static bool validate_runtime_contract(
    const gdox_xenia_runtime *runtime,
    gdox_error *error
)
{
    if (!runtime->supports_storage_isolation) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "reviewed Xenia runtime lacks the required storage isolation"
        );
        return false;
    }
    if (!runtime->supports_managed_disclaimer_acknowledgement) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "reviewed Xenia runtime lacks managed disclaimer acknowledgement"
        );
        return false;
    }
    return true;
}

static bool prepare_target(
    const gdox_xenia_runtime *runtime,
    const gdox_xenia_target *target,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    if (target->kind == GDOX_XENIA_TARGET_IMAGE) {
        if (target->length != 0U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "Xenia image targets do not accept an export length"
            );
            return false;
        }
    } else if (target->kind == GDOX_XENIA_TARGET_PRIVATE_NBD) {
        if (!runtime->supports_private_nbd) {
            gdox_error_set(
                error,
                GDOX_ERROR_UNSUPPORTED,
                "reviewed Xenia runtime has no private disc transport"
            );
            return false;
        }
        if (target->length == 0U) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "private Xenia disc export length is required"
            );
            return false;
        }
        if (!gdox_xenia_validate_disc_uri(target->location, error)
            || !format_argument(
                plan->gdox_disc,
                sizeof(plan->gdox_disc),
                "gdox_disc",
                target->location,
                error
            ) || !format_u64_argument(
                plan->gdox_disc_length,
                sizeof(plan->gdox_disc_length),
                "gdox_disc_length",
                target->length,
                error
            )) {
            return false;
        }
    } else {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "Xenia target kind is invalid"
        );
        return false;
    }
    return true;
}

static bool select_host_profile(
    gdox_xenia_performance_profile profile,
    const gdox_xenia_settings *settings,
    gdox_xenia_launch_arguments *arguments,
    gdox_error *error
)
{
    switch (profile) {
        case GDOX_XENIA_PERFORMANCE_DESKTOP:
            arguments->display_resolution_x = 0U;
            arguments->display_resolution_y = 0U;
            break;
        case GDOX_XENIA_PERFORMANCE_HANDHELD:
            arguments->display_resolution_x =
                settings->use_handheld_custom_resolution ? 720U : 0U;
            arguments->display_resolution_y =
                settings->use_handheld_custom_resolution ? 480U : 0U;
            break;
        default:
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_ARGUMENT,
                "Xenia performance profile is unsupported"
            );
            return false;
    }
    return true;
}

static bool validate_managed_configuration(
    const gdox_xenia_options *options,
    gdox_error *error
)
{
    const gdox_xenia_runtime *runtime = options->runtime->definition;
    const gdox_xenia_settings *settings = &options->policy->settings;

    if ((runtime->gpu != GDOX_XENIA_GPU_D3D12
            && runtime->gpu != GDOX_XENIA_GPU_VULKAN)
        || !runtime->supports_custom_internal_display_resolution
        || !runtime->supports_host_performance_profile
        || (options->policy->patch_set != GDOX_XENIA_PATCH_SET_NONE
            && options->policy->patch_set
                != GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1)
        || settings->max_queued_frames < 4U
        || settings->max_queued_frames > 64U
        || settings->occlusion_query_saturation_basis_points == 0U
        || settings->occlusion_query_saturation_basis_points > 10000U
        || (settings->occlusion_query != GDOX_XENIA_OCCLUSION_DEFAULT
            && settings->occlusion_query != GDOX_XENIA_OCCLUSION_STRICT
            && settings->occlusion_query != GDOX_XENIA_OCCLUSION_FAST_ALT)
        || (settings->readback_resolve != GDOX_XENIA_READBACK_NONE
            && settings->readback_resolve != GDOX_XENIA_READBACK_FAST
            && settings->readback_resolve != GDOX_XENIA_READBACK_FULL)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia launch configuration contains unsupported managed settings"
        );
        return false;
    }
    return true;
}

static void select_compatibility_arguments(
    const gdox_xenia_runtime *runtime,
    const gdox_xenia_settings *settings,
    gdox_xenia_launch_arguments *arguments
)
{
    arguments->backend = runtime->gpu == GDOX_XENIA_GPU_VULKAN
        ? "--gpu=vulkan"
        : "--gpu=d3d12";
    arguments->tearing = runtime->gpu == GDOX_XENIA_GPU_VULKAN
        ? (settings->allow_tearing
            ? "--vulkan_allow_present_mode_immediate=true"
            : "--vulkan_allow_present_mode_immediate=false")
        : (settings->allow_tearing
            ? "--d3d12_allow_variable_refresh_rate_and_tearing=true"
            : "--d3d12_allow_variable_refresh_rate_and_tearing=false");
    arguments->occlusion =
        settings->occlusion_query == GDOX_XENIA_OCCLUSION_STRICT
        ? "--occlusion_query=strict"
        : (settings->occlusion_query == GDOX_XENIA_OCCLUSION_FAST_ALT
            ? "--occlusion_query=fast-alt"
            : "--occlusion_query=fast");
    arguments->readback =
        settings->readback_resolve == GDOX_XENIA_READBACK_FULL
        ? "--readback_resolve=full"
        : settings->readback_resolve == GDOX_XENIA_READBACK_FAST
            ? "--readback_resolve=fast"
            : "--readback_resolve=none";
}

static bool format_launch_arguments(
    const gdox_xenia_options *options,
    const gdox_xenia_launch_arguments *arguments,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const gdox_xenia_settings *settings = &options->policy->settings;

    return format_argument(
            plan->module,
            sizeof(plan->module),
            "launch_module",
            options->policy->launch_module,
            error
        ) && format_argument(
            plan->storage,
            sizeof(plan->storage),
            "storage_root",
            options->storage_root,
            error
        ) && format_argument(
            plan->content,
            sizeof(plan->content),
            "content_root",
            options->content_root,
            error
        ) && format_argument(
            plan->cache,
            sizeof(plan->cache),
            "cache_root",
            options->cache_root,
            error
        ) && format_argument(
            plan->log,
            sizeof(plan->log),
            "log_file",
            options->log_file,
            error
        ) && format_unsigned_argument(
            plan->queued_frames,
            sizeof(plan->queued_frames),
            "apu_max_queued_frames",
            settings->max_queued_frames,
            error
        ) && format_unsigned_argument(
            plan->framerate_limit,
            sizeof(plan->framerate_limit),
            "framerate_limit",
            settings->vsync ? 60U : 0U,
            error
        ) && format_saturation_argument(
            plan->occlusion_query_saturation,
            sizeof(plan->occlusion_query_saturation),
            settings->occlusion_query_saturation_basis_points,
            error
        ) && format_unsigned_argument(
            plan->display_resolution_x,
            sizeof(plan->display_resolution_x),
            "custom_internal_display_resolution_x",
            arguments->display_resolution_x,
            error
        ) && format_unsigned_argument(
            plan->display_resolution_y,
            sizeof(plan->display_resolution_y),
            "custom_internal_display_resolution_y",
            arguments->display_resolution_y,
            error
        );
}

static bool append_argument_list(
    gdox_xenia_launch_plan *plan,
    const char *const *arguments,
    size_t count,
    gdox_error *error
)
{
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (!append_argument(plan, arguments[index], error)) {
            return false;
        }
    }
    return true;
}

static bool append_backend_arguments(
    const gdox_xenia_options *options,
    const gdox_xenia_launch_arguments *selected,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const char *const common[] = {
        options->runtime->launcher,
        plan->module,
        selected->backend,
        selected->tearing,
    };

    if (!append_argument_list(
            plan, common, sizeof(common) / sizeof(common[0]), error
        )) {
        return false;
    }
    if (options->runtime->definition->gpu == GDOX_XENIA_GPU_D3D12) {
        const char *const d3d12[] = {
            "--render_target_path_d3d12=rtv",
            "--d3d12_queue_priority=1",
        };

        return append_argument_list(
            plan, d3d12, sizeof(d3d12) / sizeof(d3d12[0]), error
        );
    }
    return append_argument(plan, "--render_target_path_vulkan=fbo", error);
}

static bool append_title_prefix_arguments(
    const gdox_xenia_settings *settings,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const char *const arguments[] = {
        settings->vsync ? "--vsync=true" : "--vsync=false",
        settings->async_shader_compilation
            ? "--async_shader_compilation=true"
            : "--async_shader_compilation=false",
        "--apply_patches=true",
    };

    return append_argument_list(
        plan, arguments, sizeof(arguments) / sizeof(arguments[0]), error
    );
}

static bool append_storage_invariants(
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    static const char *const arguments[] = {
        "--gdox_disclaimer_acknowledged=true",
        "--gdox_persistent_content_saves_only=true",
        "--store_shaders=false",
        "--disable_instruction_infocache=true",
    };

    return append_argument_list(
        plan, arguments, sizeof(arguments) / sizeof(arguments[0]), error
    );
}

static bool append_host_arguments(
    const gdox_xenia_runtime *runtime,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const char *const common[] = {
        "--host_present_from_non_ui_thread=true",
        "--ignore_thread_affinities=true",
        plan->framerate_limit,
    };
    const char *const display[] = {
        plan->display_resolution_x,
        plan->display_resolution_y,
    };

    if (!append_argument_list(
            plan, common, sizeof(common) / sizeof(common[0]), error
        )) {
        return false;
    }
    if (runtime->gpu == GDOX_XENIA_GPU_D3D12) {
        if (!append_argument(
                plan, "--d3d12_pipeline_creation_threads=-1", error
            )) {
            return false;
        }
    } else if (!append_argument(
            plan, "--vulkan_allow_present_mode_fifo_relaxed=false", error
        ) || !append_argument(
            plan, "--vulkan_pipeline_creation_threads=-1", error
        )) {
        return false;
    }
    if (runtime->supports_max_queued_frames
        && !append_argument(plan, plan->queued_frames, error)) {
        return false;
    }
    return append_argument_list(
        plan, display, sizeof(display) / sizeof(display[0]), error
    );
}

static bool append_title_arguments(
    const gdox_xenia_settings *settings,
    const gdox_xenia_launch_arguments *selected,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const char *const arguments[] = {
        selected->occlusion,
        plan->occlusion_query_saturation,
        selected->readback,
        settings->allow_invalid_fetch_constants
            ? "--gpu_allow_invalid_fetch_constants=true"
            : "--gpu_allow_invalid_fetch_constants=false",
        "--mount_cache=false",
        "--mount_scratch=false",
        settings->protect_zero
            ? "--protect_zero=true"
            : "--protect_zero=false",
        settings->new_xma_decoder
            ? "--xma_decoder=new"
            : "--xma_decoder=old",
        settings->use_dedicated_xma_thread
            ? "--use_dedicated_xma_thread=true"
            : "--use_dedicated_xma_thread=false",
    };

    return append_argument_list(
        plan, arguments, sizeof(arguments) / sizeof(arguments[0]), error
    );
}

static bool append_session_arguments(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const char *const leading[] = {
        options->fullscreen ? "--fullscreen=true" : "--fullscreen=false",
        "--discord=false",
    };
    const char *const trailing[] = {
        plan->log,
        options->console_output
            ? "--log_to_stdout=true"
            : "--log_to_stdout=false",
        plan->storage,
        plan->content,
        plan->cache,
    };

    if (!append_argument_list(
            plan, leading, sizeof(leading) / sizeof(leading[0]), error
        )) {
        return false;
    }
    if (options->performance_profile == GDOX_XENIA_PERFORMANCE_HANDHELD
        && (!append_argument(plan, "--log_level=0", error)
            || !append_argument(plan, "--flush_log=false", error))) {
        return false;
    }
    if (!append_argument_list(
            plan, trailing, sizeof(trailing) / sizeof(trailing[0]), error
        )) {
        return false;
    }
    if (target->kind == GDOX_XENIA_TARGET_IMAGE) {
        return append_argument(plan, target->location, error);
    }
    return append_argument(plan, plan->gdox_disc, error)
        && append_argument(plan, plan->gdox_disc_length, error);
}

bool gdox_xenia_build_target_launch_plan(
    const gdox_xenia_options *options,
    const gdox_xenia_target *target,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    gdox_xenia_launch_arguments selected = {0};
    const gdox_xenia_runtime *runtime;
    const gdox_xenia_settings *settings;

    gdox_error_clear(error);
    if (plan != NULL) {
        memset(plan, 0, sizeof(*plan));
    }
    if (!validate_launch_inputs(options, target, plan, error)) {
        return false;
    }
    runtime = options->runtime->definition;
    settings = &options->policy->settings;
    if (!validate_runtime_contract(runtime, error)
        || !prepare_target(runtime, target, plan, error)
        || !select_host_profile(
            options->performance_profile, settings, &selected, error
        ) || !validate_managed_configuration(options, error)) {
        return false;
    }
    select_compatibility_arguments(runtime, settings, &selected);
    return format_launch_arguments(options, &selected, plan, error)
        && append_backend_arguments(options, &selected, plan, error)
        && append_title_prefix_arguments(settings, plan, error)
        && append_storage_invariants(plan, error)
        && append_host_arguments(runtime, plan, error)
        && append_title_arguments(settings, &selected, plan, error)
        && append_session_arguments(options, target, plan, error);
}

bool gdox_xenia_build_launch_plan(
    const gdox_xenia_options *options,
    const char *disc_path,
    gdox_xenia_launch_plan *plan,
    gdox_error *error
)
{
    const gdox_xenia_target target = {
        .kind = GDOX_XENIA_TARGET_IMAGE,
        .location = disc_path,
        .length = 0U,
    };

    return gdox_xenia_build_target_launch_plan(
        options, &target, plan, error
    );
}
