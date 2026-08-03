#include "test.h"

#include "gdox/xenia_policy.h"

#include <string.h>

static void test_runtime_catalog(void)
{
    const gdox_xenia_runtime *runtime;

    GDOX_TEST_CHECK(gdox_xenia_runtime_count() == 2U);
    runtime = gdox_xenia_runtime_at(0U);
    GDOX_TEST_CHECK(runtime != NULL);
    GDOX_TEST_CHECK(strcmp(runtime->revision, "72ce13097") == 0);
#if defined(GDOX_XENIA_CATALOG_WINDOWS)
    GDOX_TEST_CHECK(runtime->payload_size == UINT64_C(15783936));
    GDOX_TEST_CHECK(strlen(runtime->payload_sha256) == 64U);
    GDOX_TEST_CHECK(strcmp(runtime->payload_name, "xenia_canary.exe") == 0);
    GDOX_TEST_CHECK(runtime->supports_max_queued_frames);
    GDOX_TEST_CHECK(runtime->supports_custom_internal_display_resolution);
    GDOX_TEST_CHECK(runtime->supports_host_performance_profile);
    GDOX_TEST_CHECK(runtime->supports_storage_isolation);
    GDOX_TEST_CHECK(runtime->supports_private_nbd);
    GDOX_TEST_CHECK(
        runtime->supports_managed_disclaimer_acknowledgement
    );
    GDOX_TEST_CHECK(!runtime->requires_proton);
    GDOX_TEST_CHECK(runtime->gpu == GDOX_XENIA_GPU_D3D12);
#elif defined(GDOX_XENIA_CATALOG_LINUX)
    GDOX_TEST_CHECK(runtime->payload_size == UINT64_C(15783936));
    GDOX_TEST_CHECK(strlen(runtime->payload_sha256) == 64U);
    GDOX_TEST_CHECK(strcmp(runtime->payload_name, "xenia_canary.exe") == 0);
    GDOX_TEST_CHECK(runtime->supports_max_queued_frames);
    GDOX_TEST_CHECK(runtime->supports_custom_internal_display_resolution);
    GDOX_TEST_CHECK(runtime->supports_host_performance_profile);
    GDOX_TEST_CHECK(runtime->supports_storage_isolation);
    GDOX_TEST_CHECK(!runtime->supports_private_nbd);
    GDOX_TEST_CHECK(
        runtime->supports_managed_disclaimer_acknowledgement
    );
    GDOX_TEST_CHECK(runtime->requires_proton);
    GDOX_TEST_CHECK(runtime->gpu == GDOX_XENIA_GPU_D3D12);
#else
    GDOX_TEST_CHECK(runtime->payload_size == UINT64_C(0));
    GDOX_TEST_CHECK(runtime->payload_sha256[0] == '\0');
    GDOX_TEST_CHECK(runtime->payload_name[0] == '\0');
    GDOX_TEST_CHECK(!runtime->supports_max_queued_frames);
    GDOX_TEST_CHECK(!runtime->supports_custom_internal_display_resolution);
    GDOX_TEST_CHECK(!runtime->supports_host_performance_profile);
    GDOX_TEST_CHECK(!runtime->supports_storage_isolation);
    GDOX_TEST_CHECK(!runtime->supports_private_nbd);
    GDOX_TEST_CHECK(
        !runtime->supports_managed_disclaimer_acknowledgement
    );
#endif
    runtime = gdox_xenia_runtime_at(1U);
    GDOX_TEST_CHECK(runtime != NULL);
    GDOX_TEST_CHECK(strcmp(runtime->revision, "7d8be7f17") == 0);
    GDOX_TEST_CHECK(!runtime->supports_max_queued_frames);
#if defined(GDOX_XENIA_CATALOG_WINDOWS)
    GDOX_TEST_CHECK(runtime->supports_custom_internal_display_resolution);
    GDOX_TEST_CHECK(runtime->supports_host_performance_profile);
    GDOX_TEST_CHECK(runtime->supports_storage_isolation);
    GDOX_TEST_CHECK(runtime->payload_size == UINT64_C(16756736));
    GDOX_TEST_CHECK(runtime->supports_private_nbd);
    GDOX_TEST_CHECK(
        runtime->supports_managed_disclaimer_acknowledgement
    );
#elif defined(GDOX_XENIA_CATALOG_LINUX)
    GDOX_TEST_CHECK(runtime->supports_custom_internal_display_resolution);
    GDOX_TEST_CHECK(runtime->supports_host_performance_profile);
    GDOX_TEST_CHECK(runtime->supports_storage_isolation);
    GDOX_TEST_CHECK(runtime->payload_size == UINT64_C(16756736));
    GDOX_TEST_CHECK(!runtime->supports_private_nbd);
    GDOX_TEST_CHECK(
        runtime->supports_managed_disclaimer_acknowledgement
    );
#else
    GDOX_TEST_CHECK(!runtime->supports_custom_internal_display_resolution);
    GDOX_TEST_CHECK(!runtime->supports_host_performance_profile);
    GDOX_TEST_CHECK(!runtime->supports_storage_isolation);
    GDOX_TEST_CHECK(runtime->payload_size == UINT64_C(0));
    GDOX_TEST_CHECK(runtime->payload_sha256[0] == '\0');
    GDOX_TEST_CHECK(runtime->payload_name[0] == '\0');
    GDOX_TEST_CHECK(!runtime->supports_private_nbd);
    GDOX_TEST_CHECK(
        !runtime->supports_managed_disclaimer_acknowledgement
    );
#endif
#if defined(GDOX_XENIA_CATALOG_LINUX)
    GDOX_TEST_CHECK(runtime->gpu == GDOX_XENIA_GPU_D3D12);
    GDOX_TEST_CHECK(runtime->requires_proton);
    GDOX_TEST_CHECK(
        strcmp(runtime->payload_name, "xenia_canary.exe") == 0
    );
#else
    GDOX_TEST_CHECK(runtime->gpu == GDOX_XENIA_GPU_D3D12);
#endif
    GDOX_TEST_CHECK(gdox_xenia_runtime_at(2U) == NULL);
    GDOX_TEST_CHECK(gdox_xenia_runtime_by_revision(NULL) == NULL);
    GDOX_TEST_CHECK(gdox_xenia_runtime_by_revision("") == NULL);
    GDOX_TEST_CHECK(gdox_xenia_runtime_by_revision("unknown") == NULL);
    GDOX_TEST_CHECK(
        gdox_xenia_runtime_by_revision("7d8be7f17") == runtime
    );
}

