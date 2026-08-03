"""Regression tests for the maintained xemu source integration."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))

from xemu_integration import (
    REVIEWED_STORAGE_ARTIFACTS,
    require_publishable,
    validate,
)


class XemuIntegrationTest(unittest.TestCase):
    def test_gameplay_launch_uses_the_private_runtime_contract(self) -> None:
        for source_name in (
            "src/platform/emulator_posix.c",
            "src/platform/emulator_windows.c",
        ):
            source = (ROOT / source_name).read_text(encoding="utf-8")
            self.assertIn("--gdox-runtime", source)
            self.assertIn("--gdox-save-vault", source)

    def test_candidate_series_is_pinned_and_audited(self) -> None:
        patches = validate()
        self.assertEqual(
            [patch.name for patch in patches],
            [
                "0001-volatile-hdd-core.patch",
                "0002-save-policy-security.patch",
                "0003-save-vault-migration.patch",
                "0004-managed-runtime.patch",
                "0005-performance-build-identity.patch",
                "0006-tests-build-wiring.patch",
            ],
        )

    def test_candidate_series_has_semantic_source_boundaries(self) -> None:
        patches = {
            patch.name: patch.read_text(encoding="utf-8") for patch in validate()
        }
        representatives = {
            "0001-volatile-hdd-core.patch": "block/xbox-volatile-hdd.c",
            "0002-save-policy-security.patch": "block/xbox-save-security.c",
            "0003-save-vault-migration.patch": "block/xbox-save-migration.c",
            "0004-managed-runtime.patch": "ui/xemu-gdox-runtime.c",
            "0005-performance-build-identity.patch": "scripts/xemu-version.sh",
            "0006-tests-build-wiring.patch": "tests/unit/test-xbox-volatile-hdd.c",
        }
        for patch_name, source_path in representatives.items():
            with self.subTest(patch=patch_name):
                self.assertIn(
                    f"diff --git a/{source_path} b/{source_path}",
                    patches[patch_name],
                )

    def test_changed_patch_digest_fails_closed(self) -> None:
        integration = json.loads(
            (ROOT / "packaging/xemu/integration.json").read_text(encoding="utf-8")
        )
        integration["patches"][0]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "integration.json"
            path.write_text(json.dumps(integration), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "digest differs"):
                validate(integration_path=path)

    def test_reordered_patch_series_fails_closed(self) -> None:
        integration = json.loads(
            (ROOT / "packaging/xemu/integration.json").read_text(encoding="utf-8")
        )
        integration["patches"][0], integration["patches"][1] = (
            integration["patches"][1],
            integration["patches"][0],
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "integration.json"
            path.write_text(json.dumps(integration), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "patch order differs"):
                validate(integration_path=path)

    def test_windows_migration_reopens_the_held_source_safely(self) -> None:
        source = (
            ROOT / "packaging/xemu/patches/0003-save-vault-migration.patch"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "+        FILE_SHARE_READ | FILE_SHARE_DELETE,",
            source,
        )
        self.assertIn("+        source_info.nNumberOfLinks == 1 &&", source)
        self.assertIn("+        path_info.nNumberOfLinks == 1 &&", source)
        self.assertIn(
            "+        source_info.dwVolumeSerialNumber == "
            "path_info.dwVolumeSerialNumber &&",
            source,
        )
        self.assertIn(
            "+        source_info.nFileIndexHigh == path_info.nFileIndexHigh &&",
            source,
        )
        self.assertIn(
            "+        source_info.nFileIndexLow == path_info.nFileIndexLow;",
            source,
        )

    def test_current_runtime_is_claimed_only_after_review(self) -> None:
        integration = json.loads(
            (ROOT / "packaging/xemu/integration.json").read_text(encoding="utf-8")
        )
        self.assertTrue(integration["runtime_artifacts_contain_patch"])
        self.assertEqual(
            integration["required_configuration"],
            {
                "sys.volatile_hard_disk": True,
                "perf.cache_shaders": False,
            },
        )

        for target in (
            "x86_64-unknown-linux-gnu",
            "x86_64-pc-windows-msvc",
            "x86_64-apple-darwin",
            "aarch64-apple-darwin",
        ):
            require_publishable(target=target)
        self.assertTrue(integration["runtime_capability_available"])
        self.assertEqual(
            integration["capability_query"]["response"]["storage"],
            {
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
        )

        self.assertEqual(
            integration["runtime_mode"],
            {
                "argument": "--gdox-runtime",
                "windows_detached_output": "NUL",
                "xemu_shader_cache": "disabled",
                "nvidia_profile_write": False,
                "driver_cache_environment": {
                    "MESA_SHADER_CACHE_DISABLE": "1",
                    "__GL_SHADER_DISK_CACHE": "0",
                },
            },
        )
        self.assertEqual(
            integration["storage_boundary"]["memory"],
            {
                "page_size": 65536,
                "maximum_dirty_bytes": 4294967296,
                "host_fraction_divisor": 4,
                "unknown_host_dirty_bytes": 1073741824,
                "lossy_eviction": False,
                "exhaustion_error": "ENOSPC",
            },
        )
        self.assertEqual(
            integration["storage_boundary"]["save_projector"],
            {
                "durable_allowlist": [
                    "HDD:0x00000000-0x0007ffff",
                    r"E:\UDATA",
                    r"reviewed-E:\TDATA",
                ],
                "unclassified_tdata": ("inventoried-and-preserve-legacy-source"),
                "always_transient": [
                    "F:\\",
                    "G:\\",
                    "X:\\",
                    "Y:\\",
                    "Z:\\",
                ],
            },
        )

    def test_reviewed_candidate_identities_are_recorded(self) -> None:
        self.assertEqual(
            REVIEWED_STORAGE_ARTIFACTS,
            {
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
                    "executable_member": (
                        "runtime/xemu/xemu.app/Contents/MacOS/xemu"
                    ),
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
                    "executable_member": (
                        "runtime/xemu/xemu.app/Contents/MacOS/xemu"
                    ),
                    "executable_sha256": (
                        "ed5008de0dfaa553d87043521a838164b6511050b5c06bf90e29dc7ead559801"
                    ),
                    "executable_size": 19104576,
                },
            },
        )

    def test_macos_universal_recipe_is_pinned_and_fail_closed(self) -> None:
        integration = json.loads(
            (ROOT / "packaging/xemu/integration.json").read_text(encoding="utf-8")
        )
        macos = integration["build_recipe"]["macos_universal"]
        self.assertEqual(
            macos["architectures"],
            {
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
            },
        )
        self.assertEqual(
            macos["audited_reference"]["sha256"],
            "c1cc24b11db0aea46b59ebbe4e5330213a5cf57b8bc2f171e6dbef4589d0a32b",
        )
        self.assertEqual(
            macos["audited_reference"]["executable_sha256"],
            "fcc99a569bd80bdc62de65f4a034ab808015fc11732ba59595f9a2921b5e26bd",
        )

        integration["build_recipe"]["macos_universal"]["recipe"]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "integration.json"
            path.write_text(json.dumps(integration), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "recipe digest differs"):
                validate(integration_path=path)

    def test_capability_metadata_cannot_authorize_old_artifact(self) -> None:
        integration = json.loads(
            (ROOT / "packaging/xemu/integration.json").read_text(encoding="utf-8")
        )
        integration["runtime_artifacts_contain_patch"] = True
        integration["runtime_capability_available"] = True
        integration["capability_query"]["response"]["storage"][
            "persistent_save_export"
        ] = True
        runtime = json.loads(
            (ROOT / "packaging/runtime-manifest.json").read_text(encoding="utf-8")
        )
        runtime["xemu"]["targets"]["x86_64-unknown-linux-gnu"]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as temporary:
            integration_path = Path(temporary) / "integration.json"
            integration_path.write_text(json.dumps(integration), encoding="utf-8")
            runtime_path = Path(temporary) / "runtime-manifest.json"
            runtime_path.write_text(json.dumps(runtime), encoding="utf-8")
            with (
                patch("xemu_integration.validate", return_value=[]),
                self.assertRaisesRegex(SystemExit, "executable storage capability"),
            ):
                require_publishable(
                    integration_path=integration_path,
                    runtime_manifest_path=runtime_path,
                    target="x86_64-unknown-linux-gnu",
                )

        runtime = json.loads(
            (ROOT / "packaging/runtime-manifest.json").read_text(encoding="utf-8")
        )
        executable = dict(runtime["xemu"]["embedded_build_path_files"][1])
        executable["sha256"] = "0" * 64
        runtime["xemu"]["embedded_privacy_files"].append(executable)
        with tempfile.TemporaryDirectory() as temporary:
            runtime_path = Path(temporary) / "runtime-manifest.json"
            runtime_path.write_text(json.dumps(runtime), encoding="utf-8")
            with (
                patch("xemu_integration.validate", return_value=[]),
                self.assertRaisesRegex(SystemExit, "identity conflicts"),
            ):
                require_publishable(
                    runtime_manifest_path=runtime_path,
                    target="aarch64-apple-darwin",
                )


if __name__ == "__main__":
    unittest.main()
