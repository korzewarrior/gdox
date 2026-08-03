#!/usr/bin/env python3
"""Generate the compiled Xenia policy table from reviewed compatibility data."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from xenia_compatibility import (
    DEFAULT_MANIFEST,
    CompatibilityError,
    XeniaCompatibility,
    XeniaPolicy,
    XeniaSettings,
    load_compatibility_manifest,
)
from xenia_distribution import (
    LINUX_TARGET,
    WINDOWS_TARGET,
    has_reviewed_managed_session_capability,
    has_reviewed_save_only_capability,
    validate_runtime_manifest,
)

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = ROOT / "src" / "core" / "xenia_policy.c"
DEFAULT_RUNTIME_MANIFEST = ROOT / "packaging" / "runtime-manifest.json"


def _reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise CompatibilityError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _boolean(value: bool) -> str:
    return "true" if value else "false"


def _settings_values(
    settings: XeniaSettings,
) -> tuple[tuple[str, str], ...]:
    occlusion = {
        "default": "GDOX_XENIA_OCCLUSION_DEFAULT",
        "strict": "GDOX_XENIA_OCCLUSION_STRICT",
        "fast-alt": "GDOX_XENIA_OCCLUSION_FAST_ALT",
    }[settings.occlusion_query]
    readback = {
        "none": "GDOX_XENIA_READBACK_NONE",
        "fast": "GDOX_XENIA_READBACK_FAST",
        "full": "GDOX_XENIA_READBACK_FULL",
    }[settings.readback_resolve]
    return (
        ("allow_tearing", _boolean(settings.allow_tearing)),
        ("vsync", _boolean(settings.vsync)),
        ("max_queued_frames", f"{settings.max_queued_frames}U"),
        (
            "allow_invalid_fetch_constants",
            _boolean(settings.allow_invalid_fetch_constants),
        ),
        ("occlusion_query", occlusion),
        (
            "occlusion_query_saturation_basis_points",
            f"{settings.occlusion_query_saturation_basis_points}U",
        ),
        ("readback_resolve", readback),
        ("protect_zero", _boolean(settings.protect_zero)),
        ("new_xma_decoder", _boolean(settings.xma_decoder == "new")),
        (
            "use_dedicated_xma_thread",
            _boolean(settings.use_dedicated_xma_thread),
        ),
        (
            "async_shader_compilation",
            _boolean(settings.async_shader_compilation),
        ),
        (
            "use_handheld_custom_resolution",
            _boolean(settings.use_handheld_custom_resolution),
        ),
    )


def _policy_lines(
    policy: XeniaPolicy,
    runtime_index: dict[str, int],
    indent: str,
) -> list[str]:
    values = _settings_values(policy.settings)
    patch_set = {
        "none": "GDOX_XENIA_PATCH_SET_NONE",
        "mass-effect-world-rendering-v1": (
            "GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1"
        ),
    }[policy.patch_set]
    return [
        f"{indent}{{",
        f"{indent}    &runtimes[{runtime_index[policy.revision]}],",
        f'{indent}    "{policy.launch_module}",',
        f"{indent}    {patch_set},",
        f"{indent}    {{",
        *(
            f"{indent}        .{name} = {value},"
            for name, value in values
        ),
        f"{indent}    }},",
        f"{indent}}}",
    ]


def _runtime_values(
    revision: str,
    asset: dict,
    target: str,
) -> tuple[str, str, int, str, bool, bool, bool, bool, bool, bool]:
    format_name = asset["format"]
    if target == LINUX_TARGET and format_name == "linux-appimage":
        backend = "GDOX_XENIA_GPU_VULKAN"
        requires_proton = False
    elif target == LINUX_TARGET and format_name == "windows-zip-proton":
        backend = "GDOX_XENIA_GPU_D3D12"
        requires_proton = True
    else:
        backend = "GDOX_XENIA_GPU_D3D12"
        requires_proton = False
    required_performance_options = {
        "async_shader_compilation",
        "flush_log",
        "framerate_limit",
        "host_present_from_non_ui_thread",
        "ignore_thread_affinities",
        "log_level",
    }
    if backend == "GDOX_XENIA_GPU_VULKAN":
        required_performance_options.update(
            {
                "vulkan_allow_present_mode_fifo_relaxed",
                "vulkan_pipeline_creation_threads",
            }
        )
    else:
        required_performance_options.add("d3d12_pipeline_creation_threads")
    managed_options = set(asset["managed_options"])
    required_storage_options = {
        "storage_root",
        "content_root",
        "cache_root",
        "store_shaders",
        "disable_instruction_infocache",
        "gdox_persistent_content_saves_only",
        "log_file",
        "mount_cache",
        "mount_scratch",
    }
    supports_managed_disclaimer_acknowledgement = (
        "gdox_disclaimer_acknowledged" in managed_options
        and has_reviewed_managed_session_capability(revision, target, asset)
    )
    return (
        asset["executable"],
        asset["executable_sha256"],
        asset["executable_size"],
        backend,
        requires_proton,
        "apu_max_queued_frames" in asset["managed_options"],
        "custom_internal_display_resolution" in asset["managed_options"],
        required_performance_options.issubset(managed_options),
        required_storage_options.issubset(managed_options)
        and has_reviewed_save_only_capability(revision, target, asset),
        asset["disc_transport"] == "gdox-private-nbd-v1",
        supports_managed_disclaimer_acknowledgement,
    )


def _runtime_target_lines(
    revision: str,
    asset: dict,
    target: str,
) -> list[str]:
    (
        name,
        digest,
        size,
        backend,
        requires_proton,
        supports_queued_frames,
        supports_custom_resolution,
        supports_performance_profile,
        supports_storage_isolation,
        supports_private_nbd,
        supports_managed_disclaimer_acknowledgement,
    ) = _runtime_values(revision, asset, target)
    return [
        f'        "{name}",',
        f'        "{digest}",',
        f"        UINT64_C({size}),",
        f"        {backend},",
        f"        {_boolean(requires_proton)},",
        f"        {_boolean(supports_queued_frames)},",
        f"        {_boolean(supports_custom_resolution)},",
        f"        {_boolean(supports_performance_profile)},",
        f"        {_boolean(supports_storage_isolation)},",
        f"        {_boolean(supports_private_nbd)},",
        f"        {_boolean(supports_managed_disclaimer_acknowledgement)},",
    ]


def _unsupported_runtime_lines() -> list[str]:
    return [
        '        "",',
        '        "",',
        "        UINT64_C(0),",
        "        GDOX_XENIA_GPU_D3D12,",
        "        false,",
        "        false,",
        "        false,",
        "        false,",
        "        false,",
        "        false,",
        "        false,",
    ]


def render(compatibility: XeniaCompatibility, manifest: dict) -> str:
    runtime_index = {
        runtime.revision: index for index, runtime in enumerate(compatibility.runtimes)
    }
    lines = [
        "/* Generated by scripts/generate_xenia_policy.py. Do not edit. */",
        '#include "gdox/xenia_policy.h"',
        "",
        "#include <string.h>",
        "",
        "typedef struct gdox_xenia_title_policy_entry {",
        "    gdox_xenia_title_identity identity;",
        "    gdox_xenia_launch_policy policy;",
        "} gdox_xenia_title_policy_entry;",
        "",
        "static const gdox_xenia_runtime runtimes[] = {",
    ]
    for runtime in compatibility.runtimes:
        targets = manifest["xenia"]["revisions"][runtime.revision]["targets"]
        lines.extend(
            [
                "    {",
                f'        "{runtime.revision}",',
                "#if defined(GDOX_XENIA_CATALOG_WINDOWS)",
            ]
        )
        lines.extend(
            _runtime_target_lines(
                runtime.revision,
                targets[WINDOWS_TARGET],
                WINDOWS_TARGET,
            )
        )
        lines.append("#elif defined(GDOX_XENIA_CATALOG_LINUX)")
        lines.extend(
            _runtime_target_lines(
                runtime.revision,
                targets[LINUX_TARGET],
                LINUX_TARGET,
            )
        )
        lines.append("#else")
        lines.extend(_unsupported_runtime_lines())
        lines.extend(["#endif", "    },"])
    lines.extend(
        [
            "};",
            "",
            "static const gdox_xenia_launch_policy default_policy =",
        ]
    )
    default_lines = _policy_lines(compatibility.default, runtime_index, "    ")
    default_lines[-1] += ";"
    lines.extend(default_lines)
    lines.extend(
        [
            "",
            "static const gdox_xenia_title_policy_entry title_policies[] = {",
        ]
    )
    for title in compatibility.titles:
        identity = (
            "{UINT32_C(0x"
            f"{title.title_id:08x}), UINT32_C(0x{title.media_id:08x}), "
            f"{title.disc_number}U, {title.disc_count}U" + "}"
        )
        lines.extend(["    {", f"        {identity},"])
        policy_lines = _policy_lines(title, runtime_index, "        ")
        policy_lines[-1] += ","
        lines.extend(policy_lines)
        lines.append("    },")
    lines.extend(
        [
            "};",
            "",
            "const gdox_xenia_launch_policy *gdox_xenia_default_policy(void)",
            "{",
            "    return &default_policy;",
            "}",
            "",
            "const gdox_xenia_launch_policy *gdox_xenia_select_policy(",
            "    const gdox_xenia_title_identity *identity",
            ")",
            "{",
            "    size_t index;",
            "",
            "    if (identity == NULL) {",
            "        return &default_policy;",
            "    }",
            "    for (index = 0U;",
            "         index < sizeof(title_policies) / sizeof(title_policies[0]);",
            "         ++index) {",
            "        const gdox_xenia_title_identity *candidate =",
            "            &title_policies[index].identity;",
            "        if (candidate->title_id == identity->title_id",
            "            && candidate->media_id == identity->media_id",
            "            && candidate->disc_number == identity->disc_number",
            "            && candidate->disc_count == identity->disc_count) {",
            "            return &title_policies[index].policy;",
            "        }",
            "    }",
            "    return &default_policy;",
            "}",
            "",
            "size_t gdox_xenia_runtime_count(void)",
            "{",
            "    return sizeof(runtimes) / sizeof(runtimes[0]);",
            "}",
            "",
            "const gdox_xenia_runtime *gdox_xenia_runtime_at(size_t index)",
            "{",
            "    if (index >= gdox_xenia_runtime_count()) {",
            "        return NULL;",
            "    }",
            "    return &runtimes[index];",
            "}",
            "",
            "const gdox_xenia_runtime *gdox_xenia_runtime_by_revision(",
            "    const char *revision",
            ")",
            "{",
            "    size_t index;",
            "",
            "    if (revision == NULL || revision[0] == '\\0') {",
            "        return NULL;",
            "    }",
            "    for (index = 0U; index < gdox_xenia_runtime_count(); ++index) {",
            "        if (strcmp(runtimes[index].revision, revision) == 0) {",
            "            return &runtimes[index];",
            "        }",
            "    }",
            "    return NULL;",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--runtime-manifest", type=Path, default=DEFAULT_RUNTIME_MANIFEST
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the output is absent or differs from generated content",
    )
    args = parser.parse_args()
    try:
        compatibility = load_compatibility_manifest(args.manifest)
        runtime_manifest = json.loads(
            args.runtime_manifest.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
        )
        validate_runtime_manifest(runtime_manifest)
        output = render(compatibility, runtime_manifest)
    except (CompatibilityError, OSError, json.JSONDecodeError) as error:
        parser.exit(1, f"{error}\n")

    if args.check:
        try:
            current = args.output.read_text(encoding="utf-8")
        except OSError as error:
            parser.exit(1, f"{args.output}: {error}\n")
        if current != output:
            parser.exit(
                1,
                f"{args.output}: generated policy is stale; "
                "run scripts/generate_xenia_policy.py\n",
            )
        print(f"verified {args.output}")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