static void test_default_policy(void)
{
    const gdox_xenia_launch_policy *policy = gdox_xenia_default_policy();

    GDOX_TEST_CHECK(policy != NULL);
    GDOX_TEST_CHECK(policy == gdox_xenia_select_policy(NULL));
    GDOX_TEST_CHECK(policy->runtime != NULL);
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "72ce13097") == 0);
    GDOX_TEST_CHECK(strcmp(policy->launch_module, "default.xex") == 0);
    GDOX_TEST_CHECK(policy->patch_set == GDOX_XENIA_PATCH_SET_NONE);
    GDOX_TEST_CHECK(!policy->settings.allow_tearing);
    GDOX_TEST_CHECK(policy->settings.vsync);
    GDOX_TEST_CHECK(policy->settings.max_queued_frames == 64U);
    GDOX_TEST_CHECK(!policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_DEFAULT
    );
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query_saturation_basis_points == 10000U
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_NONE
    );
    GDOX_TEST_CHECK(policy->settings.protect_zero);
    GDOX_TEST_CHECK(policy->settings.new_xma_decoder);
    GDOX_TEST_CHECK(policy->settings.use_dedicated_xma_thread);
    GDOX_TEST_CHECK(policy->settings.async_shader_compilation);
    GDOX_TEST_CHECK(policy->settings.use_handheld_custom_resolution);
}

