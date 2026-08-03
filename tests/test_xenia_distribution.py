"""Regression tests for fail-closed Xenia distribution provenance."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from fetch_runtime import (
    load_manifest,
)
from fetch_runtime import (
    validate_manifest as validate_complete_runtime_manifest,
)
from generate_xenia_policy import _runtime_values
from package_release import (
    fetch_runtime,
    is_link_or_reparse_point,
    macos_signing_commands,
    sign_macos,
    verify_macos_embedded_runtime,
)
from package_xenia_windows_runtime import write_archive
from private_candidate_runtime import (
    bundle_private_candidate_runtime,
    stage_private_xemu_candidate,
)
from release_audit_provenance import (
    XEMU_BUILD_PATH_FILES,
    XEMU_NOTICE_FILES,
    XEMU_PRIVACY_FILES,
    XENIA_RUNTIME_FILES,
)
from xenia_distribution import (
    LINUX_TARGET,
    WINDOWS_TARGET,
    has_reviewed_managed_session_capability,
    has_reviewed_save_only_capability,
    load_injection_environment_policy,
    render_injection_environment_policy,
    require_publishable,
    validate_archive,
    validate_runtime_manifest,
    verify_storage_capability,
)


class XeniaDistributionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = load_manifest()
        self.xemu_integration = json.loads(
            (ROOT / "packaging" / "xemu" / "integration.json").read_text(
                encoding="utf-8"
            )
        )

    def test_windows_reparse_points_are_treated_as_links(self) -> None:
        path = Mock()
        path.is_symlink.return_value = False
        with patch(
            "package_release.stat.FILE_ATTRIBUTE_REPARSE_POINT",
            0x400,
            create=True,
        ):
            path.lstat.return_value = Mock(st_file_attributes=0x400)
            self.assertTrue(is_link_or_reparse_point(path))

            path.lstat.return_value = Mock(st_file_attributes=0)
            self.assertFalse(is_link_or_reparse_point(path))

    def windows_asset(self, revision: str = "72ce13097") -> dict:
        return self.manifest["xenia"]["revisions"][revision]["targets"][
            WINDOWS_TARGET
        ]

    def make_xemu_candidate_fixture(
        self,
        root: Path,
        *,
        source_notice: str = (
            "This bundle contains the reviewed GDOX-patched xemu executable.\n"
        ),
        side_effect: bool = False,
    ) -> tuple[Path, Path, dict]:
        response = json.dumps(
            self.xemu_integration["capability_query"]["response"],
            separators=(",", ":"),
        )
        candidate = root / "candidate-xemu"
        candidate.write_text(
            "#!/bin/sh\n"
            + ('mkdir -p "$HOME"\n' if side_effect else "")
            + f"printf '%s\\n' '{response}'\n",
            encoding="utf-8",
        )
        candidate.chmod(0o700)
        runtime = root / "runtime"
        bundled = runtime / "xemu" / "AppDir" / "usr" / "bin" / "xemu"
        bundled.parent.mkdir(parents=True)
        bundled.write_bytes(b"upstream xemu")
        launcher = runtime / "xemu" / "xemu"
        launcher.write_text(
            "#!/bin/sh\n"
            'exec "$(dirname "$0")/AppDir/usr/bin/xemu" "$@"\n',
            encoding="utf-8",
        )
        launcher.chmod(0o700)
        (runtime / "SOURCE.md").write_text(
            source_notice,
            encoding="utf-8",
        )
        manifest = {
            "xemu": {
                "version": "test",
                "embedded_privacy_files": [
                    {
                        "member": "runtime/xemu/AppDir/usr/bin/xemu",
                        "size": candidate.stat().st_size,
                        "sha256": hashlib.sha256(
                            candidate.read_bytes()
                        ).hexdigest(),
                    }
                ],
            }
        }
        return candidate, runtime, manifest

    def test_public_runtime_fetch_uses_only_the_public_bundle_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary, patch(
            "package_release.run"
        ) as run:
            destination = Path(temporary) / "runtime"
            fetch_runtime(
                "x86_64-steamdeck-linux-gnu",
                destination,
            )

        command = run.call_args.args[0]
        self.assertEqual(command[2], "bundle")
        self.assertEqual(
            command[command.index("--target") + 1],
            LINUX_TARGET,
        )
        self.assertFalse(any("candidate" in argument for argument in command))

    def test_private_runtime_accepts_only_the_reviewed_candidate_set(self) -> None:
        candidate_manifest = deepcopy(self.manifest)
        for definition in candidate_manifest["xenia"]["revisions"].values():
            asset = definition["targets"].get(WINDOWS_TARGET)
            if asset is not None:
                asset["release_state"] = "candidate-only"
                asset["url"] = None
        expected = {
            asset["archive_name"]
            for definition in candidate_manifest["xenia"]["revisions"].values()
            if (
                asset := definition["targets"].get(WINDOWS_TARGET)
            ) is not None
            and asset["release_state"] == "candidate-only"
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidates = root / "candidates"
            candidates.mkdir()
            for name in expected:
                (candidates / name).write_bytes(b"reviewed fixture")

            with patch(
                "private_candidate_runtime.load_manifest",
                return_value=candidate_manifest,
            ), patch(
                "private_candidate_runtime.stage_runtime_bundle"
            ) as stage_bundle:
                bundle_private_candidate_runtime(
                    WINDOWS_TARGET,
                    root / "runtime",
                    root / "cache",
                    candidates,
                )
            self.assertEqual(stage_bundle.call_count, 1)

            (candidates / "unreviewed.zip").write_bytes(b"extra")
            with patch(
                "private_candidate_runtime.load_manifest",
                return_value=candidate_manifest,
            ), self.assertRaisesRegex(
                SystemExit,
                "only the exact reviewed archives",
            ):
                bundle_private_candidate_runtime(
                    WINDOWS_TARGET,
                    root / "runtime",
                    root / "cache",
                    candidates,
                )

    def test_private_xemu_candidate_is_exact_and_side_effect_free(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate, runtime, manifest = self.make_xemu_candidate_fixture(
                root
            )
            bundled = runtime / "xemu" / "AppDir" / "usr" / "bin" / "xemu"

            stage_private_xemu_candidate(
                candidate,
                runtime,
                "x86_64-steamdeck-linux-gnu",
                manifest,
            )

            self.assertEqual(bundled.read_bytes(), candidate.read_bytes())
            self.assertEqual(bundled.stat().st_mode & 0o777, 0o755)
            metadata = json.loads(
                (runtime / "CANDIDATE.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["release_state"], "candidate-only")
            self.assertEqual(
                metadata["xemu"]["capabilities"],
                self.xemu_integration["capability_query"]["response"],
            )
            identity = manifest["xemu"]["embedded_privacy_files"][0]
            self.assertEqual(metadata["xemu"]["size"], identity["size"])
            self.assertEqual(metadata["xemu"]["sha256"], identity["sha256"])
            self.assertIn(
                "contains a reviewed private GDOX-patched xemu",
                (runtime / "SOURCE.md").read_text(encoding="utf-8"),
            )

    def test_windows_private_xemu_candidate_uses_windows_runtime_layout(
        self,
    ) -> None:
        response = json.dumps(
            self.xemu_integration["capability_query"]["response"],
            separators=(",", ":"),
        )
        for target in (
            "x86_64-pc-windows-gnu",
            "x86_64-pc-windows-msvc",
        ):
            with self.subTest(target=target), tempfile.TemporaryDirectory() \
                    as temporary:
                root = Path(temporary)
                candidate = root / "candidate-xemu.exe"
                candidate.write_bytes(b"reviewed Windows xemu")
                runtime = root / "runtime"
                bundled = runtime / "xemu" / "xemu.exe"
                bundled.parent.mkdir(parents=True)
                bundled.write_bytes(b"upstream Windows xemu")
                (runtime / "SOURCE.md").write_text(
                    "This bundle contains the reviewed GDOX-patched xemu "
                    "executable.\n",
                    encoding="utf-8",
                )
                manifest = {
                    "xemu": {
                        "version": "test",
                        "embedded_privacy_files": [
                            {
                                "member": "runtime/xemu/xemu.exe",
                                "size": candidate.stat().st_size,
                                "sha256": hashlib.sha256(
                                    candidate.read_bytes()
                                ).hexdigest(),
                            }
                        ],
                    }
                }
                probe = Mock(
                    returncode=0,
                    stdout=response + "\n",
                    stderr="",
                )
                with patch(
                    "private_candidate_runtime.shutil.which",
                    side_effect=lambda name: f"/usr/bin/{name}",
                ), patch(
                    "private_candidate_runtime.subprocess.run",
                    side_effect=[Mock(returncode=0), probe],
                ) as run:
                    stage_private_xemu_candidate(
                        candidate,
                        runtime,
                        target,
                        manifest,
                    )

                self.assertEqual(bundled.read_bytes(), candidate.read_bytes())
                metadata = json.loads(
                    (runtime / "CANDIDATE.json").read_text(encoding="utf-8")
                )
                self.assertEqual(
                    metadata["xemu"]["executable"],
                    "runtime/xemu/xemu.exe",
                )
                self.assertEqual(
                    run.call_args_list[1].args[0],
                    [
                        "/usr/bin/wine",
                        str(bundled),
                        self.xemu_integration["capability_query"]["argument"],
                    ],
                )

    def test_private_xemu_candidate_rejects_a_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate-xemu"
            candidate.write_bytes(b"reviewed xemu")
            link = root / "candidate-link"
            link.symlink_to(candidate.name)
            manifest = {
                "xemu": {
                    "embedded_privacy_files": [
                        {
                            "member": "runtime/xemu/AppDir/usr/bin/xemu",
                            "size": candidate.stat().st_size,
                            "sha256": hashlib.sha256(
                                candidate.read_bytes()
                            ).hexdigest(),
                        }
                    ]
                }
            }
            with self.assertRaisesRegex(SystemExit, "regular file"):
                stage_private_xemu_candidate(
                    link,
                    root / "runtime",
                    "x86_64-steamdeck-linux-gnu",
                    manifest,
                )

    def test_private_xemu_candidate_rejects_an_unexpected_notice(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate, runtime, manifest = self.make_xemu_candidate_fixture(
                root,
                source_notice="unexpected source notice\n",
            )

            with self.assertRaisesRegex(SystemExit, "source notice"):
                stage_private_xemu_candidate(
                    candidate,
                    runtime,
                    "x86_64-steamdeck-linux-gnu",
                    manifest,
                )

    def test_private_xemu_candidate_rejects_probe_side_effects(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate, runtime, manifest = self.make_xemu_candidate_fixture(
                root,
                side_effect=True,
            )

            with self.assertRaisesRegex(SystemExit, "capability probe"):
                stage_private_xemu_candidate(
                    candidate,
                    runtime,
                    "x86_64-steamdeck-linux-gnu",
                    manifest,
                )
            self.assertFalse((runtime / "CANDIDATE.json").exists())

    def test_review_manifest_is_valid_and_runtimes_are_publishable(self) -> None:
        validate_runtime_manifest(self.manifest)
        for target in (LINUX_TARGET, WINDOWS_TARGET):
            require_publishable(target, self.manifest)

    def test_windows_build_recipe_keeps_independent_verified_roots(self) -> None:
        recipe_entry = self.manifest["xenia"]["integration"][
            "windows_build"
        ]["recipe"]
        recipe_path = ROOT / recipe_entry["path"]
        recipe_bytes = recipe_path.read_bytes()
        recipe = recipe_bytes.decode("utf-8")

        self.assertEqual(len(recipe_bytes), recipe_entry["size"])
        self.assertEqual(
            hashlib.sha256(recipe_bytes).hexdigest(),
            recipe_entry["sha256"],
        )
        self.assertIn("function Get-CanonicalPath", recipe)
        self.assertIn("function Assert-NoReparsePoints", recipe)
        self.assertIn("function Test-PathsOverlap", recipe)
        self.assertIn(
            "RepositoryRoot, WorkRoot, and OutputDirectory must be "
            "separate trees.",
            recipe,
        )
        self.assertLess(
            recipe.index(
                'Assert-NoReparsePoints $VulkanInstaller "VulkanInstaller"'
            ),
            recipe.index("& $VulkanInstaller --root $env:VULKAN_SDK"),
        )
        self.assertIn(
            "Directory must not exist or must be empty: $Directory",
            recipe,
        )
        self.assertIn("& .\\xb.bat setup", recipe)
        self.assertIn(
            "& cmake --build build --config Release --target xenia-app",
            recipe,
        )
        self.assertNotIn("CacheRoot", recipe)
        self.assertNotIn("source_reused", recipe)
        self.assertNotIn("build_reused", recipe)

    def test_xenia_integration_requires_a_reviewed_patch(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xenia"]["integration"]["patches"] = []
        with self.assertRaisesRegex(SystemExit, "at least one reviewed patch"):
            validate_runtime_manifest(altered)

    def test_reviewed_content_patch_is_claimed_only_by_verified_assets(
        self,
    ) -> None:
        patch_path = (
            "packaging/xenia/patches/"
            "0004-gdox-ephemeral-game-content.patch"
        )
        self.assertTrue((ROOT / patch_path).is_file())
        self.assertIn(
            patch_path,
            {
                entry["path"]
                for entry in self.manifest["xenia"]["integration"]["patches"]
            },
        )
        for revision, definition in self.manifest["xenia"][
            "revisions"
        ].items():
            for target, asset in definition["targets"].items():
                self.assertIn(
                    "gdox_persistent_content_saves_only",
                    asset["managed_options"],
                )
                self.assertEqual(asset["origin"], "gdox-patched")
                self.assertEqual(asset["release_state"], "published")
                self.assertTrue(
                    has_reviewed_save_only_capability(
                        revision, target, asset
                    )
                )

    def test_managed_disclaimer_patch_is_explicit_and_verified(
        self,
    ) -> None:
        patch_path = (
            "packaging/xenia/patches/"
            "0005-gdox-managed-disclaimer.patch"
        )
        patch_text = (ROOT / patch_path).read_text(encoding="utf-8")
        self.assertIn(
            patch_path,
            {
                entry["path"]
                for entry in self.manifest["xenia"]["integration"][
                    "patches"
                ]
            },
        )
        self.assertIn("DEFINE_transient_bool(", patch_text)
        self.assertIn("gdox_disclaimer_acknowledged", patch_text)
        self.assertIn("!cvars::gdox_disclaimer_acknowledged", patch_text)
        self.assertIn("acknowledged-no-external-link", patch_text)
        for revision, definition in self.manifest["xenia"][
            "revisions"
        ].items():
            for target, asset in definition["targets"].items():
                self.assertIn(
                    "gdox_disclaimer_acknowledged",
                    asset["managed_options"],
                )
                self.assertTrue(
                    has_reviewed_managed_session_capability(
                        revision, target, asset
                    )
                )

    def test_publishable_metadata_requires_the_disclaimer_patch(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xenia"]["integration"]["patches"] = [
            patch
            for patch in altered["xenia"]["integration"]["patches"]
            if patch["path"]
            != "packaging/xenia/patches/0005-gdox-managed-disclaimer.patch"
        ]
        for definition in altered["xenia"]["revisions"].values():
            asset = definition["targets"][LINUX_TARGET]
            asset["release_state"] = "published"
            asset["url"] = (
                "https://downloads.gdox.invalid/xenia/"
                + asset["archive_name"]
            )
        with self.assertRaisesRegex(SystemExit, "managed disclaimer patch"):
            require_publishable(LINUX_TARGET, altered)

    def test_reviewed_binary_enables_managed_launch(self) -> None:
        revision = "72ce13097"
        asset = self.manifest["xenia"]["revisions"][revision]["targets"][
            WINDOWS_TARGET
        ]

        runtime_values = _runtime_values(revision, asset, WINDOWS_TARGET)
        self.assertTrue(runtime_values[10])

    def test_publishable_metadata_also_requires_the_content_patch(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xenia"]["integration"]["patches"] = [
            patch
            for patch in altered["xenia"]["integration"]["patches"]
            if patch["path"]
            != "packaging/xenia/patches/0004-gdox-ephemeral-game-content.patch"
        ]
        for definition in altered["xenia"]["revisions"].values():
            asset = definition["targets"][LINUX_TARGET]
            asset["release_state"] = "published"
            asset["url"] = (
                "https://downloads.gdox.invalid/xenia/"
                + asset["archive_name"]
            )
        with self.assertRaisesRegex(SystemExit, "save-only content patch"):
            require_publishable(LINUX_TARGET, altered)

    def test_upstream_binary_cannot_claim_save_only_storage(self) -> None:
        altered = deepcopy(self.manifest)
        for definition in altered["xenia"]["revisions"].values():
            asset = definition["targets"][LINUX_TARGET]
            asset["release_state"] = "published"
            asset["url"] = (
                "https://downloads.gdox.invalid/xenia/"
                + asset["archive_name"]
            )
        altered["xenia"]["revisions"]["72ce13097"]["targets"][
            LINUX_TARGET
        ]["origin"] = "upstream-release"

        with self.assertRaisesRegex(SystemExit, "unchanged upstream binary"):
            require_publishable(LINUX_TARGET, altered)

    def test_linux_schema_accepts_a_patched_candidate(self) -> None:
        validate_runtime_manifest(self.manifest)
        for definition in self.manifest["xenia"]["revisions"].values():
            asset = definition["targets"][LINUX_TARGET]
            self.assertEqual(asset["format"], "windows-zip-proton")
            self.assertEqual(asset["origin"], "gdox-patched")

    def test_pre_content_patch_windows_binary_cannot_claim_storage(self) -> None:
        altered = deepcopy(self.manifest)
        for definition in altered["xenia"]["revisions"].values():
            asset = definition["targets"][WINDOWS_TARGET]
            asset["release_state"] = "published"
            asset["url"] = (
                "https://downloads.gdox.invalid/xenia/" +
                asset["archive_name"]
            )
        altered["xenia"]["revisions"]["72ce13097"]["targets"][
            WINDOWS_TARGET
        ]["executable_sha256"] = "0" * 64

        validate_runtime_manifest(altered)
        with self.assertRaisesRegex(SystemExit, "executable save-only"):
            require_publishable(WINDOWS_TARGET, altered)

    def test_generated_policy_rejects_manifest_only_storage_claim(self) -> None:
        altered = deepcopy(self.manifest)
        revision = "72ce13097"
        asset = altered["xenia"]["revisions"][revision]["targets"][
            WINDOWS_TARGET
        ]
        asset["executable_sha256"] = "0" * 64

        runtime_values = _runtime_values(revision, asset, WINDOWS_TARGET)
        self.assertFalse(runtime_values[8])

    def test_storage_capability_query_requires_exact_response(self) -> None:
        response = (
            '{"schema":1,"runtime":"xenia","storage":{'
            '"persistent":"saves-profiles-only",'
            '"game_content":"ephemeral"},"session":{'
            '"disclaimer":"acknowledged-no-external-link"}}'
        )
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "xenia-capability"
            executable.write_text(
                "#!/bin/sh\n"
                "test \"$1\" = --gdox-storage-capabilities || exit 2\n"
                f"printf '%s\\n' '{response}'\n",
                encoding="utf-8",
            )
            executable.chmod(0o700)
            verify_storage_capability(executable, "test Xenia")

            executable.write_text(
                executable.read_text(encoding="utf-8") +
                "printf unexpected\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(SystemExit, "failed exact"):
                verify_storage_capability(executable, "test Xenia")

            executable.write_text(
                "#!/bin/sh\n"
                "test \"$1\" = --gdox-storage-capabilities || exit 2\n"
                "touch unwanted-state\n"
                f"printf '%s\\n' '{response}'\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(SystemExit, "failed exact"):
                verify_storage_capability(executable, "test Xenia")

    def test_title_profiles_do_not_own_guest_cache_mount_policy(self) -> None:
        compatibility = json.loads(
            (ROOT / "packaging/xenia-compatibility.json").read_text(
                encoding="utf-8"
            )
        )
        profiles = [compatibility["default"], *compatibility["titles"]]
        for profile in profiles:
            self.assertNotIn("mount_cache", profile["settings"])

    def test_proton_prefix_is_session_scoped(self) -> None:
        launcher = (ROOT / "packaging/linux/xenia-proton").read_text(
            encoding="utf-8"
        )

        self.assertIn('--cache_root=/*)', launcher)
        self.assertIn('STEAM_COMPAT_DATA_PATH="$session_cache/proton"', launcher)
        self.assertNotIn('gdox/xenia/proton/$revision', launcher)

    def test_linux_injection_policy_is_exact(self) -> None:
        removed, fixed = load_injection_environment_policy()

        self.assertEqual(
            set(removed),
            {
                "LD_PRELOAD",
                "ENABLE_GAMESCOPE_WSI",
                "VK_INSTANCE_LAYERS",
                "VK_LAYER_PATH",
                "VK_ADD_LAYER_PATH",
                "VK_IMPLICIT_LAYER_PATH",
                "VK_ADD_IMPLICIT_LAYER_PATH",
                "VK_LOADER_LAYERS_ENABLE",
                "VK_LOADER_LAYERS_ALLOW",
                "MANGOHUD",
                "MANGOHUD_CONFIG",
                "MANGOHUD_CONFIGFILE",
                "MANGOHUD_DLSYM",
                "MANGOHUD_OUTPUT",
                "MANGOHUD_LOG_LEVEL",
                "ENABLE_VKBASALT",
                "VKBASALT_CONFIG_FILE",
            },
        )
        self.assertEqual(
            dict(fixed),
            {
                "DISABLE_GAMESCOPE_WSI": "1",
                "VK_LOADER_LAYERS_DISABLE": "~implicit~",
            },
        )
        self.assertFalse(
            {
                "DRI_PRIME",
                "VK_DRIVER_FILES",
                "VK_ICD_FILENAMES",
                "SteamGamepadUI",
                "STEAM_COMPAT_CLIENT_INSTALL_PATH",
            }
            & (set(removed) | set(dict(fixed)))
        )

    def test_linux_launchers_contain_environment_and_relative_writes(self) -> None:
        removed_injection, fixed_injection = (
            load_injection_environment_policy()
        )
        payload_text = (
            "#!/bin/sh\n"
            "{\n"
            "  pwd\n"
            "  printf 'HOME=%s\\n' \"${HOME:-}\"\n"
            "  printf 'XDG_CONFIG_HOME=%s\\n' \"${XDG_CONFIG_HOME:-}\"\n"
            "  printf 'XDG_DATA_HOME=%s\\n' \"${XDG_DATA_HOME:-}\"\n"
            "  printf 'XDG_STATE_HOME=%s\\n' \"${XDG_STATE_HOME:-}\"\n"
            "  printf 'XDG_RUNTIME_DIR=%s\\n' \"${XDG_RUNTIME_DIR:-}\"\n"
            "  printf 'TMPDIR=%s\\n' \"${TMPDIR:-}\"\n"
            "  printf 'STEAM_COMPAT_DATA_PATH=%s\\n' "
            "\"${STEAM_COMPAT_DATA_PATH:-}\"\n"
            "  printf 'STEAM_COMPAT_INSTALL_PATH=%s\\n' "
            "\"${STEAM_COMPAT_INSTALL_PATH:-}\"\n"
            "  printf 'PROTON_LOG=%s\\n' \"${PROTON_LOG:-}\"\n"
            "  printf 'PROTON_LOG_DIR=%s\\n' \"${PROTON_LOG_DIR:-}\"\n"
            "  printf 'PROTON_DUMP_DEBUG_COMMANDS=%s\\n' "
            "\"${PROTON_DUMP_DEBUG_COMMANDS:-}\"\n"
            "  printf 'PROTON_DEBUG_DIR=%s\\n' \"${PROTON_DEBUG_DIR:-}\"\n"
            "  printf 'PROTON_CRASH_REPORT_DIR=%s\\n' "
            "\"${PROTON_CRASH_REPORT_DIR:-}\"\n"
            "  printf 'DISCOVERY_HOME=%s\\n' "
            "\"${GDOX_XENIA_DISCOVERY_HOME:-}\"\n"
            "  printf 'DISABLE_GAMESCOPE_WSI=%s\\n' "
            "\"${DISABLE_GAMESCOPE_WSI:-}\"\n"
            "  printf 'ENABLE_GAMESCOPE_WSI=%s\\n' "
            "\"${ENABLE_GAMESCOPE_WSI+present}\"\n"
            "  env | sed 's/^/PROCESS_ENV:/'\n"
            "} > \"$GDOX_XENIA_TEST_CAPTURE\"\n"
            ": > relative-xenia-write\n"
        )
        payload_bytes = payload_text.encode("utf-8")
        payload_sha256 = hashlib.sha256(payload_bytes).hexdigest()
        poisoned_names = tuple(dict.fromkeys((
            "HOME",
            "XDG_CACHE_HOME",
            "XDG_CONFIG_HOME",
            "XDG_DATA_HOME",
            "XDG_STATE_HOME",
            "XDG_RUNTIME_DIR",
            "TMPDIR",
            "STEAM_COMPAT_DATA_PATH",
            "STEAM_COMPAT_INSTALL_PATH",
            "STEAM_COMPAT_MEDIA_PATH",
            "STEAM_COMPAT_TRANSCODED_MEDIA_PATH",
            "PROTON_LOG",
            "PROTON_LOG_DIR",
            "PROTON_DUMP_DEBUG_COMMANDS",
            "PROTON_DEBUG_DIR",
            "PROTON_CRASH_REPORT_DIR",
            "WINEPREFIX",
            *(name for name in removed_injection),
            *(name for name, unused in fixed_injection),
        )))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            runtime.mkdir()
            payload = runtime / "xenia_canary.exe"
            payload.write_bytes(payload_bytes)
            payload.chmod(0o700)
            poison = root / "poison"
            poison.mkdir()
            discovery = root / "discovery"
            proton = discovery / (
                ".local/share/Steam/steamapps/common/"
                "Proton - Experimental/proton"
            )
            proton.parent.mkdir(parents=True)
            proton.write_text(
                "#!/bin/sh\n"
                ": > relative-proton-write\n"
                "test \"$1\" = run || exit 2\n"
                "shift\n"
                "exec \"$@\"\n",
                encoding="utf-8",
            )
            proton.chmod(0o700)
            steamclient = discovery / ".steam/sdk64/steamclient.so"
            steamclient.parent.mkdir(parents=True)
            steamclient.write_bytes(b"test Steam client\n")

            base_environment = os.environ.copy()
            for name in poisoned_names:
                base_environment[name] = str(poison)
            base_environment.pop("GDOX_PROTON", None)
            base_environment["GDOX_XENIA_DISCOVERY_HOME"] = str(discovery)
            base_environment["SteamGamepadUI"] = "1"

            for template_name in ("xenia-proton", "xenia-native"):
                with self.subTest(template=template_name):
                    session = root / f"session-{template_name}"
                    session.mkdir()
                    capture = root / f"capture-{template_name}"
                    environment = base_environment.copy()
                    environment["GDOX_XENIA_TEST_CAPTURE"] = str(capture)
                    template = (ROOT / "packaging/linux" / template_name)
                    launcher_text = template.read_text(encoding="utf-8")
                    launcher_text = launcher_text.replace(
                        "@GDOX_XENIA_EXECUTABLE@", payload.name
                    ).replace(
                        "@GDOX_XENIA_EXECUTABLE_SIZE@", str(len(payload_bytes))
                    ).replace(
                        "@GDOX_XENIA_EXECUTABLE_SHA256@", payload_sha256
                    ).replace(
                        "@GDOX_XENIA_INJECTION_ENVIRONMENT@",
                        render_injection_environment_policy(),
                    )
                    launcher = runtime / template_name
                    launcher.write_text(launcher_text, encoding="utf-8")
                    launcher.chmod(0o700)

                    subprocess.run(
                        [str(launcher), f"--cache_root={session}"],
                        cwd=poison,
                        env=environment,
                        check=True,
                        capture_output=True,
                        text=True,
                    )

                    recorded = capture.read_text(encoding="utf-8")
                    process_environment = dict(
                        line.removeprefix("PROCESS_ENV:").split("=", 1)
                        for line in recorded.splitlines()
                        if line.startswith("PROCESS_ENV:") and "=" in line
                    )
                    self.assertEqual(recorded.splitlines()[0], str(session))
                    self.assertIn(f"HOME={session}/home\n", recorded)
                    self.assertIn(
                        f"XDG_CONFIG_HOME={session}/xdg-config\n", recorded
                    )
                    self.assertIn(f"XDG_DATA_HOME={session}/xdg-data\n", recorded)
                    self.assertIn(
                        f"XDG_STATE_HOME={session}/xdg-state\n", recorded
                    )
                    self.assertIn(
                        f"XDG_RUNTIME_DIR={session}/xdg-runtime\n", recorded
                    )
                    self.assertIn(f"TMPDIR={session}/tmp\n", recorded)
                    self.assertIn("DISCOVERY_HOME=\n", recorded)
                    self.assertIn("DISABLE_GAMESCOPE_WSI=1\n", recorded)
                    self.assertIn("ENABLE_GAMESCOPE_WSI=\n", recorded)
                    for name in removed_injection:
                        self.assertNotIn(name, process_environment)
                    for name, value in fixed_injection:
                        self.assertEqual(process_environment.get(name), value)
                    self.assertEqual(
                        process_environment.get("SteamGamepadUI"), "1"
                    )
                    self.assertTrue((session / "relative-xenia-write").is_file())
                    self.assertFalse((poison / "relative-xenia-write").exists())
                    if template_name == "xenia-proton":
                        projected_steamclient = (
                            session / "home/.steam/sdk64/steamclient.so"
                        )
                        self.assertTrue(projected_steamclient.is_symlink())
                        self.assertEqual(
                            projected_steamclient.resolve(), steamclient.resolve()
                        )
                        subprocess.run(
                            [str(launcher), f"--cache_root={session}"],
                            cwd=poison,
                            env=environment,
                            check=True,
                            capture_output=True,
                            text=True,
                        )
                        self.assertEqual(
                            projected_steamclient.resolve(), steamclient.resolve()
                        )
                        self.assertIn(
                            f"STEAM_COMPAT_DATA_PATH={session}/proton\n",
                            recorded,
                        )
                        self.assertIn(
                            f"STEAM_COMPAT_INSTALL_PATH={session}/install\n",
                            recorded,
                        )
                        self.assertIn("PROTON_LOG=0\n", recorded)
                        self.assertIn(
                            f"PROTON_LOG_DIR={session}/proton-logs\n", recorded
                        )
                        self.assertIn(
                            "PROTON_DUMP_DEBUG_COMMANDS=0\n", recorded
                        )
                        self.assertIn(
                            f"PROTON_DEBUG_DIR={session}/proton-debug\n",
                            recorded,
                        )
                        self.assertIn(
                            f"PROTON_CRASH_REPORT_DIR={session}/proton-crash\n",
                            recorded,
                        )
                        self.assertTrue(
                            (session / "relative-proton-write").is_file()
                        )
                        self.assertFalse(
                            (poison / "relative-proton-write").exists()
                        )

    def test_xenia_runtime_exemptions_are_exact_manifest_files(self) -> None:
        expected = {
            (
                f"runtime/xenia/{revision}/{asset['executable']}",
                asset["archive_name"],
                asset["executable"],
                asset["executable_size"],
                asset["executable_sha256"],
            )
            for revision, definition in self.manifest["xenia"][
                "revisions"
            ].items()
            for asset in definition["targets"].values()
        }
        self.assertEqual(set(XENIA_RUNTIME_FILES), expected)

    def test_xemu_build_path_exemptions_are_exact_manifest_files(self) -> None:
        expected = {
            (file["member"], file["size"], file["sha256"])
            for file in self.manifest["xemu"]["embedded_build_path_files"]
        }
        self.assertEqual(set(XEMU_BUILD_PATH_FILES), expected)

    def test_xemu_build_path_exemption_member_must_be_normalized(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xemu"]["embedded_build_path_files"][0]["member"] = (
            "runtime/xemu/../substituted"
        )
        with self.assertRaisesRegex(SystemExit, "normalized relative path"):
            validate_complete_runtime_manifest(altered)

    def test_xemu_build_path_exemption_rejects_unknown_fields(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xemu"]["embedded_build_path_files"][0]["note"] = (
            "not part of the schema"
        )
        with self.assertRaisesRegex(SystemExit, "unexpected note"):
            validate_complete_runtime_manifest(altered)

    def test_xemu_privacy_exemptions_are_exact_manifest_files(self) -> None:
        expected = {
            (file["member"], file["size"], file["sha256"])
            for file in self.manifest["xemu"]["embedded_privacy_files"]
        }
        self.assertEqual(set(XEMU_PRIVACY_FILES), expected)

    def test_xemu_privacy_exemption_rejects_unknown_fields(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xemu"]["embedded_privacy_files"][0]["note"] = (
            "not part of the schema"
        )
        with self.assertRaisesRegex(SystemExit, "unexpected note"):
            validate_complete_runtime_manifest(altered)

    def test_xemu_notice_exemptions_are_exact_manifest_files(self) -> None:
        expected = {
            (file["member"], file["size"], file["sha256"])
            for file in self.manifest["xemu"]["embedded_notice_files"]
        }
        self.assertEqual(set(XEMU_NOTICE_FILES), expected)

    def test_xemu_notice_exemption_rejects_unknown_fields(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xemu"]["embedded_notice_files"][0]["note"] = (
            "not part of the schema"
        )
        with self.assertRaisesRegex(SystemExit, "unexpected note"):
            validate_complete_runtime_manifest(altered)

    def test_xemu_notice_exemption_rejects_duplicate_identity(self) -> None:
        altered = deepcopy(self.manifest)
        altered["xemu"]["embedded_notice_files"].append(
            deepcopy(altered["xemu"]["embedded_notice_files"][0])
        )
        with self.assertRaisesRegex(SystemExit, "duplicate.*identity"):
            validate_complete_runtime_manifest(altered)

    def test_macos_signing_preserves_nested_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            application = Path(temporary) / "GDOX.app"
            executable = application / "Contents" / "MacOS" / "gdox"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"GDOX")
            commands = macos_signing_commands(application, "-")
            self.assertEqual(len(commands), 2)
            self.assertEqual(commands[0][-1], str(executable))
            self.assertEqual(commands[1][-1], str(application))
            for command in commands:
                self.assertNotIn("--deep", command)
                self.assertNotIn("--timestamp", command)

            release_commands = macos_signing_commands(
                application,
                "Developer ID Application: Example",
            )
            for command in release_commands:
                sign_index = command.index("--sign")
                self.assertEqual(
                    command[sign_index + 1],
                    "Developer ID Application: Example",
                )
                self.assertIn("--options", command)
                self.assertIn("runtime", command)
                self.assertIn("--timestamp", command)
                self.assertNotIn("--deep", command)

            wrong_case = executable.with_name("GDOX")
            executable.rename(wrong_case)
            with self.assertRaisesRegex(SystemExit, "executable is missing"):
                macos_signing_commands(application, "-")

            restore = executable.with_name("gdox.restore")
            wrong_case.rename(restore)
            restore.rename(executable)
            real_executable = executable.with_name("gdox-real")
            executable.rename(real_executable)
            executable.symlink_to(real_executable.name)
            with self.assertRaisesRegex(SystemExit, "executable is missing"):
                macos_signing_commands(application, "-")

    def test_macos_developer_package_signing_does_not_require_runtime(self) -> None:
        application = Path("GDOX.app")
        with (
            patch("package_release.sys.platform", "darwin"),
            patch("package_release.run") as run,
            patch(
                "package_release.verify_macos_embedded_runtime"
            ) as verify_runtime,
            patch(
                "package_release.macos_signing_commands",
                return_value=(["sign-gdox"], ["sign-app"]),
            ),
        ):
            sign_macos(application, None)
        verify_runtime.assert_not_called()
        self.assertEqual(run.call_count, 3)

    def test_macos_embedded_runtime_verification_is_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            application = Path(temporary) / "GDOX.app"
            member = "runtime/xemu/xemu.app/Contents/MacOS/xemu"
            runtime = application / "Contents" / "Resources" / member
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"pinned xemu")
            manifest = {
                "xemu": {
                    "embedded_build_path_files": [
                        {
                            "member": member,
                            "size": runtime.stat().st_size,
                            "sha256": hashlib.sha256(
                                runtime.read_bytes()
                            ).hexdigest(),
                        }
                    ]
                }
            }
            verified = verify_macos_embedded_runtime(
                application,
                manifest,
            )
            self.assertEqual(len(verified), 1)
            runtime.write_bytes(b"changed xemu")
            with self.assertRaisesRegex(SystemExit, "runtime changed"):
                verify_macos_embedded_runtime(application, manifest)

            runtime.unlink()
            alternate = runtime.with_name("alternate-xemu")
            alternate.write_bytes(b"pinned xemu")
            runtime.symlink_to(alternate)
            with self.assertRaisesRegex(SystemExit, "runtime changed"):
                verify_macos_embedded_runtime(application, manifest)

    def test_macos_signing_rejects_symlinked_bundle_ancestors(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            application = root / "GDOX.app"
            contents = application / "Contents"
            contents.mkdir(parents=True)

            outside_macos = root / "outside-macos"
            outside_macos.mkdir()
            (outside_macos / "gdox").write_bytes(b"GDOX")
            (contents / "MacOS").symlink_to(
                outside_macos,
                target_is_directory=True,
            )
            with self.assertRaisesRegex(SystemExit, "executable is missing"):
                macos_signing_commands(application, "-")

            (contents / "MacOS").unlink()
            (contents / "MacOS").mkdir()
            (contents / "MacOS" / "gdox").write_bytes(b"GDOX")

            member = "runtime/xemu/xemu.app/Contents/MacOS/xemu"
            outside_resources = root / "outside-resources"
            runtime = outside_resources / member
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"pinned xemu")
            (contents / "Resources").symlink_to(
                outside_resources,
                target_is_directory=True,
            )
            manifest = {
                "xemu": {
                    "embedded_build_path_files": [
                        {
                            "member": member,
                            "size": runtime.stat().st_size,
                            "sha256": hashlib.sha256(
                                runtime.read_bytes()
                            ).hexdigest(),
                        }
                    ]
                }
            }
            with self.assertRaisesRegex(SystemExit, "runtime changed"):
                verify_macos_embedded_runtime(application, manifest)

    def test_candidate_cannot_claim_an_upstream_archive(self) -> None:
        altered = deepcopy(self.manifest)
        asset = altered["xenia"]["revisions"]["72ce13097"]["targets"][
            WINDOWS_TARGET
        ]
        asset["release_state"] = "candidate-only"
        asset["url"] = (
            "https://github.com/xenia-canary/xenia-canary/releases/download/"
            f"72ce130/{asset['archive_name']}"
        )
        with self.assertRaisesRegex(SystemExit, "candidate-only"):
            validate_runtime_manifest(altered)

    def test_published_patched_runtime_cannot_use_xenia_origin(self) -> None:
        altered = deepcopy(self.manifest)
        asset = altered["xenia"]["revisions"]["72ce13097"]["targets"][
            WINDOWS_TARGET
        ]
        asset["release_state"] = "published"
        asset["url"] = (
            "https://github.com/xenia-canary/xenia-canary/releases/download/"
            f"gdox/{asset['archive_name']}"
        )
        with self.assertRaisesRegex(SystemExit, "downstream archive"):
            validate_runtime_manifest(altered)

    def test_deterministic_archive_has_independent_outer_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "xenia_canary.exe"
            license_file = root / "LICENSE"
            executable.write_bytes(b"MZ\x00reviewed-runtime")
            license_file.write_bytes(b"BSD-3-Clause\n")
            first = root / "first.zip"
            second = root / "second.zip"
            write_archive(executable, license_file, first)
            write_archive(executable, license_file, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())

            asset = deepcopy(self.windows_asset())
            asset["executable_size"] = executable.stat().st_size
            asset["executable_sha256"] = hashlib.sha256(
                executable.read_bytes()
            ).hexdigest()
            asset["size"] = first.stat().st_size
            asset["sha256"] = hashlib.sha256(first.read_bytes()).hexdigest()
            validate_archive(first, asset, "test archive")
            with first.open("ab") as output:
                output.write(b"corrupt")
            with self.assertRaisesRegex(SystemExit, "archive failed"):
                validate_archive(first, asset, "test archive")


if __name__ == "__main__":
    unittest.main()
