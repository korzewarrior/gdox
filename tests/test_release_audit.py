"""Regression tests for release content and archive auditing."""

from __future__ import annotations

import gzip
import hashlib
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from io import BytesIO, StringIO
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
import android_source_provenance
import audit_release
import release_audit_provenance
from android_source_provenance import (
    ANDROID_COMPONENT_TREE_KEYS,
    ANDROID_SOURCE_COMPONENTS,
    SourceTreeEntry,
    canonical_manifest_lines,
    canonical_tree_digest,
    dependency_lock,
)
from audit_release import (
    inspect_archive,
    inspect_bytes,
    inspect_filesystem_symlink,
)


class ReleaseAuditTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="gdox-release-audit-"
        )
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def artifact(self, data: bytes, name: str = "artifact") -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path

    @contextmanager
    def trusted_android_source_fixture(
        self,
        revision: str,
        source_data: bytes = b"source",
    ):
        archive_name = "gdox-0.2.0-android-corresponding-source"
        archive_path = self.root / f"{archive_name}.tar.gz"
        tree_digest = canonical_tree_digest(
            [
                SourceTreeEntry(
                    "source.c",
                    "file",
                    0o644,
                    size=len(source_data),
                    content_sha256=hashlib.sha256(source_data).hexdigest(),
                )
            ]
        )
        locked = dependency_lock()
        for key in ANDROID_COMPONENT_TREE_KEYS.values():
            locked[key] = tree_digest
        with patch.object(
            android_source_provenance,
            "dependency_lock",
            return_value=locked,
        ):
            release_audit_provenance.cached_android_source_validation.cache_clear()
            manifest = (
                "\n".join(canonical_manifest_lines(revision)) + "\n"
            ).encode()
            with tarfile.open(archive_path, "w:gz") as archive:
                info = tarfile.TarInfo(f"{archive_name}/SOURCE_MANIFEST.txt")
                info.size = len(manifest)
                archive.addfile(info, BytesIO(manifest))
                for component in ANDROID_SOURCE_COMPONENTS:
                    info = tarfile.TarInfo(
                        f"{archive_name}/source/{component}/source.c"
                    )
                    info.size = len(source_data)
                    archive.addfile(info, BytesIO(source_data))
            try:
                yield archive_path, archive_name, source_data
            finally:
                release_audit_provenance.cached_android_source_validation.cache_clear()

    def test_artifact_only_cli_does_not_implicitly_scan_repository(self) -> None:
        source_root = self.root / "source"
        source_root.mkdir()
        (source_root / "unfinished.txt").write_bytes(
            b"TO" + b"DO: unfinished"
        )
        artifact = self.artifact(b"clean artifact", "artifact.bin")
        output = StringIO()
        with (
            patch.object(audit_release, "ROOT", source_root),
            patch.object(
                sys,
                "argv",
                ["audit_release.py", "--artifact", str(artifact)],
            ),
            redirect_stdout(output),
            redirect_stderr(output),
        ):
            self.assertEqual(audit_release.main(), 0)
        with (
            patch.object(audit_release, "ROOT", source_root),
            patch.object(sys, "argv", ["audit_release.py"]),
            redirect_stdout(output),
            redirect_stderr(output),
        ):
            self.assertEqual(audit_release.main(), 1)

    def test_path_cli_uses_archive_aware_content_inspection(self) -> None:
        release = self.root / "release"
        release.mkdir()
        archive_path = release / "clean.tar.gz"
        tar_data = BytesIO()
        content = b"clean release content\n"
        with tarfile.open(fileobj=tar_data, mode="w") as archive:
            member = tarfile.TarInfo("clean/file.txt")
            member.size = len(content)
            archive.addfile(member, BytesIO(content))
        with archive_path.open("wb") as output, gzip.GzipFile(
            filename="private" + "@corp.test",
            mode="wb",
            fileobj=output,
            mtime=0,
        ) as compressed:
            compressed.write(tar_data.getvalue())

        commands = (
            ["--artifact", str(archive_path)],
            ["--path", str(release)],
        )
        for arguments in commands:
            with self.subTest(arguments=arguments):
                audited = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "scripts" / "audit_release.py"),
                        *arguments,
                    ],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(audited.returncode, 0, msg=audited.stderr)

    def test_release_audit_rejects_profile_paths_in_binaries(self) -> None:
        profile_paths = {
            "linux": (
                b"/" + b"home/" + b"unrelated-builder/source/file.cc\0"
            ),
            "macos": (
                b"/Users/"
                b"unrelated-builder"
                b"/source/file.cc\0"
            ),
            "windows": (
                b"C:\\"
                b"Users\\unrelated-builder\\source\\file.cc\0"
            ),
        }
        artifact = self.root / "private-xenia-candidates.zip"
        with zipfile.ZipFile(artifact, "w") as archive:
            for platform, profile_path in profile_paths.items():
                cases = {
                    "ascii": profile_path,
                    "utf-16le": profile_path.decode("ascii").encode(
                        "utf-16le"
                    ),
                }
                for encoding, private_path in cases.items():
                    archive.writestr(
                        "runtime/xenia/"
                        f"candidate-{platform}-{encoding}/xenia_canary.exe",
                        b"MZ\0binary-data\0" + private_path,
                    )
        audited = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(artifact),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(audited.returncode, 0)
        for rule_label in (
            "absolute Linux home path",
            "absolute macOS home path",
            "absolute Windows profile path",
        ):
            self.assertIn(rule_label, audited.stderr)

    def test_release_audit_allows_generic_profile_placeholders(self) -> None:
        profile_paths = {
            "linux": b"/" + b"home/" + b"user/source/file.cc\0",
            "macos": (
                b"/Users/"
                b"user"
                b"/source/file.cc\0"
            ),
            "windows": b"C:\\" + b"Users\\user\\source\\file.cc\0",
        }
        artifact = self.root / "generic-xenia-candidates.zip"
        with zipfile.ZipFile(artifact, "w") as archive:
            for platform, profile_path in profile_paths.items():
                for encoding, generic_path in {
                    "ascii": profile_path,
                    "utf-16le": profile_path.decode("ascii").encode(
                        "utf-16le"
                    ),
                }.items():
                    archive.writestr(
                        "runtime/xenia/"
                        f"candidate-{platform}-{encoding}/xenia_canary.exe",
                        b"MZ\0binary-data\0" + generic_path,
                    )
        audited = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(artifact),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(
            audited.returncode,
            0,
            msg=f"{audited.stdout}\n{audited.stderr}",
        )

    def test_release_audit_rejects_profile_path_edge_cases(self) -> None:
        cases = {
            "absolute Linux home path": (
                (
                    b"/home/"
                    b"user-private"
                    b"/source.cc"
                ),
                (
                    b"/home/"
                    b"private+builder"
                    b"/source.cc"
                ),
                (
                    b"/home/"
                    b"Private Builder"
                    b"/source.cc"
                ),
                (
                    b"/home/"
                    b"private@builder"
                    b"/source.cc"
                ),
                (
                    b"/home/"
                    b"private=builder"
                    b"/source.cc"
                ),
            ),
            "absolute macOS home path": (
                (
                    b"/Users/"
                    b"user.private"
                    b"/source.cc"
                ),
                (
                    b"/users/"
                    b"Private Builder"
                    b"/source.cc"
                ),
            ),
            "absolute Windows profile path": (
                (
                    b"C:\\Users\\"
                    b"user-name"
                    b"\\source.cc"
                ),
                (
                    b"C:\\Users\\"
                    b"Private Builder"
                    b"\\source.cc"
                ),
            ),
        }
        for rule_label, paths in cases.items():
            for path in paths:
                for encoding, encoded in {
                    "ascii": path,
                    "utf-16le": path.decode("ascii").encode("utf-16le"),
                }.items():
                    with self.subTest(
                        rule=rule_label,
                        path=path,
                        encoding=encoding,
                    ):
                        findings: list[str] = []
                        inspect_bytes(
                            "candidate-runtime",
                            b"MZ\0" + encoded,
                            findings,
                        )
                        self.assertTrue(
                            any(rule_label in finding for finding in findings),
                            msg=findings,
                        )

    def test_release_audit_scans_wide_paths_after_eight_kibibytes(self) -> None:
        path = (
            "/Users/"
            + "private-builder"
            + "/source.cc"
        ).encode("utf-16le")
        findings: list[str] = []
        inspect_bytes(
            "delayed-wide-path",
            b"MZ" + b"A" * 9000 + path,
            findings,
        )
        self.assertTrue(
            any("absolute macOS home path" in finding for finding in findings),
            msg=findings,
        )

    def test_release_audit_rejects_unpinned_xemu_build_path(self) -> None:
        cases = {
            "runtime/xemu/xemu.app/Contents/MacOS/xemu": (
                b"binary\0/Users/"
                b"unrelated-builder/xemu/source.cc\0"
            ),
            "xemu-substituted/runtime/xemu/xenia_canary.exe": (
                b"binary\0/Users/"
                b"unrelated-builder/xemu/source.cc\0"
            ),
            "release/source/vendor/substituted.bin": (
                b"binary\0/Users/"
                b"unrelated-builder/vendor/source.cc\0"
            ),
        }
        for index, (member, data) in enumerate(cases.items()):
            with self.subTest(member=member):
                artifact = self.root / f"substituted-runtime-{index}.zip"
                with zipfile.ZipFile(artifact, "w") as archive:
                    archive.writestr(member, data)
                audited = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "scripts" / "audit_release.py"),
                        "--artifact",
                        str(artifact),
                    ],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertNotEqual(audited.returncode, 0)
                self.assertIn("absolute macOS home path", audited.stderr)

    def test_release_audit_does_not_trust_generic_vendor_paths(self) -> None:
        unfinished = b"TO" + b"DO: substituted content"
        labels = (
            "release.zip!xemu-substituted/file.c",
            "release.zip!source/vendor/file.c",
            "release.zip!android/emulator/patches/0001-android-core.patch",
        )
        for label in labels:
            with self.subTest(label=label):
                findings: list[str] = []
                inspect_bytes(label, unfinished, findings)
                self.assertTrue(
                    any(
                        "unfinished implementation marker" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

        archive_name = "gdox-0.2.0-android-corresponding-source"
        archive_path = self.root / f"{archive_name}.tar.gz"
        manifest = (
            "\n".join(canonical_manifest_lines("0" * 40)) + "\n"
        ).encode()
        with tarfile.open(archive_path, "w:gz") as archive:
            info = tarfile.TarInfo(f"{archive_name}/SOURCE_MANIFEST.txt")
            info.size = len(manifest)
            archive.addfile(info, BytesIO(manifest))
            for component in ANDROID_SOURCE_COMPONENTS:
                data = unfinished
                info = tarfile.TarInfo(
                    f"{archive_name}/source/{component}/source.c"
                )
                info.size = len(data)
                archive.addfile(info, BytesIO(data))
        vendor_label = (
            f"{archive_path}!"
            f"{archive_name}/source/xemu/source.c"
        )
        findings = []
        inspect_bytes(vendor_label, unfinished, findings)
        self.assertTrue(
            any(
                "unfinished implementation marker" in finding
                for finding in findings
            ),
            msg=findings,
        )

    def test_content_bound_android_third_party_source_exemptions(self) -> None:
        static_vendor_lines = (
            b"/" + b"home/private-builder/source.cc",
            b"/" + b"Users/private-builder/source.cc",
            b"C:" + b"\\Users\\private-builder\\source.cc",
            b"/dev/" + b"bsg/1:2:3:4",
            b"/run/" + b"user/1234",
            b"bottom-" + b"left monitor",
            b"-----BEGIN " + b"PRIVATE KEY-----",
            b"A" * 48,
            b"pass" + b'word="upstream-fixture"',
            b"person" + b"@upstream.test",
        )
        static_vendor_text = b"\n".join(static_vendor_lines) + b"\n"
        static_vendor_cases = {
            "text": static_vendor_text,
            "binary": b"MZ\0" + static_vendor_text + b"\0",
            "binary-utf16": (
                b"MZ\0\0"
                + static_vendor_text.decode("ascii").encode("utf-16le")
                + b"\0\0"
            ),
        }
        for case, data in static_vendor_cases.items():
            with self.subTest(case=case), self.trusted_android_source_fixture(
                "3" * 40, data
            ) as (
                    archive_path,
                    archive_name,
                    source_data,
            ):
                    vendor_label = (
                        f"{archive_path}!{archive_name}/source/xemu/source.c"
                    )
                    findings = []
                    inspect_bytes(vendor_label, source_data, findings)
                    self.assertEqual(findings, [])

                    strict_members = (
                        f"{archive_name}/source/gdox/source.c",
                        f"{archive_name}/source/xemu/not-source.c",
                        f"{archive_name}/source/xemu-substituted/source.c",
                    )
                    for member in strict_members:
                        findings = []
                        inspect_bytes(
                            f"{archive_path}!{member}",
                            source_data,
                            findings,
                        )
                        self.assertTrue(
                            any(
                                "absolute macOS home path" in finding
                                for finding in findings
                            ),
                            msg=(member, findings),
                        )

    def test_android_third_party_source_keeps_runtime_marker_checks(self) -> None:
        marker = b"/home/active-builder"
        cases = {
            "text": marker + b"/source.c\n",
            "binary-ascii": b"MZ\0" + marker + b"/source.c\0",
            "binary-utf16": b"MZ\0" + marker.decode().encode("utf-16le"),
        }
        for case, data in cases.items():
            with self.subTest(case=case), self.trusted_android_source_fixture(
                "1" * 40, data
            ) as (
                    archive_path,
                    archive_name,
                    source_data,
            ):
                    for component in ("glib", "gdox"):
                        findings: list[str] = []
                        label = (
                            f"{archive_path}!{archive_name}/source/"
                            f"{component}/source.c"
                        )
                        with patch.object(
                            audit_release,
                            "RUNTIME_MARKERS",
                            [("test runtime marker", marker)],
                        ):
                            inspect_bytes(label, source_data, findings)
                        self.assertTrue(
                            any(
                                "test runtime marker" in item
                                for item in findings
                            ),
                            msg=(component, findings),
                        )

    def test_content_bound_pax_paths_and_symlink_targets(self) -> None:
        source_data = b"source"
        long_path = (
            "fixtures/"
            + "nested/" * 14
            + "home/private-builder/source.c"
        )
        tree_digest = canonical_tree_digest(
            [
                SourceTreeEntry(
                    long_path,
                    "file",
                    0o644,
                    size=len(source_data),
                    content_sha256=hashlib.sha256(source_data).hexdigest(),
                ),
                SourceTreeEntry(
                    "fixture-link",
                    "symlink",
                    0o777,
                    symlink_target=long_path,
                ),
            ]
        )
        locked = dependency_lock()
        for key in ANDROID_COMPONENT_TREE_KEYS.values():
            locked[key] = tree_digest
        archive_name = "gdox-0.2.0-android-corresponding-source"
        archive_path = self.root / f"{archive_name}.tar.gz"
        with patch.object(
            android_source_provenance,
            "dependency_lock",
            return_value=locked,
        ):
            manifest = (
                "\n".join(canonical_manifest_lines("5" * 40)) + "\n"
            ).encode()
            with tarfile.open(
                archive_path,
                "w:gz",
                format=tarfile.PAX_FORMAT,
            ) as archive:
                info = tarfile.TarInfo(f"{archive_name}/SOURCE_MANIFEST.txt")
                info.size = len(manifest)
                archive.addfile(info, BytesIO(manifest))
                for component in ANDROID_SOURCE_COMPONENTS:
                    if component == "gdox":
                        path = f"{archive_name}/source/gdox/source.c"
                        info = tarfile.TarInfo(path)
                        info.size = len(source_data)
                        archive.addfile(info, BytesIO(source_data))
                        continue
                    prefix = f"{archive_name}/source/{component}/"
                    info = tarfile.TarInfo(prefix + long_path)
                    info.size = len(source_data)
                    archive.addfile(info, BytesIO(source_data))
                    info = tarfile.TarInfo(prefix + "fixture-link")
                    info.type = tarfile.SYMTYPE
                    info.linkname = long_path
                    info.mode = 0o777
                    archive.addfile(info)
            release_audit_provenance.cached_android_source_validation.cache_clear()
            findings: list[str] = []
            inspect_archive(archive_path, findings)
            self.assertEqual(findings, [])

            member = f"{archive_name}/source/glib/{long_path}"
            findings = []
            inspect_bytes(
                f"{archive_path}!{member} metadata 2",
                b"path=/" + b"home/substituted/source.c",
                findings,
            )
            self.assertTrue(
                any("absolute Linux home path" in item for item in findings),
                msg=findings,
            )
            link = f"{archive_name}/source/glib/fixture-link"
            findings = []
            inspect_bytes(
                f"{archive_path}!{link} link target",
                b"fixtures/" + b"home/substituted/source.c",
                findings,
            )
            self.assertTrue(
                any("absolute Linux home path" in item for item in findings),
                msg=findings,
            )

    def test_malformed_android_provenance_gets_no_source_exemption(self) -> None:
        archive_name = "gdox-0.2.0-android-corresponding-source"
        archive_path = self.root / f"{archive_name}.tar.gz"
        lines = list(canonical_manifest_lines("2" * 40))
        xemu_index = next(
            index for index, line in enumerate(lines) if line.startswith("xemu ")
        )
        lines[xemu_index] = "xemu " + "f" * 40 + " + patch series " + "0" * 64
        manifest = ("\n".join(lines) + "\n").encode()
        with tarfile.open(archive_path, "w:gz") as archive:
            info = tarfile.TarInfo(f"{archive_name}/SOURCE_MANIFEST.txt")
            info.size = len(manifest)
            archive.addfile(info, BytesIO(manifest))
            for component in ANDROID_SOURCE_COMPONENTS:
                data = b"source"
                info = tarfile.TarInfo(
                    f"{archive_name}/source/{component}/source.c"
                )
                info.size = len(data)
                archive.addfile(info, BytesIO(data))
        label = f"{archive_path}!{archive_name}/source/xemu/private.bin"
        findings: list[str] = []
        inspect_bytes(
            label,
            b"MZ\0/" + b"Users/private-builder/source.c\0",
            findings,
        )
        self.assertTrue(
            any("absolute macOS home path" in item for item in findings),
            msg=findings,
        )

    def test_android_source_archive_structure_must_match_manifest(self) -> None:
        base_name = "gdox-0.2.0-android-corresponding-source"
        canonical = list(canonical_manifest_lines("3" * 40))
        cases = {
            "duplicate": (
                [*canonical, canonical[2]],
                set(ANDROID_SOURCE_COMPONENTS),
            ),
            "missing-root": (
                canonical,
                set(ANDROID_SOURCE_COMPONENTS) - {"xemu"},
            ),
            "extra-root": (
                canonical,
                set(ANDROID_SOURCE_COMPONENTS) | {"future"},
            ),
        }
        for case, (manifest_lines, source_roots) in cases.items():
            with self.subTest(case=case):
                archive_path = self.root / case / f"{base_name}.tar.gz"
                archive_path.parent.mkdir()
                manifest = ("\n".join(manifest_lines) + "\n").encode()
                with tarfile.open(archive_path, "w:gz") as archive:
                    info = tarfile.TarInfo(
                        f"{base_name}/SOURCE_MANIFEST.txt"
                    )
                    info.size = len(manifest)
                    archive.addfile(info, BytesIO(manifest))
                    for component in source_roots:
                        data = b"source"
                        info = tarfile.TarInfo(
                            f"{base_name}/source/{component}/source.c"
                        )
                        info.size = len(data)
                        archive.addfile(info, BytesIO(data))
                findings: list[str] = []
                inspect_archive(archive_path, findings)
                self.assertTrue(
                    any(
                        "invalid Android corresponding-source structure"
                        in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_content_bound_android_structure_rejects_member_anomalies(
        self,
    ) -> None:
        source_data = b"source"
        tree_digest = canonical_tree_digest(
            [
                SourceTreeEntry(
                    "source.c",
                    "file",
                    0o644,
                    size=len(source_data),
                    content_sha256=hashlib.sha256(source_data).hexdigest(),
                )
            ]
        )
        locked = dependency_lock()
        for key in ANDROID_COMPONENT_TREE_KEYS.values():
            locked[key] = tree_digest
        cases = (
            "duplicate",
            "unsupported",
            "missing",
            "extra",
            "setuid",
            "noncanonical-file-mode",
            "noncanonical-manifest-mode",
            "contiguous-file-type",
            "legacy-file-type",
            "contiguous-manifest-type",
        )
        with patch.object(
            android_source_provenance,
            "dependency_lock",
            return_value=locked,
        ):
            manifest = (
                "\n".join(canonical_manifest_lines("4" * 40)) + "\n"
            ).encode()
            for case in cases:
                with self.subTest(case=case):
                    archive_name = (
                        "gdox-0.2.0-android-corresponding-source"
                    )
                    archive_path = self.root / case / f"{archive_name}.tar.gz"
                    archive_path.parent.mkdir()
                    with tarfile.open(archive_path, "w:gz") as archive:
                        info = tarfile.TarInfo(
                            f"{archive_name}/SOURCE_MANIFEST.txt"
                        )
                        info.size = len(manifest)
                        if case == "noncanonical-manifest-mode":
                            info.mode = 0o600
                        if case == "contiguous-manifest-type":
                            info.type = tarfile.CONTTYPE
                        archive.addfile(info, BytesIO(manifest))
                        for component in ANDROID_SOURCE_COMPONENTS:
                            if case == "missing" and component == "xemu":
                                continue
                            info = tarfile.TarInfo(
                                f"{archive_name}/source/{component}/source.c"
                            )
                            if case == "unsupported" and component == "xemu":
                                info.type = tarfile.FIFOTYPE
                                archive.addfile(info)
                            else:
                                info.size = len(source_data)
                                if component == "xemu" and case == "setuid":
                                    info.mode = 0o4755
                                if (
                                    component == "xemu"
                                    and case == "noncanonical-file-mode"
                                ):
                                    info.mode = 0o700
                                if (
                                    component == "xemu"
                                    and case == "contiguous-file-type"
                                ):
                                    info.type = tarfile.CONTTYPE
                                if (
                                    component == "xemu"
                                    and case == "legacy-file-type"
                                ):
                                    info.type = tarfile.AREGTYPE
                                archive.addfile(info, BytesIO(source_data))
                                if case == "duplicate" and component == "xemu":
                                    archive.addfile(info, BytesIO(source_data))
                        if case == "extra":
                            info = tarfile.TarInfo(
                                f"{archive_name}/source/future/source.c"
                            )
                            info.size = len(source_data)
                            archive.addfile(info, BytesIO(source_data))
                    (
                        release_audit_provenance
                        .cached_android_source_validation.cache_clear()
                    )
                    self.assertEqual(
                        release_audit_provenance.android_source_components(
                            str(archive_path)
                        ),
                        frozenset(),
                    )

    def test_android_core_patch_only_exempts_upstream_todos(self) -> None:
        data = (
            b"TO"
            + b"DO\nHA"
            + b"CK\nX"
            + b"XX\nT"
            + b"BD\nPLACE"
            + b"HOLDER\nchat"
            + b"gpt\n"
        )
        findings: list[str] = []
        inspect_bytes(
            str(ROOT / "android/emulator/patches/0001-android-core.patch"),
            data,
            findings,
        )
        self.assertFalse(
            any("unfinished implementation marker" in item for item in findings)
        )
        self.assertTrue(
            any("automated authorship reference" in item for item in findings)
        )

    def test_patch_context_does_not_claim_upstream_work_is_unfinished(self) -> None:
        label = "packaging/xemu/patches/0001-runtime.patch"
        context = (
            b" context with TO"
            + b"DO, HA"
            + b"CK, X"
            + b"XX, T"
            + b"BD, and PLACE"
            + b"HOLDER\n-cleaned FIX"
            + b"ME\n"
        )
        findings: list[str] = []
        inspect_bytes(label, context, findings)
        self.assertFalse(
            any("unfinished implementation marker" in item for item in findings)
        )

        added = b"+new PLACE" + b"HOLDER remains\n"
        findings = []
        inspect_bytes(label, added, findings)
        self.assertTrue(
            any("unfinished implementation marker" in item for item in findings)
        )

    def test_release_audit_rejects_expanded_authorship_references(self) -> None:
        cases = (
            b"gem" + b"ini",
            b"anth" + b"ropic",
            b"l" + b"lm",
        )
        for data in cases:
            with self.subTest(data=data):
                findings: list[str] = []
                inspect_bytes("first-party-source.txt", data, findings)
                self.assertTrue(
                    any(
                        "automated authorship reference" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_rejects_expanded_content_attribution(self) -> None:
        cases = (
            b"a" + b"i-assisted",
            b"created with a" + b"i",
            b"using a" + b"i assistance",
            b"a" + b"i support",
        )
        for data in cases:
            with self.subTest(data=data):
                findings: list[str] = []
                inspect_bytes("first-party-source.txt", data, findings)
                self.assertTrue(
                    any(
                        "automated-content attribution" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_rejects_expanded_unfinished_markers(self) -> None:
        cases = (
            b"HA" + b"CK",
            b"X" + b"XX",
            b"T" + b"BD",
            b"PLACE" + b"HOLDER",
        )
        for data in cases:
            with self.subTest(data=data):
                findings: list[str] = []
                inspect_bytes("first-party-source.txt", data, findings)
                self.assertTrue(
                    any(
                        "unfinished implementation marker" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_rejects_expanded_unprofessional_language(self) -> None:
        cases = (
            b"sh" + b"it",
            b"bull" + b"sh" + b"it",
            b"motherfu" + b"cker",
            b"ass" + b"hole",
            b"dumb" + b"ass",
            b"bi" + b"tch",
        )
        for data in cases:
            with self.subTest(data=data):
                findings: list[str] = []
                inspect_bytes("first-party-source.txt", data, findings)
                self.assertTrue(
                    any(
                        "unprofessional language" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_scans_hygiene_in_third_party_notice_paths(self) -> None:
        data = (
            b"TO"
            b"DO: substituted notice\nprivate.person"
            b"@corp.test\n"
        )
        labels = (
            "release/runtime/xemu/licenses/not-a-license.txt",
            "release/runtime/xemu/LICENSE.txt.evil",
            "release/runtime/hdd/LICENSE.txt/private.txt",
            "release/runtime/xemu/AppDir/usr/share/doc/fake/copyright",
        )
        for label in labels:
            with self.subTest(label=label):
                findings: list[str] = []
                inspect_bytes(label, data, findings)
                self.assertTrue(
                    any(
                        "unfinished implementation marker" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )
                self.assertTrue(
                    any(
                        "personal email address" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_rejects_wide_runtime_markers(self) -> None:
        marker = "audit-private-host"
        artifact = self.artifact(
            b"MZ\0binary-data\0" + marker.encode("utf-16le"),
            "private-marker.exe",
        )
        environment = os.environ.copy()
        environment["GDOX_AUDIT_FORBID"] = marker
        audited = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(artifact),
            ],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(audited.returncode, 0)
        self.assertIn("GDOX_AUDIT_FORBID item 0", audited.stderr)

    def test_filesystem_symlink_audit_is_contained_and_non_following(self) -> None:
        stage = self.root / "stage"
        stage.mkdir()
        target = stage / "runtime" / "xemu"
        target.parent.mkdir()
        target.write_bytes(
            b"/" + b"Users/" + b"private-builder/source.cc"
        )
        internal = stage / "xemu"
        internal.symlink_to(Path("runtime") / "xemu")
        findings: list[str] = []
        inspect_filesystem_symlink(stage, internal, findings)
        self.assertEqual(findings, [])

        external_target = self.root / "external"
        external_target.write_bytes(b"outside")
        external = stage / "external"
        external.symlink_to(external_target)
        findings = []
        inspect_filesystem_symlink(stage, external, findings)
        self.assertTrue(
            any("unsafe filesystem link target" in item for item in findings),
            msg=findings,
        )

        broken = stage / "broken"
        broken.symlink_to("missing")
        findings = []
        inspect_filesystem_symlink(stage, broken, findings)
        self.assertTrue(
            any("broken filesystem link target" in item for item in findings),
            msg=findings,
        )

    def test_explicit_stage_audit_scans_excluded_source_directory_names(self) -> None:
        stage = self.root / "stage"
        hidden = stage / "dist" / "secret.txt"
        hidden.parent.mkdir(parents=True)
        hidden.write_bytes(b"pass" + b'word="private-secret"')
        (stage / "visible").symlink_to(Path("dist") / "secret.txt")
        audited = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--path",
                str(stage),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(audited.returncode, 0)
        self.assertIn("credential-shaped token", audited.stderr)

    def test_release_audit_scans_sensitive_binary_text(self) -> None:
        cases = {
            "host-assigned BSG address": b"/dev/" + b"bsg/1:2:3:4",
            "host-assigned runtime UID": b"/run/" + b"user/1000",
            "desktop/monitor placement rule": (
                b"bottom-left " + b"monitor"
            ),
            "private key material": (
                b"-----BEGIN "
                + b"PRIVATE KEY-----\n"
                + b"A" * 48
            ),
            "credential-shaped token": (
                b"pass" + b'word="private-secret"'
            ),
            "personal email address": b"person" + b"@corp.test",
        }
        for rule_label, value in cases.items():
            for encoding, encoded in {
                "ascii": value,
                "utf-16le": value.decode("ascii").encode("utf-16le"),
            }.items():
                with self.subTest(rule=rule_label, encoding=encoding):
                    findings: list[str] = []
                    inspect_bytes("nul-bearing-binary", b"MZ\0" + encoded, findings)
                    self.assertTrue(
                        any(rule_label in finding for finding in findings),
                        msg=findings,
                    )

    def test_release_audit_scans_private_key_header_variants(self) -> None:
        values = (
            b"-----BEGIN "
            + b"DSA PRIVATE KEY"
            + b"-----\n"
            + b"A" * 48,
            b"-----BEGIN "
            + b"ENCRYPTED PRIVATE KEY"
            + b"-----\n"
            + b"A" * 48,
            b"-----BEGIN "
            + b"PGP PRIVATE KEY BLOCK"
            + b"-----\n"
            + b"Version: GnuPG\n"
            + b"Comment: test fixture\n\n"
            + b"A" * 48,
        )
        for value in values:
            for container, encoded in (
                ("text", value),
                ("binary", b"MZ\0" + value),
                ("utf-16le", b"MZ\0" + value.decode().encode("utf-16le")),
            ):
                with self.subTest(value=value[:40], container=container):
                    findings: list[str] = []
                    inspect_bytes("private-key-candidate", encoded, findings)
                    self.assertTrue(
                        any(
                            "private key material" in finding
                            for finding in findings
                        ),
                        msg=findings,
                    )

    def test_large_nul_free_text_receives_full_hygiene_scan(self) -> None:
        data = (
            b"A" * (8 * 1024 * 1024 + 1)
            + b"\n"
            + b"TO"
            + b"DO: unfinished"
        )
        findings: list[str] = []
        inspect_bytes("large-source.txt", data, findings)
        self.assertTrue(
            any(
                "unfinished implementation marker" in finding
                for finding in findings
            ),
            msg=findings,
        )

    def test_binary_exemptions_remain_narrow(self) -> None:
        runtime_data = b"\0".join(
            (
                b"MZ",
                b"/" + b"Users/" + b"private-builder/source.cc",
                b"person" + b"@corp.test",
                b"pass" + b'word="private-secret"',
                b"-----BEGIN "
                + b"PRIVATE KEY-----\n"
                + b"A" * 48,
            )
        )
        findings: list[str] = []
        with patch.object(
            audit_release,
            "is_verified_xenia_runtime_file",
            return_value=True,
        ):
            inspect_bytes(
                "release/runtime/xenia/xenia_canary.exe",
                runtime_data,
                findings,
            )
        self.assertFalse(
            any("absolute macOS home path" in item for item in findings),
            msg=findings,
        )
        self.assertFalse(
            any("personal email address" in item for item in findings),
            msg=findings,
        )
        self.assertTrue(
            any("credential-shaped token" in item for item in findings),
            msg=findings,
        )
        self.assertTrue(
            any("private key material" in item for item in findings),
            msg=findings,
        )

        notice_data = b"\n".join(
            (
                b"notice",
                b"TO" + b"DO: supplied by the upstream notice",
                b"person" + b"@corp.test",
                b"pass" + b'word="private-secret"',
            )
        )
        findings = []
        with patch.object(
            audit_release,
            "is_verified_third_party_notice",
            return_value=True,
        ):
            inspect_bytes("release/runtime/xemu/LICENSE.txt", notice_data, findings)
        self.assertFalse(
            any("personal email address" in item for item in findings),
            msg=findings,
        )
        self.assertFalse(
            any(
                "unfinished implementation marker" in item
                for item in findings
            ),
            msg=findings,
        )
        self.assertTrue(
            any("credential-shaped token" in item for item in findings),
            msg=findings,
        )

    def test_public_project_contact_exemption_is_exact(self) -> None:
        public_contact = "qemu-devel@nongnu.org"
        cases = (
            public_contact.encode(),
            public_contact.upper().encode(),
            b"MZ\0" + public_contact.encode() + b"\0",
            b"MZ\0\0" + public_contact.encode("utf-16le") + b"\0\0",
        )
        for data in cases:
            with self.subTest(data=data):
                findings: list[str] = []
                inspect_bytes("runtime/libxemu.so", data, findings)
                self.assertFalse(
                    any(
                        "personal email address" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

        for data in (
            b"MZ\0"
            + public_contact.encode()
            + b"\0person"
            + b"@corp.test\0",
            b"MZ\0\0"
            + public_contact.encode("utf-16le")
            + b"\0\0"
            + ("person" + "@corp.test").encode("utf-16le")
            + b"\0\0",
        ):
            findings = []
            inspect_bytes(
                "runtime/libxemu.so",
                data,
                findings,
            )
            self.assertTrue(
                any(
                    "personal email address" in finding
                    for finding in findings
                ),
                msg=findings,
            )

        for data in (
            ("x" + public_contact).encode(),
            (public_contact + ".invalid").encode(),
        ):
            findings = []
            inspect_bytes("runtime/libxemu.so", data, findings)
            self.assertTrue(
                any(
                    "personal email address" in finding
                    for finding in findings
                ),
                msg=findings,
            )

    def test_xenia_runtime_exemption_requires_exact_identity(self) -> None:
        data = b"MZ\0project-list" + b"@example.org"
        digest = hashlib.sha256(data).hexdigest()
        identity = (
            "runtime/xenia/revision/xenia_canary.exe",
            "reviewed-xenia.zip",
            "xenia_canary.exe",
            len(data),
            digest,
        )
        with patch.object(
            release_audit_provenance,
            "XENIA_RUNTIME_FILES",
            (identity,),
        ):
            verifier = release_audit_provenance.is_verified_xenia_runtime_file
            self.assertTrue(
                verifier(
                    "release/runtime/xenia/revision/xenia_canary.exe",
                    data,
                )
            )
            self.assertTrue(
                verifier(
                    "reviewed-xenia.zip!xenia_canary.exe",
                    data,
                )
            )
            self.assertFalse(
                verifier(
                    "substituted.zip!xenia_canary.exe",
                    data,
                )
            )
            self.assertFalse(
                verifier(
                    "release/runtime/xenia/other/xenia_canary.exe",
                    data,
                )
            )
            self.assertFalse(
                verifier(
                    "release/runtime/xenia/revision/xenia_canary.exe",
                    data + b"changed",
                )
            )

    def test_known_upstream_test_key_exemption_requires_exact_identity(self) -> None:
        data = b"\0".join(
            (
                b"ELF",
                b"-----BEGIN " + b"PRIVATE KEY-----\n" + b"A" * 48,
                b"pass" + b'word="private-secret"',
            )
        )
        member = release_audit_provenance.XEMU_KNOWN_TEST_KEY_MEMBER
        identity = ((member, len(data), hashlib.sha256(data).hexdigest()),)
        findings: list[str] = []
        with patch.object(
            release_audit_provenance,
            "XEMU_PRIVACY_FILES",
            identity,
        ):
            self.assertTrue(
                release_audit_provenance.is_verified_xemu_known_test_key_file(
                    member,
                    data,
                )
            )
            self.assertFalse(
                release_audit_provenance.is_verified_xemu_known_test_key_file(
                    f"{member}.moved",
                    data,
                )
            )
            self.assertFalse(
                release_audit_provenance.is_verified_xemu_known_test_key_file(
                    member,
                    data + b"changed",
                )
            )
            inspect_bytes(
                f"release/{member}",
                data,
                findings,
            )
        self.assertFalse(
            any("private key material" in item for item in findings),
            msg=findings,
        )
        self.assertTrue(
            any("credential-shaped token" in item for item in findings),
            msg=findings,
        )

        findings = []
        inspect_bytes(
            f"release/{member}",
            data,
            findings,
        )
        self.assertTrue(
            any("private key material" in item for item in findings),
            msg=findings,
        )
        self.assertTrue(
            any("credential-shaped token" in item for item in findings),
            msg=findings,
        )


if __name__ == "__main__":
    unittest.main()