static void test_exact_title_selection(void)
{
    gdox_xenia_title_identity identity = {
        UINT32_C(0x555308c2),
        UINT32_C(0x68ec85bf),
        1U,
        2U,
    };
    const gdox_xenia_launch_policy *policy =
        gdox_xenia_select_policy(&identity);

    GDOX_TEST_CHECK(policy != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(policy->patch_set == GDOX_XENIA_PATCH_SET_NONE);
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "72ce13097") == 0);
    GDOX_TEST_CHECK(strcmp(policy->launch_module, "scimitar_final.xex") == 0);
    GDOX_TEST_CHECK(policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_STRICT
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_FULL
    );

    identity.title_id = UINT32_C(0x555308ae);
    identity.media_id = UINT32_C(0x6d9f552e);
    policy = gdox_xenia_select_policy(&identity);
    GDOX_TEST_CHECK(policy != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "7d8be7f17") == 0);
    GDOX_TEST_CHECK(policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_STRICT
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_NONE
    );
    GDOX_TEST_CHECK(policy->settings.protect_zero);
    GDOX_TEST_CHECK(policy->settings.new_xma_decoder);
    GDOX_TEST_CHECK(policy->settings.use_dedicated_xma_thread);

    identity = (gdox_xenia_title_identity){
        UINT32_C(0x4d5307dc),
        UINT32_C(0x596f9615),
        1U,
        1U,
    };
    policy = gdox_xenia_select_policy(&identity);
    GDOX_TEST_CHECK(policy != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "72ce13097") == 0);
    GDOX_TEST_CHECK(strcmp(policy->launch_module, "default.xex") == 0);
    GDOX_TEST_CHECK(!policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_FAST_ALT
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_FULL
    );
    GDOX_TEST_CHECK(!policy->settings.protect_zero);
    GDOX_TEST_CHECK(policy->settings.new_xma_decoder);
    GDOX_TEST_CHECK(!policy->settings.use_dedicated_xma_thread);
}

static void test_halo_4_policy(void)
{
    const gdox_xenia_title_identity identity = {
        UINT32_C(0x4d530919),
        UINT32_C(0x1c9d20bc),
        1U,
        1U,
    };
    const gdox_xenia_launch_policy *policy =
        gdox_xenia_select_policy(&identity);

    GDOX_TEST_CHECK(policy != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "7d8be7f17") == 0);
    GDOX_TEST_CHECK(strcmp(policy->launch_module, "default.xex") == 0);
    GDOX_TEST_CHECK(policy->patch_set == GDOX_XENIA_PATCH_SET_NONE);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_DEFAULT
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_FAST
    );
    GDOX_TEST_CHECK(!policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(policy->settings.async_shader_compilation);
}

static void test_mass_effect_policy(void)
{
    gdox_xenia_title_identity identity = {
        UINT32_C(0x4d5307e8),
        UINT32_C(0x572ba75d),
        1U,
        1U,
    };
    const gdox_xenia_launch_policy *policy =
        gdox_xenia_select_policy(&identity);

    GDOX_TEST_CHECK(policy != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "72ce13097") == 0);
    GDOX_TEST_CHECK(strcmp(policy->launch_module, "default.xex") == 0);
    GDOX_TEST_CHECK(
        policy->patch_set
            == GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1
    );
    GDOX_TEST_CHECK(!policy->settings.allow_tearing);
    GDOX_TEST_CHECK(policy->settings.vsync);
    GDOX_TEST_CHECK(policy->settings.max_queued_frames == 64U);
    GDOX_TEST_CHECK(!policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_STRICT
    );
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query_saturation_basis_points == 7500U
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_NONE
    );
    GDOX_TEST_CHECK(policy->settings.protect_zero);
    GDOX_TEST_CHECK(policy->settings.new_xma_decoder);
    GDOX_TEST_CHECK(policy->settings.use_dedicated_xma_thread);
    GDOX_TEST_CHECK(!policy->settings.async_shader_compilation);
    GDOX_TEST_CHECK(!policy->settings.use_handheld_custom_resolution);
}

