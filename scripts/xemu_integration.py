#!/usr/bin/env python3
"""Validate the maintained xemu source integration."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INTEGRATION = ROOT / "packaging/xemu/integration.json"
DEFAULT_RUNTIME_MANIFEST = ROOT / "packaging/runtime-manifest.json"
# Kept outside editable integration and runtime manifests. A target artifact is
# admitted only after its extracted executable has returned the exact reviewed
# capability response and its full package has passed storage tests.
REVIEWED_STORAGE_ARTIFACTS: dict[str, dict[str, str | int]] = {
    "x86_64-unknown-linux-gnu": {
        "artifact_sha256": (
            "0612c735d1aba98b19bccd2528bd0666625e3898659c6135587bc0c4e0a7da66"
        ),
        "artifact_size": 15436280,
        "executable_member": "runtime/xemu/AppDir/usr/bin/xemu",
        "executable_sha256": (
            "19d64e54d35bd8bd7a1eec0adf44a775b36a4ba2ae7fb52a0dd663197f1771eb"
        ),
        "executable_size": 15306400,
    },
    "x86_64-pc-windows-msvc": {
        "artifact_sha256": (
            "2d0e730a35474aad67a4cb3c092f70aacd3a5a4cbc1c45eb7cce11f3d16012bf"
        ),
        "artifact_size": 11621823,
        "executable_member": "runtime/xemu/xemu.exe",
        "executable_sha256": (
            "fcda4d37ce66236e9843d8624de9c9f87295964fe272b00067384476cacde596"
        ),
        "executable_size": 31763961,
    },
    "x86_64-apple-darwin": {
        "artifact_sha256": (
            "0f2c80aa50fd67f8ad2ab3348c64445de93f63f5edcc28666259a1e5f1d6b097"
        ),
        "artifact_size": 16605427,
        "executable_member": "runtime/xemu/xemu.app/Contents/MacOS/xemu",
        "executable_sha256": (
            "ed5008de0dfaa553d87043521a838164b6511050b5c06bf90e29dc7ead559801"
        ),
        "executable_size": 19104576,
    },
    "aarch64-apple-darwin": {
        "artifact_sha256": (
            "0f2c80aa50fd67f8ad2ab3348c64445de93f63f5edcc28666259a1e5f1d6b097"
        ),
        "artifact_size": 16605427,
        "executable_member": "runtime/xemu/xemu.app/Contents/MacOS/xemu",
        "executable_sha256": (
            "ed5008de0dfaa553d87043521a838164b6511050b5c06bf90e29dc7ead559801"
        ),
        "executable_size": 19104576,
    },
}
PATCH_LAYOUT = (
    (
        "0001-volatile-hdd-core.patch",
        (
            "block/xbox-volatile-hdd.c",
            "config_spec.yml",
            "include/block/xbox-volatile-hdd.h",
            "system/vl.c",
        ),
    ),
    (
        "0002-save-policy-security.patch",
        (
            "block/xbox-save-policy.c",
            "block/xbox-save-security.c",
            "include/block/xbox-save-policy.h",
            "include/block/xbox-save-security.h",
        ),
    ),
    (
        "0003-save-vault-migration.patch",
        (
            "block/xbox-save-migration.c",
            "block/xbox-save-receipt.c",
            "block/xbox-save-vault.c",
            "include/block/xbox-save-migration.h",
            "include/block/xbox-save-receipt.h",
            "include/block/xbox-save-vault.h",
            "include/ui/xemu-gdox-save-operation.h",
            "ui/xemu-gdox-save-operation.c",
        ),
    ),
    (
        "0004-managed-runtime.patch",
        (
            "include/ui/xemu-gdox-runtime.h",
            "ui/xemu-gdox-runtime.c",
            "ui/xemu.c",
        ),
    ),
    (
        "0005-performance-build-identity.patch",
        (
            "hw/xbox/nv2a/pgraph/gl/shaders.c",
            "scripts/xemu-version.sh",
        ),
    ),
    (
        "0006-tests-build-wiring.patch",
        (
            "block/meson.build",
            "meson.build",
            "tests/unit/meson.build",
            "tests/unit/xemu-gdox-test-security.h",
            "tests/unit/test-xbox-save-vault.c",
            "tests/unit/test-xbox-volatile-hdd.c",
            "tests/unit/test-xemu-gdox-runtime.c",
            "tests/unit/test-xemu-version.sh",
            "ui/meson.build",
        ),
    ),
)


def require_keys(value: dict, expected: set[str], context: str) -> None:
    actual = set(value)
    if actual != expected:
        raise RuntimeError(
            f"{context} keys differ: expected {sorted(expected)}, "
            f"found {sorted(actual)}"
        )


def load_object(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TypeError(f"{path} must contain a JSON object")
    return value


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_repo_file(
    definition: dict,
    expected_path: str,
    root: Path,
    context: str,
) -> None:
    if not isinstance(definition, dict):
        raise TypeError(f"{context} must be an object")
    require_keys(definition, {"path", "sha256", "size"}, context)
    if definition["path"] != expected_path:
        raise RuntimeError(f"{context} path differs")
    path = root / expected_path
    if not path.is_file() or path.is_symlink():
        raise RuntimeError(f"{context} is missing or unsafe: {path}")
    if path.stat().st_size != definition["size"]:
        raise RuntimeError(f"{context} size differs: {path}")
    if file_sha256(path) != definition["sha256"]:
        raise RuntimeError(f"{context} digest differs: {path}")


def patch_modified_paths(patch: Path, text: str) -> tuple[str, ...]:
    paths: list[str] = []
    for line in text.splitlines():
        if not line.startswith("diff --git "):
            continue
        fields = line.split()
        if (
            len(fields) != 4
            or not fields[2].startswith("a/")
            or not fields[3].startswith("b/")
            or fields[2][2:] != fields[3][2:]
        ):
            raise RuntimeError(f"{patch} contains an invalid diff header: {line}")
        paths.append(fields[2][2:])
    if not paths:
        raise RuntimeError(f"{patch} contains no file changes")
    return tuple(paths)


def validate_patch_series(patches: list[Path]) -> None:
    expected_names = tuple(name for name, _ in PATCH_LAYOUT)
    actual_names = tuple(patch.name for patch in patches)
    if actual_names != expected_names:
        raise RuntimeError(
            "xemu patch order differs: "
            f"expected {list(expected_names)}, found {list(actual_names)}"
        )

    texts: list[str] = []
    seen_paths: set[str] = set()
    for patch, (_, expected_paths) in zip(patches, PATCH_LAYOUT, strict=True):
        text = patch.read_text(encoding="utf-8")
        actual_paths = patch_modified_paths(patch, text)
        if actual_paths != expected_paths:
            raise RuntimeError(
                f"{patch} source ownership differs: "
                f"expected {list(expected_paths)}, found {list(actual_paths)}"
            )
        duplicate_paths = seen_paths.intersection(actual_paths)
        if duplicate_paths:
            raise RuntimeError(
                "xemu patch series changes a path more than once: "
                f"{sorted(duplicate_paths)}"
            )
        seen_paths.update(actual_paths)
        texts.append(text)

    text = "".join(texts)
    required_tokens = (
        "XBOX_VOLATILE_HDD_CACHE_START UINT64_C(0x00080000)",
        "XBOX_VOLATILE_HDD_CACHE_END   UINT64_C(0x8ca80000)",
        "XBOX_VOLATILE_HDD_MAX_PAGE_LIMIT 65536",
        "XBOX_VOLATILE_HDD_MAX_DIRTY_BYTES",
        "XBOX_VOLATILE_HDD_UNKNOWN_HOST_DIRTY_BYTES GiB",
        "g_try_malloc0(XBOX_VOLATILE_HDD_PAGE_SIZE)",
        "return -ENOMEM;",
        "return -ENOSPC;",
        "qemu_get_host_physmem()",
        "qemu_co_mutex_lock(&s->request_lock)",
        "bdrv_co_pread(bs->backing",
        "bdrv_co_preadv_part(bs->backing",
        "*nperm = BLK_PERM_CONSISTENT_READ;",
        "*nshared = BLK_PERM_ALL;",
        ".supports_backing           = true",
        "volatile_hard_disk",
        "default: true",
        "qemu_opt_set_bool(opts, BDRV_OPT_READ_ONLY, true",
        "drive->type == IF_IDE && drive->bus == 0 && drive->unit == 0",
        "if (g_config.perf.cache_shaders)",
        "--gdox-capabilities",
        "full_hdd_ram_cow",
        "backing_writes",
        "persistent_save_export",
        "persistent_save_scope",
        "persistent_save_format",
        "logical-files-v2",
        "hdd-config-v1",
        "positive-reviewed-paths-v1",
        "preserve-legacy-source",
        "persistent_save_atomic",
        "persistent_save_import_before_boot",
        "persistent_save_checkpoint",
        "legacy_hdd_migration",
        "migration_receipt",
        "migration_interruption_safe",
        "original-xbox-migration-receipt-v1.gdox",
        "source_removal_safe",
        "source_projection_complete",
        "source_removed",
        "vault_merge_has_file_ancestor",
        "Xbox HDD image must not depend on a backing image",
        "Could not exclusively lock Xbox HDD for removal",
        "Multiple Xbox HDD removal quarantines are valid",
        "migration_cleanup_empty_quarantines",
        'XEMU_GDOX_SAVE_VAULT_ARGUMENT "--gdox-save-vault"',
        "xbox_save_vault_checkpoint",
        "max_dirty_bytes",
        'XEMU_GDOX_RUNTIME_ARGUMENT "--gdox-runtime"',
        "MESA_SHADER_CACHE_DISABLE",
        "__GL_SHADER_DISK_CACHE",
        "else if (gdox_runtime)",
        'freopen("NUL", "w", stdout)',
        "g_config.perf.cache_shaders = false;",
        "g_config.display.setup_nvidia_profile = false;",
        'test "${SOURCE_DATE_EPOCH+x}" = x',
        "SOURCE_DATE_EPOCH must be a non-negative integer",
        'date -u -d "@$SOURCE_DATE_EPOCH"',
        'date -u -r "$SOURCE_DATE_EPOCH"',
        "2000-01-01T00:00:00Z",
        "/xbox-volatile-hdd/blank-fatx",
        "/xbox-volatile-hdd/full-disk-cow-and-boundaries",
        "/xbox-volatile-hdd/zero-discard-and-failure",
        "/xbox-volatile-hdd/concurrent-write-discard",
        "/xbox-volatile-hdd/cache-only-write-skips-save-scan",
        "/xbox-save-vault/source-identity-same-size-mutation",
        "/xbox-save-vault/migration-idempotent-and-differing",
        "/xbox-save-vault/real-raw-and-qcow2-migration",
        "/xemu-gdox-launch/disabled",
        "/xemu-gdox-launch/runtime",
        "qcrypto_init(errp)",
        "qemu_init_subsystems();",
    )
    for value in required_tokens:
        if value not in text:
            raise RuntimeError(f"xemu patch series is missing required source: {value}")
    forbidden = (
        ".is_filter",
        "volatile_cache_partitions",
        "bdrv_co_pwritev_part(bs->backing",
        "bdrv_co_pwrite_zeroes(bs->backing",
        "bdrv_co_pdiscard(bs->backing",
        "bdrv_co_flush(bs->backing",
    )
    for value in forbidden:
        if value in text:
            raise RuntimeError(
                f"xemu patch series contains a backing-write or obsolete path: {value}"
            )

    runtime_branch = text.index("else if (gdox_runtime)")
    null_output = text.index('freopen("NUL", "w", stdout)')
    ordinary_log = text.index('CreateFileA("xemu.log"')
    if not runtime_branch < null_output < ordinary_log:
        raise RuntimeError(
            "xemu patch series does not isolate GDOX output from the ordinary log path"
        )

    save_operation = text.rindex("+bool xemu_gdox_save_operation_run")
    next_file = text.find("\ndiff --git ", save_operation)
    save_source = text[save_operation : next_file if next_file >= 0 else None]
    crypto = save_source.index("+    if (qcrypto_init(errp) < 0)")
    subsystems = save_source.index("+    qemu_init_subsystems();")
    main_loop = save_source.index("+    if (qemu_init_main_loop(errp) < 0)")
    if not crypto < subsystems < main_loop:
        raise RuntimeError(
            f"{patch} does not initialize crypto, subsystems, and the main "
            "loop in the required order"
        )
    for partial_init in (
        "+    qemu_init_cpu_list();",
        "+    qemu_init_cpu_loop();",
        "+    qemu_init_main_loop_lock();",
        "+    qemu_mutex_lock_main_loop();",
        "+    bql_lock();",
        "+    bdrv_init();",
    ):
        if partial_init in save_source:
            raise RuntimeError(
                f"{patch} uses partial headless initialization: {partial_init}"
            )


def validate(
    integration_path: Path = DEFAULT_INTEGRATION,
    runtime_manifest_path: Path = DEFAULT_RUNTIME_MANIFEST,
    root: Path = ROOT,
) -> list[Path]:
    integration = load_object(integration_path)
    require_keys(
        integration,
        {
            "schema",
            "base",
            "patches",
            "build_recipe",
            "required_configuration",
            "capability_query",
            "runtime_mode",
            "storage_boundary",
            "runtime_artifacts_contain_patch",
            "runtime_capability_available",
        },
        str(integration_path),
    )
    if integration["schema"] != 4:
        raise RuntimeError("unsupported xemu integration schema")
    artifact_state = integration["runtime_artifacts_contain_patch"]
    capability_state = integration["runtime_capability_available"]
    if not isinstance(artifact_state, bool) or not isinstance(capability_state, bool):
        raise TypeError("xemu runtime integration state must be boolean")
    if artifact_state != capability_state:
        raise RuntimeError("xemu runtime artifact and capability states differ")

    base = integration["base"]
    if not isinstance(base, dict):
        raise TypeError("xemu integration base must be an object")
    require_keys(base, {"version", "source_sha256", "source_size"}, "base")

    runtime = load_object(runtime_manifest_path)
    xemu = runtime.get("xemu")
    if not isinstance(xemu, dict) or not isinstance(xemu.get("source"), dict):
        raise TypeError("runtime manifest has no xemu source provenance")
    source = xemu["source"]
    expected_base = {
        "version": xemu.get("version"),
        "source_sha256": source.get("sha256"),
        "source_size": source.get("size"),
    }
    if base != expected_base:
        raise RuntimeError("xemu integration base differs from runtime manifest")

    expected_build_recipe = {
        "source_date_epoch": 315532800,
        "canonical_source_root": "/usr/src/xemu-0.8.136",
        "configure_arguments": [
            "--target-list=i386-softmmu",
            "--disable-werror",
            "-Db_lto=true",
            "-Dx86_version=3",
        ],
        "extra_cflags": [
            "-DXBOX=1",
            "-Wno-error=redundant-decls",
            "-ffile-prefix-map=${SOURCE_ROOT}=/usr/src/xemu-0.8.136",
            "-fdebug-prefix-map=${SOURCE_ROOT}=/usr/src/xemu-0.8.136",
        ],
    }
    build_recipe = integration["build_recipe"]
    if not isinstance(build_recipe, dict):
        raise TypeError("xemu build recipe must be an object")
    require_keys(
        build_recipe,
        {*expected_build_recipe, "macos_universal"},
        "xemu build recipe",
    )
    for key, expected in expected_build_recipe.items():
        if build_recipe[key] != expected:
            raise RuntimeError(f"xemu build recipe {key} differs")

    macos = build_recipe["macos_universal"]
    if not isinstance(macos, dict):
        raise TypeError("xemu macOS build recipe must be an object")
    require_keys(
        macos,
        {
            "recipe",
            "packager",
            "source_archive",
            "dylibbundler",
            "toolchain",
            "xemu_version",
            "architectures",
            "archive_name",
            "audited_reference",
            "deterministic_output",
        },
        "xemu macOS build recipe",
    )
    validate_repo_file(
        macos["recipe"],
        "packaging/xemu/build_macos.py",
        root,
        "xemu macOS recipe",
    )
    validate_repo_file(
        macos["packager"],
        "scripts/package_xemu_macos_runtime.py",
        root,
        "xemu macOS packager",
    )
    expected_source_archive = {
        "root": "xemu-0.8.136",
        "sha256": expected_base["source_sha256"],
        "size": expected_base["source_size"],
    }
    if macos["source_archive"] != expected_source_archive:
        raise RuntimeError("xemu macOS source archive differs")
    expected_dylibbundler = {
        "source": {
            "root": "macdylibbundler-1.0.5",
            "sha256": (
                "13384ebe7ca841ec392ac49dc5e50b1470190466623fa0e5cd30f1c634858530"
            ),
            "size": 13101,
        },
        "executable": {
            "sha256": (
                "ac1172726dbaaad1809d36e6f86f7123398f8b6186685b4e6341d1e83da40a0d"
            ),
            "size": 203376,
        },
        "version_output": "dylibbundler 1.0.5",
    }
    if macos["dylibbundler"] != expected_dylibbundler:
        raise RuntimeError("xemu macOS dylibbundler contract differs")
    expected_toolchain = {
        "macos": "26.5.2",
        "xcode": "Xcode 26.6\nBuild version 17F113",
        "macos_sdk": "26.5",
        "clang": "Apple clang version 21.0.0 (clang-2100.1.1.101)",
        "git": "git version 2.55.0",
        "cmake": "cmake version 4.4.0",
        "ninja": "1.13.0.git.kitware.jobserver-pipe-1",
        "zip": (
            "This is Zip 3.0 (July 5th 2008), by Info-ZIP, with "
            "modifications by Apple Inc."
        ),
        "python": "3.14.6",
        "python_packages": {
            "PyYAML": "6.0.3",
            "requests": "2.32.5",
            "ninja": "1.13.0",
        },
    }
    if macos["toolchain"] != expected_toolchain:
        raise RuntimeError("xemu macOS toolchain contract differs")
    expected_architectures = {
        "arm64": {
            "minimum_macos": "14.0",
            "uuid": "4a183fcc-19de-54e5-a2db-8642ff10d4c0",
            "link_arguments": [
                "-arch",
                "arm64",
                "-target",
                "arm64-apple-macos14.0",
                "-isysroot",
                "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk",
                "-mmacosx-version-min=14.0",
            ],
            "rpath": "@executable_path/../Libraries/arm64/",
            "pre_normalization_count": 2,
        },
        "x86_64": {
            "minimum_macos": "12.7.5",
            "uuid": "816e9cee-8885-5192-b445-993d50a568ae",
            "link_arguments": [
                "-arch",
                "x86_64",
                "-target",
                "x86_64-apple-macos12.7.5",
                "-isysroot",
                "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk",
                "-mmacosx-version-min=12.7.5",
            ],
            "rpath": "@executable_path/../Libraries/x86_64/",
            "pre_normalization_count": 2,
        },
    }
    if macos["architectures"] != expected_architectures:
        raise RuntimeError("xemu macOS architecture contract differs")
    archive_name = "xemu-0.8.136-gdox-storage-macos-universal-adhoc.zip"
    if macos["xemu_version"] != "0.8.136" or macos["archive_name"] != archive_name:
        raise RuntimeError("xemu macOS artifact naming differs")
    expected_reference = {
        "archive_name": archive_name,
        "executable_sha256": (
            "fcc99a569bd80bdc62de65f4a034ab808015fc11732ba59595f9a2921b5e26bd"
        ),
        "executable_size": 24044560,
        "sha256": ("c1cc24b11db0aea46b59ebbe4e5330213a5cf57b8bc2f171e6dbef4589d0a32b"),
        "size": 17679098,
    }
    if macos["audited_reference"] != expected_reference:
        raise RuntimeError("xemu macOS audited reference differs")
    deterministic = macos["deterministic_output"]
    if not isinstance(deterministic, dict):
        raise TypeError("xemu macOS deterministic output must be an object")
    require_keys(
        deterministic,
        {
            "archive_name",
            "executable_sha256",
            "executable_size",
            "sha256",
            "size",
        },
        "xemu macOS deterministic output",
    )
    if (
        deterministic["archive_name"] != archive_name
        or not isinstance(deterministic["executable_sha256"], str)
        or len(deterministic["executable_sha256"]) != 64
        or not isinstance(deterministic["sha256"], str)
        or len(deterministic["sha256"]) != 64
        or not isinstance(deterministic["executable_size"], int)
        or deterministic["executable_size"] <= 0
        or not isinstance(deterministic["size"], int)
        or deterministic["size"] <= 0
    ):
        raise RuntimeError("xemu macOS deterministic output is invalid")

    configuration = integration["required_configuration"]
    expected_configuration = {
        "sys.volatile_hard_disk": True,
        "perf.cache_shaders": False,
    }
    if configuration != expected_configuration:
        raise RuntimeError("xemu integration configuration is not fail-closed")

    expected_capability = {
        "argument": "--gdox-capabilities",
        "response": {
            "schema": 3,
            "runtime": "xemu",
            "storage": {
                "full_hdd_ram_cow": True,
                "backing_writes": False,
                "persistent_save_export": True,
                "persistent_save_scope": (r"hdd-config-v1+E:\UDATA+reviewed-E:\TDATA"),
                "persistent_save_format": "logical-files-v2",
                "persistent_hdd_config_format": "hdd-config-v1",
                "persistent_hdd_config_bytes": 524288,
                "tdata_policy": "positive-reviewed-paths-v1",
                "tdata_policy_revision": 1,
                "unknown_tdata": "preserve-legacy-source",
                "persistent_save_atomic": True,
                "persistent_save_import_before_boot": True,
                "persistent_save_checkpoint": ("guest-flush-and-orderly-shutdown"),
                "legacy_hdd_migration": True,
                "migration_receipt": True,
                "migration_interruption_safe": True,
                "max_dirty_bytes": 4294967296,
            },
        },
    }
    if integration["capability_query"] != expected_capability:
        raise RuntimeError("xemu capability query contract differs")

    expected_runtime_mode = {
        "argument": "--gdox-runtime",
        "windows_detached_output": "NUL",
        "xemu_shader_cache": "disabled",
        "nvidia_profile_write": False,
        "driver_cache_environment": {
            "MESA_SHADER_CACHE_DISABLE": "1",
            "__GL_SHADER_DISK_CACHE": "0",
        },
    }
    if integration["runtime_mode"] != expected_runtime_mode:
        raise RuntimeError("xemu GDOX runtime mode contract differs")

    expected_boundary = {
        "mode": "full-hdd-ram-cow-v1",
        "base_image": "clean-immutable-template",
        "durable_game_content": False,
        "persistent_save_export": {
            "scope": r"hdd-config-v1+E:\UDATA+reviewed-E:\TDATA",
            "format": "logical-files-v2",
            "hdd_config_format": "hdd-config-v1",
            "hdd_config_bytes": 524288,
            "tdata_policy": "positive-reviewed-paths-v1",
            "tdata_policy_revision": 1,
            "unknown_tdata": "preserve-legacy-source",
            "atomic": True,
            "import_before_boot": True,
            "checkpoint": "guest-flush-and-orderly-shutdown",
        },
        "legacy_hdd_migration": True,
        "migration_receipt": True,
        "migration_interruption_safe": True,
        "memory": {
            "page_size": 65536,
            "maximum_dirty_bytes": 4294967296,
            "host_fraction_divisor": 4,
            "unknown_host_dirty_bytes": 1073741824,
            "lossy_eviction": False,
            "exhaustion_error": "ENOSPC",
        },
        "save_projector": {
            "durable_allowlist": [
                "HDD:0x00000000-0x0007ffff",
                r"E:\UDATA",
                r"reviewed-E:\TDATA",
            ],
            "unclassified_tdata": "inventoried-and-preserve-legacy-source",
            "always_transient": [
                "F:\\",
                "G:\\",
                "X:\\",
                "Y:\\",
                "Z:\\",
            ],
        },
    }
    if integration["storage_boundary"] != expected_boundary:
        raise RuntimeError("xemu storage boundary contract differs")

    entries = integration["patches"]
    if not isinstance(entries, list) or not entries:
        raise RuntimeError("xemu integration patch series is empty")

    patches: list[Path] = []
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise TypeError(f"patch entry {index} must be an object")
        require_keys(entry, {"path", "sha256", "size"}, f"patch entry {index}")
        relative = Path(entry["path"])
        expected_parent = Path("packaging/xemu/patches")
        if relative.is_absolute() or relative.parent != expected_parent:
            raise RuntimeError(f"invalid xemu patch path: {relative}")
        patch = root / relative
        if not patch.is_file():
            raise RuntimeError(f"xemu patch is missing: {patch}")
        if patch.stat().st_size != entry["size"]:
            raise RuntimeError(f"xemu patch size differs: {patch}")
        if file_sha256(patch) != entry["sha256"]:
            raise RuntimeError(f"xemu patch digest differs: {patch}")
        patches.append(patch)
    validate_patch_series(patches)
    return patches


def require_publishable(
    integration_path: Path = DEFAULT_INTEGRATION,
    runtime_manifest_path: Path = DEFAULT_RUNTIME_MANIFEST,
    root: Path = ROOT,
    target: str | None = None,
) -> None:
    validate(integration_path, runtime_manifest_path, root)
    integration = load_object(integration_path)
    storage = integration["capability_query"]["response"]["storage"]
    if (
        integration["runtime_artifacts_contain_patch"] is not True
        or integration["runtime_capability_available"] is not True
        or storage["persistent_save_export"] is not True
    ):
        raise SystemExit(
            "xemu runtime artifacts do not provide the required volatile-HDD "
            "and save-only persistence contract"
        )
    runtime = load_object(runtime_manifest_path)
    targets = runtime.get("xemu", {}).get("targets", {})
    selected_targets = targets if target is None else {target: targets.get(target)}
    reviewed_executables: dict[str, dict] = {}
    for field in ("embedded_privacy_files", "embedded_build_path_files"):
        for item in runtime.get("xemu", {}).get(field, []):
            if not isinstance(item, dict) or not isinstance(item.get("member"), str):
                continue
            member = item["member"]
            existing = reviewed_executables.get(member)
            if existing is not None and existing != item:
                raise SystemExit(
                    f"xemu executable identity conflicts across manifest lists: {member}"
                )
            reviewed_executables[member] = item
    for target_name, asset in selected_targets.items():
        if not isinstance(asset, dict):
            raise SystemExit(f"no xemu runtime artifact is defined for {target_name}")
        reviewed = REVIEWED_STORAGE_ARTIFACTS.get(target_name)
        executable = (
            reviewed_executables.get(reviewed.get("executable_member"))
            if isinstance(reviewed, dict)
            else None
        )
        if (
            not isinstance(reviewed, dict)
            or reviewed.get("artifact_sha256") != asset.get("sha256")
            or reviewed.get("artifact_size") != asset.get("size")
            or not isinstance(executable, dict)
            or reviewed.get("executable_sha256") != executable.get("sha256")
            or reviewed.get("executable_size") != executable.get("size")
        ):
            raise SystemExit(
                f"xemu runtime artifact for {target_name} has not passed "
                "executable storage capability verification"
            )


def check_source(source_root: Path, patches: list[Path]) -> None:
    with tempfile.TemporaryDirectory(prefix="gdox-xemu-index-") as temporary:
        environment = os.environ | {"GIT_INDEX_FILE": str(Path(temporary) / "index")}
        subprocess.run(
            ["git", "-C", str(source_root), "read-tree", "HEAD"],
            check=True,
            capture_output=True,
            env=environment,
        )
        for patch in patches:
            result = subprocess.run(
                [
                    "git",
                    "-C",
                    str(source_root),
                    "apply",
                    "--cached",
                    "--whitespace=error-all",
                    str(patch),
                ],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )
            if result.returncode != 0:
                detail = result.stderr.strip() or "Git returned no error detail"
                raise RuntimeError(f"{patch.name} does not apply: {detail}")


def apply_source(source_root: Path, patches: list[Path]) -> None:
    status = subprocess.run(
        ["git", "-C", str(source_root), "status", "--porcelain"],
        check=True,
        capture_output=True,
    ).stdout
    if status:
        raise RuntimeError(f"xemu source tree is not clean: {source_root}")
    check_source(source_root, patches)
    for patch in patches:
        subprocess.run(
            [
                "git",
                "-C",
                str(source_root),
                "apply",
                "--whitespace=error-all",
                str(patch),
            ],
            check=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        help="also verify the series against a clean pinned xemu source tree",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="apply the validated series to the clean source tree",
    )
    args = parser.parse_args()
    patches = validate()
    if args.apply and not args.source_root:
        parser.error("--apply requires --source-root")
    if args.source_root:
        operation = apply_source if args.apply else check_source
        operation(args.source_root.resolve(), patches)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"xemu integration validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
