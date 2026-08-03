"""Regression tests for portable and safe release archives."""

from __future__ import annotations

import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
import warnings
import zipfile
from io import BytesIO
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
import release_archive_audit
from audit_release import inspect_archive
from release_archive import create_archive


class ReleaseArchiveAuditTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="gdox-release-archive-audit-"
        )
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_release_archives_are_independent_of_output_name_and_mtime(
        self,
    ) -> None:
        stage = self.root / "gdox-release"
        stage.mkdir()
        executable = stage / "gdox"
        executable.write_bytes(b"executable\n")
        executable.chmod(0o755)

        first_tar = self.root / "first.tar.gz"
        first_zip = self.root / "first.zip"
        create_archive(stage, first_tar, windows=False)
        create_archive(stage, first_zip, windows=True)

        executable.chmod(0o755)
        executable.touch()
        second_tar = self.root / "renamed.tar.gz"
        second_zip = self.root / "renamed.zip"
        create_archive(stage, second_tar, windows=False)
        create_archive(stage, second_zip, windows=True)

        self.assertEqual(first_tar.read_bytes(), second_tar.read_bytes())
        self.assertEqual(first_zip.read_bytes(), second_zip.read_bytes())

    def test_release_audit_rejects_unsafe_archive_paths_and_links(self) -> None:
        unsafe_zip = self.root / "unsafe-members.zip"
        with zipfile.ZipFile(unsafe_zip, "w") as archive:
            archive.writestr("../outside", b"data")
            archive.writestr("C:/outside", b"data")
            fifo = zipfile.ZipInfo("package/fifo")
            fifo.create_system = 3
            fifo.external_attr = (stat.S_IFIFO | 0o644) << 16
            archive.writestr(fifo, b"")
        zip_audit = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(unsafe_zip),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(zip_audit.returncode, 0)
        self.assertIn("unsafe archive member path", zip_audit.stderr)
        self.assertIn("unsupported archive member type", zip_audit.stderr)

        unsafe_tar = self.root / "unsafe-links.tar"
        with tarfile.open(unsafe_tar, "w") as archive:
            link = tarfile.TarInfo("package/bin/escape")
            link.type = tarfile.SYMTYPE
            link.linkname = "../../outside"
            archive.addfile(link)
            hardlink = tarfile.TarInfo("package/bin/hardlink")
            hardlink.type = tarfile.LNKTYPE
            hardlink.linkname = "outside"
            archive.addfile(hardlink)
        tar_audit = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(unsafe_tar),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(tar_audit.returncode, 0)
        self.assertIn("unsafe archive link target", tar_audit.stderr)

    def test_release_audit_rejects_nonportable_archive_member_names(self) -> None:
        names = (
            "C:outside",
            "package/AUX.txt",
            "package/file:stream",
            "package/trailing-dot.",
            "package/trailing-space ",
            "package/control-\nname",
            "package/less<than",
            "package/greater>than",
            'package/double"quote',
            "package/vertical|bar",
            "package/question?mark",
            "package/asterisk*",
        )
        for index, name in enumerate(names):
            with self.subTest(name=name):
                artifact = self.root / f"nonportable-{index}.zip"
                with zipfile.ZipFile(artifact, "w") as archive:
                    archive.writestr(name, b"data")
                findings: list[str] = []
                inspect_archive(artifact, findings)
                self.assertTrue(
                    any(
                        "unsafe archive member path" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_rejects_forbidden_characters_in_links(self) -> None:
        forbidden = '<>"|?*'
        for index, character in enumerate(forbidden):
            with self.subTest(character=character):
                artifact = self.root / f"unsafe-link-{index}.tar"
                with tarfile.open(artifact, "w") as archive:
                    link = tarfile.TarInfo("package/link")
                    link.type = tarfile.SYMTYPE
                    link.linkname = f"bad{character}target"
                    archive.addfile(link)
                findings: list[str] = []
                inspect_archive(artifact, findings)
                self.assertTrue(
                    any(
                        "unsafe archive link target" in finding
                        for finding in findings
                    ),
                    msg=findings,
                )

    def test_release_audit_rejects_archive_member_aliases(self) -> None:
        cases = (
            (
                "exact duplicate",
                ("package/file", "package/file"),
                "duplicate archive member name",
            ),
            (
                "case collision",
                ("package/File", "package/file"),
                "portable archive member path collision",
            ),
            (
                "Unicode normalization collision",
                ("package/caf\u00e9", "package/cafe\u0301"),
                "portable archive member path collision",
            ),
            (
                "leading-dot collision",
                ("./package/file", "package/file"),
                "portable archive member path collision",
            ),
            (
                "file-directory collision",
                ("package/node", "package/node/"),
                "archive member",
            ),
            (
                "file before descendant",
                ("package/node", "package/node/child"),
                "non-directory ancestor",
            ),
            (
                "file after descendant",
                ("package/node/child", "package/node"),
                "has descendants",
            ),
        )
        for archive_type in ("zip", "tar"):
            for index, (case, names, expected) in enumerate(cases):
                with self.subTest(archive=archive_type, case=case):
                    artifact = self.root / (
                        f"aliases-{archive_type}-{index}.{archive_type}"
                    )
                    if archive_type == "zip":
                        with warnings.catch_warnings():
                            warnings.simplefilter("ignore", UserWarning)
                            with zipfile.ZipFile(artifact, "w") as archive:
                                for name in names:
                                    archive.writestr(name, b"data")
                    else:
                        with tarfile.open(artifact, "w") as archive:
                            for name in names:
                                info = tarfile.TarInfo(name)
                                if name.endswith("/"):
                                    info.type = tarfile.DIRTYPE
                                    archive.addfile(info)
                                else:
                                    info.size = 4
                                    archive.addfile(info, BytesIO(b"data"))
                    findings: list[str] = []
                    inspect_archive(artifact, findings)
                    self.assertTrue(
                        any(expected in finding for finding in findings),
                        msg=findings,
                    )

    @unittest.skipUnless(shutil.which("zstd"), "zstd is unavailable")
    def test_release_audit_inspects_zstd_tar_contents(self) -> None:
        source = self.root / "private-source.tar"
        with tarfile.open(source, "w") as archive:
            data = b"pass" + b'word="private-secret"'
            info = tarfile.TarInfo("package/source.txt")
            info.size = len(data)
            archive.addfile(info, BytesIO(data))
        artifact = self.root / "private-source.tar.zst"
        subprocess.run(
            [
                "zstd",
                "--quiet",
                "--force",
                "-o",
                str(artifact),
                str(source),
            ],
            check=True,
        )
        findings: list[str] = []
        inspect_archive(artifact, findings)
        self.assertTrue(
            any("credential-shaped token" in finding for finding in findings),
            msg=findings,
        )

    def test_zstd_tar_without_decompressor_fails_closed(self) -> None:
        artifact = self.root / "source.tar.zst"
        artifact.write_bytes(b"compressed data")
        findings: list[str] = []
        with patch.object(release_archive_audit.shutil, "which", return_value=None):
            inspect_archive(artifact, findings)
        self.assertTrue(
            any("zstd is required" in finding for finding in findings),
            msg=findings,
        )

    def test_release_audit_scans_archive_member_names(self) -> None:
        artifact = self.root / "private-member-names.zip"
        private_email = "person" + "@corp.test"
        credential = "pass" + 'word="private-secret"'
        with zipfile.ZipFile(artifact, "w") as archive:
            archive.writestr(f"package/{private_email}", b"data")
            archive.writestr(f"package/{credential}", b"data")
        findings: list[str] = []
        inspect_archive(artifact, findings)
        self.assertTrue(
            any("personal email address" in finding for finding in findings),
            msg=findings,
        )
        self.assertTrue(
            any("credential-shaped token" in finding for finding in findings),
            msg=findings,
        )

    def test_release_audit_validates_zip_types_independent_of_names(self) -> None:
        artifact = self.root / "zip-types.zip"
        with zipfile.ZipFile(artifact, "w") as archive:
            fifo = zipfile.ZipInfo("package/fifo/")
            fifo.create_system = 3
            fifo.external_attr = (stat.S_IFIFO | 0o644) << 16
            archive.writestr(fifo, b"")

            symlink = zipfile.ZipInfo("package/link/")
            symlink.create_system = 3
            symlink.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(symlink, b"../../outside")

            regular = zipfile.ZipInfo("package/regular/")
            regular.create_system = 3
            regular.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(regular, b"data")

        findings: list[str] = []
        inspect_archive(artifact, findings)
        self.assertTrue(
            any(
                "unsupported archive member type" in finding
                for finding in findings
            ),
            msg=findings,
        )
        self.assertTrue(
            any(
                "archive member type conflicts with name" in finding
                for finding in findings
            ),
            msg=findings,
        )
        self.assertTrue(
            any(
                "unsafe archive link target" in finding
                for finding in findings
            ),
            msg=findings,
        )

        dos_artifact = self.root / "dos-attributes.zip"
        with zipfile.ZipFile(dos_artifact, "w") as archive:
            compatible = zipfile.ZipInfo("package/file")
            compatible.create_system = 0
            compatible.external_attr = (stat.S_IFIFO | 0o644) << 16
            archive.writestr(compatible, b"data")
        findings = []
        inspect_archive(dos_artifact, findings)
        self.assertFalse(
            any(
                "unsupported archive member type" in finding
                for finding in findings
            ),
            msg=findings,
        )

    def test_release_audit_inspects_archive_metadata(self) -> None:
        metadata_zip = self.root / "private-metadata.zip"
        with zipfile.ZipFile(metadata_zip, "w") as archive:
            archive.comment = (
                b"/Users/"
                b"private-builder"
                b"/source"
            )
            info = zipfile.ZipInfo("package/file")
            info.comment = (
                b"C:\\Users\\"
                b"private-builder"
                b"\\source"
            )
            private_extra = b"\0" + b"pass" + b'word="private-secret"'
            info.extra = (
                b"\xfe\xca"
                + len(private_extra).to_bytes(2, "little")
                + private_extra
            )
            archive.writestr(info, b"data")
        zip_audit = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(metadata_zip),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(zip_audit.returncode, 0)
        self.assertIn("absolute macOS home path", zip_audit.stderr)
        self.assertIn("absolute Windows profile path", zip_audit.stderr)
        self.assertIn("credential-shaped token", zip_audit.stderr)

        metadata_tar = self.root / "private-metadata.tar"
        with tarfile.open(metadata_tar, "w") as archive:
            info = tarfile.TarInfo("package/file")
            info.size = 4
            info.uname = (
                "/Users/"
                + "private-builder"
                + "/source"
            )
            info.gname = "private@" + "example.test"
            info.pax_headers = {
                "comment": "/" + "home/" + "private-builder" + "/source"
            }
            archive.addfile(info, BytesIO(b"data"))
        tar_audit = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(metadata_tar),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(tar_audit.returncode, 0)
        self.assertIn("absolute Linux home path", tar_audit.stderr)
        self.assertIn("absolute macOS home path", tar_audit.stderr)
        self.assertIn("personal email address", tar_audit.stderr)


if __name__ == "__main__":
    unittest.main()