static void test_halo_3_policy(void)
{
    gdox_xenia_title_identity identity = {
        UINT32_C(0x4d5307e6),
        UINT32_C(0x699e0227),
        1U,
        1U,
    };
    const gdox_xenia_launch_policy *policy =
        gdox_xenia_select_policy(&identity);

    GDOX_TEST_CHECK(policy != gdox_xenia_default_policy());
    GDOX_TEST_CHECK(strcmp(policy->runtime->revision, "72ce13097") == 0);
    GDOX_TEST_CHECK(strcmp(policy->launch_module, "default.xex") == 0);
    GDOX_TEST_CHECK(policy->patch_set == GDOX_XENIA_PATCH_SET_NONE);
    GDOX_TEST_CHECK(!policy->settings.allow_tearing);
    GDOX_TEST_CHECK(policy->settings.vsync);
    GDOX_TEST_CHECK(policy->settings.max_queued_frames == 64U);
    GDOX_TEST_CHECK(!policy->settings.allow_invalid_fetch_constants);
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query == GDOX_XENIA_OCCLUSION_DEFAULT
    );
    GDOX_TEST_CHECK(
        policy->settings.occlusion_query_saturation_basis_points == 10000U
    );
    GDOX_TEST_CHECK(
        policy->settings.readback_resolve == GDOX_XENIA_READBACK_FAST
    );
    GDOX_TEST_CHECK(!policy->settings.protect_zero);
    GDOX_TEST_CHECK(policy->settings.new_xma_decoder);
    GDOX_TEST_CHECK(policy->settings.use_dedicated_xma_thread);
    GDOX_TEST_CHECK(policy->settings.async_shader_compilation);
    GDOX_TEST_CHECK(policy->settings.use_handheld_custom_resolution);
}

static void test_identity_requires_all_fields(void)
{
    static const gdox_xenia_title_identity mismatches[] = {
        {UINT32_C(0x555308c3), UINT32_C(0x68ec85bf), 1U, 2U},
        {UINT32_C(0x555308c2), UINT32_C(0x68ec85be), 1U, 2U},
        {UINT32_C(0x555308c2), UINT32_C(0x68ec85bf), 2U, 2U},
        {UINT32_C(0x555308c2), UINT32_C(0x68ec85bf), 1U, 1U},
        {UINT32_C(0x4d5307dd), UINT32_C(0x596f9615), 1U, 1U},
        {UINT32_C(0x4d5307dc), UINT32_C(0x596f9614), 1U, 1U},
        {UINT32_C(0x4d5307dc), UINT32_C(0x596f9615), 2U, 2U},
        {UINT32_C(0x4d5307dc), UINT32_C(0x596f9615), 1U, 2U},
        {UINT32_C(0x4d5307e9), UINT32_C(0x572ba75d), 1U, 1U},
        {UINT32_C(0x4d5307e8), UINT32_C(0x572ba75c), 1U, 1U},
        {UINT32_C(0x4d5307e8), UINT32_C(0x572ba75d), 2U, 2U},
        {UINT32_C(0x4d5307e8), UINT32_C(0x572ba75d), 1U, 2U},
        {UINT32_C(0x4d5307e7), UINT32_C(0x699e0227), 1U, 1U},
        {UINT32_C(0x4d5307e6), UINT32_C(0x699e0226), 1U, 1U},
        {UINT32_C(0x4d5307e6), UINT32_C(0x699e0227), 2U, 2U},
        {UINT32_C(0x4d5307e6), UINT32_C(0x699e0227), 1U, 2U},
        {UINT32_C(0x4d530918), UINT32_C(0x1c9d20bc), 1U, 1U},
        {UINT32_C(0x4d530919), UINT32_C(0x1c9d20bd), 1U, 1U},
        {UINT32_C(0x4d530919), UINT32_C(0x1c9d20bc), 2U, 1U},
        {UINT32_C(0x4d530919), UINT32_C(0x1c9d20bc), 1U, 2U},
    };
    size_t index;

    for (index = 0U; index < sizeof(mismatches) / sizeof(mismatches[0]); ++index) {
        GDOX_TEST_CHECK(
            gdox_xenia_select_policy(&mismatches[index])
            == gdox_xenia_default_policy()
        );
    }
}

void gdox_test_xenia_policy(void)
{
    test_runtime_catalog();
    test_default_policy();
    test_exact_title_selection();
    test_halo_3_policy();
    test_halo_4_policy();
    test_mass_effect_policy();
    test_identity_requires_all_fields();
}
