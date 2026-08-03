"""Regression tests for the binary release packaging boundary."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
import zipfile
from copy import deepcopy
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
from audit_release import inspect_archive
from fetch_runtime import (
    audit_runtime_release,
    load_manifest,
    publication_runtime_assets,
    validate_manifest,
)
from linux_bridge_distribution import validate_stage as validate_bridge_stage
from package_private_candidate import candidate_package_name
from package_release import (
    copy_documentation,
    payload_inventory,
    release_package_name,
    validate_payload_inventory,
)

WINDOWS_TARGET = "x86_64-pc-windows-msvc"


class PackageReleaseTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="gdox-package-release-"
        )
        self.root = Path(self.temporary.name)
        self.manifest = load_manifest()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def materialize(stage: Path, names: set[str]) -> None:
        for name in names:
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture\n")

    def test_runtime_less_package_is_unmistakably_developer_only(self) -> None:
        ordinary = release_package_name(
            "0.2.0",
            WINDOWS_TARGET,
            without_runtime=False,
        )
        candidate = candidate_package_name("0.2.0", WINDOWS_TARGET)
        developer = release_package_name(
            "0.2.0",
            WINDOWS_TARGET,
            without_runtime=True,
        )
        self.assertEqual(ordinary, f"gdox-0.2.0-{WINDOWS_TARGET}")
        self.assertEqual(candidate, ordinary + "-candidate")
        self.assertEqual(developer, ordinary + "-developer-no-runtime")

        artifact = self.root / f"{developer}.zip"
        with zipfile.ZipFile(artifact, "w") as archive:
            archive.writestr(f"{developer}/gdox.exe", b"fixture")
        findings: list[str] = []
        inspect_archive(artifact, findings)
        self.assertTrue(
            any(
                "developer-only runtime-less package" in finding
                for finding in findings
            ),
            msg=findings,
        )

    def test_runtime_less_packager_creates_only_the_developer_name(self) -> None:
        artifact = self.root / "gdox.exe"
        artifact.write_bytes(b"developer fixture\n")
        output = self.root / "output"
        completed = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "package_release.py"),
                "--version",
                "0.2.0",
                "--target",
                WINDOWS_TARGET,
                "--artifact",
                str(artifact),
                "--output",
                str(output),
                "--without-runtime",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, msg=completed.stderr)
        developer = output / (
            f"gdox-0.2.0-{WINDOWS_TARGET}-developer-no-runtime.zip"
        )
        self.assertTrue(developer.is_file())
        self.assertFalse(
            (output / f"gdox-0.2.0-{WINDOWS_TARGET}.zip").exists()
        )

        audited = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(developer),
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(audited.returncode, 0)
        self.assertIn(
            "developer-only runtime-less package",
            audited.stderr,
        )

    def test_candidate_packaging_is_absent_from_public_commands(self) -> None:
        for script in ("package_release.py", "fetch_runtime.py"):
            path = ROOT / "scripts" / script
            self.assertNotIn(
                "candidate",
                path.read_text(encoding="utf-8").casefold(),
            )
            completed = subprocess.run(
                [sys.executable, str(path), "--help"],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, msg=completed.stderr)
            self.assertNotIn("candidate", completed.stdout.casefold())

        private_help = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "package_private_candidate.py"),
                "--help",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(private_help.returncode, 0, msg=private_help.stderr)
        self.assertIn(
            "--candidate-runtime-directory",
            private_help.stdout,
        )
        self.assertIn(
            "--candidate-xemu-executable",
            private_help.stdout,
        )

    def test_binary_documentation_excludes_source_only_material(self) -> None:
        stage = self.root / "documentation"
        copy_documentation(stage)

        self.assertTrue((stage / "docs" / "USER_GUIDE.md").is_file())
        self.assertTrue((stage / "licenses/libusb-LGPL-2.1.txt").is_file())
        self.assertFalse((stage / "docs" / "CATALOG.md").exists())
        self.assertFalse((stage / "docs" / "schemas").exists())
        self.assertFalse((stage / "catalog").exists())

    def test_unreleased_android_is_absent_from_public_tag_workflow(self) -> None:
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        status = (ROOT / "docs/STATUS.md").read_text(
            encoding="utf-8"
        ).casefold()
        releasing = " ".join(
            (ROOT / "docs/RELEASING.md")
            .read_text(encoding="utf-8")
            .casefold()
            .split()
        )

        android_status = next(
            line for line in status.splitlines() if "| android arm64 |" in line
        )
        self.assertIn("not released", android_status)
        self.assertIn("android remains non-publishable", releasing)
        self.assertNotIn("android", workflow.casefold())

    def test_publication_workflow_stays_draft_and_repository_local(self) -> None:
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        ).casefold()
        releasing = " ".join(
            (ROOT / "docs/RELEASING.md")
            .read_text(encoding="utf-8")
            .casefold()
            .split()
        )

        self.assertIn("--draft", workflow)
        self.assertIn("verify release commit is main", workflow)
        self.assertIn(
            'test "$(git rev-parse head)" = "$(git rev-parse origin/main)"',
            workflow,
        )
        self.assertIn(
            "refusing to replace assets on a published release",
            workflow,
        )
        for forbidden in (
            "forgejo",
            "website",
            "package_private_candidate",
            "private/xbox360",
        ):
            self.assertNotIn(forbidden, workflow)
        self.assertIn(
            "website is a separate repository and deployment",
            releasing,
        )
        self.assertIn("github and forgejo", releasing)

    def test_macos_signing_precedes_final_payload_validation(self) -> None:
        source = (ROOT / "scripts/package_release.py").read_text(
            encoding="utf-8"
        )
        main = source.split("def main() -> None:", 1)[1]

        self.assertLess(main.index("sign_macos("), main.index("validate_stage("))
        self.assertLess(
            main.index("sign_macos("),
            main.index('str(ROOT / "scripts" / "audit_release.py")'),
        )

    def test_platform_payload_inventory_rejects_unexpected_files(self) -> None:
        stage = self.root / "windows-stage"
        exact, prefixes = payload_inventory(
            WINDOWS_TARGET,
            "windows",
            None,
            runtime_included=False,
        )
        self.assertEqual(prefixes, ())
        self.assertIn("gdox.exe", exact)
        self.assertNotIn("install.sh", exact)
        self.assertFalse(any(path.startswith("docs/schemas/") for path in exact))
        self.materialize(stage, exact)
        validate_payload_inventory(
            WINDOWS_TARGET,
            "windows",
            stage,
            None,
            runtime_included=False,
        )

        (stage / "install.sh").write_bytes(b"unexpected\n")
        with self.assertRaisesRegex(SystemExit, "unexpected files"):
            validate_payload_inventory(
                WINDOWS_TARGET,
                "windows",
                stage,
                None,
                runtime_included=False,
            )

    def test_runtime_inventory_allows_only_reviewed_runtime_subtrees(self) -> None:
        stage = self.root / "runtime-stage"
        exact, prefixes = payload_inventory(
            WINDOWS_TARGET,
            "windows",
            self.manifest,
            runtime_included=True,
        )
        self.assertIn("runtime/xemu/", prefixes)
        self.assertIn("runtime/xenia/", prefixes)
        self.assertNotIn("runtime/CANDIDATE.json", exact)
        self.materialize(stage, exact)
        self.materialize(
            stage,
            {
                "runtime/xemu/xemu.exe",
                "runtime/xenia/revision/xenia_canary.exe",
            },
        )
        validate_payload_inventory(
            WINDOWS_TARGET,
            "windows",
            stage,
            self.manifest,
            runtime_included=True,
        )

        (stage / "runtime" / "debug.txt").write_bytes(b"unexpected\n")
        with self.assertRaisesRegex(SystemExit, "runtime/debug.txt"):
            validate_payload_inventory(
                WINDOWS_TARGET,
                "windows",
                stage,
                self.manifest,
                runtime_included=True,
            )

    def test_runtime_manifest_rejects_unreviewed_schema_expansion(self) -> None:
        manifest = deepcopy(self.manifest)
        manifest["private_notes"] = "must not enter VERSIONS.json"
        with self.assertRaisesRegex(SystemExit, "missing or unknown fields"):
            validate_manifest(manifest)

        manifest = deepcopy(self.manifest)
        del manifest["xemu"]["targets"]["aarch64-apple-darwin"]
        with self.assertRaisesRegex(SystemExit, "every supported desktop target"):
            validate_manifest(manifest)

        manifest = deepcopy(self.manifest)
        manifest["xemu"]["licenses"][1]["name"] = manifest["xemu"][
            "licenses"
        ][0]["name"]
        with self.assertRaisesRegex(SystemExit, "file names must be unique"):
            validate_manifest(manifest)

    def test_runtime_publication_inventory_is_exact(self) -> None:
        stage = self.root / "runtime-release"
        stage.mkdir()
        assets = publication_runtime_assets(self.manifest)
        self.assertIn(self.manifest["xemu"]["source"]["name"], assets)
        for name in assets:
            (stage / name).write_bytes(b"reviewed fixture")
        (stage / "SHA256SUMS").write_text(
            "".join(
                f"{asset['sha256']}  {name}\n"
                for name, asset in assets.items()
            ),
            encoding="utf-8",
        )
        with patch("fetch_runtime.verify", return_value=True):
            audit_runtime_release(stage)
            (stage / "unexpected.txt").write_text("unexpected\n")
            with self.assertRaisesRegex(SystemExit, "missing, extra, or unsafe"):
                audit_runtime_release(stage)

    def test_platform_payload_inventory_requires_every_exact_file(self) -> None:
        stage = self.root / "missing-stage"
        exact, _ = payload_inventory(
            WINDOWS_TARGET,
            "windows",
            None,
            runtime_included=False,
        )
        self.materialize(stage, exact)
        (stage / "THIRD_PARTY_NOTICES.md").unlink()

        with self.assertRaisesRegex(
            SystemExit,
            r"(?s)missing required files.*THIRD_PARTY_NOTICES\.md",
        ):
            validate_payload_inventory(
                WINDOWS_TARGET,
                "windows",
                stage,
                None,
                runtime_included=False,
            )

    def test_platform_payload_inventory_rejects_required_file_symlink(
        self,
    ) -> None:
        stage = self.root / "symlink-stage"
        exact, _ = payload_inventory(
            WINDOWS_TARGET,
            "windows",
            None,
            runtime_included=False,
        )
        self.materialize(stage, exact)
        notice = stage / "THIRD_PARTY_NOTICES.md"
        notice.unlink()
        notice.symlink_to(stage / "LICENSE")

        with self.assertRaisesRegex(
            SystemExit,
            r"(?s)missing required files.*THIRD_PARTY_NOTICES\.md",
        ):
            validate_payload_inventory(
                WINDOWS_TARGET,
                "windows",
                stage,
                None,
                runtime_included=False,
            )

    def test_steam_deck_bridge_inventory_is_exact(self) -> None:
        stage = self.root / "bridge-stage"
        self.materialize(
            stage,
            {
                "libexec/nbdfuse",
                "libexec/nbdfuse.bin",
                "runtime/bridge/LICENSE.txt",
                "runtime/bridge/SOURCE.md",
                "runtime/bridge/VERSION.json",
                "runtime/bridge/lib/libnbd.so.0",
            },
        )
        with patch("linux_bridge_distribution._verify_file"):
            validate_bridge_stage(
                "x86_64-steamdeck-linux-gnu",
                stage,
                self.manifest,
            )
            (stage / "runtime" / "bridge" / "debug.txt").write_bytes(
                b"unexpected\n"
            )
            with self.assertRaisesRegex(SystemExit, "unexpected contents"):
                validate_bridge_stage(
                    "x86_64-steamdeck-linux-gnu",
                    stage,
                    self.manifest,
                )


if __name__ == "__main__":
    unittest.main()
