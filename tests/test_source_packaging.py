"""Regression tests for exact-commit GDOX source packaging."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
from package_source import create_source_archive, validate_clean_repository
from package_corresponding_source import (
    audit_corresponding_source_archive,
    create_corresponding_source_archive,
)


class SourcePackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="gdox-source-packaging-"
        )
        self.root = Path(self.temporary.name)
        self.repository = self.root / "repository"
        self.repository.mkdir()
        self.git("init", "--quiet")
        self.git("config", "user.name", "GDOX Test")
        self.git("config", "user.email", "gdox-test@example.com")
        self.git("config", "core.symlinks", "false")

        (self.repository / "plain.txt").write_bytes(b"committed\n")
        executable = self.repository / "tool.sh"
        executable.write_bytes(b"#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
        self.git("add", "plain.txt", "tool.sh")
        self.git("update-index", "--chmod=+x", "tool.sh")
        link_blob = subprocess.run(
            ["git", "hash-object", "-w", "--stdin"],
            cwd=self.repository,
            input=b"plain.txt",
            check=True,
            capture_output=True,
        ).stdout.decode("ascii").strip()
        self.git(
            "update-index",
            "--add",
            "--cacheinfo",
            f"120000,{link_blob},link",
        )
        (self.repository / "link").write_bytes(b"plain.txt")
        self.git("commit", "--quiet", "-m", "fixture")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def git(self, *arguments: str) -> None:
        subprocess.run(
            ["git", *arguments],
            cwd=self.repository,
            check=True,
            capture_output=True,
        )

    def test_public_source_packaging_requires_a_clean_repository(self) -> None:
        validate_clean_repository(self.repository)

        (self.repository / "plain.txt").write_bytes(b"modified\n")
        with self.assertRaisesRegex(RuntimeError, "uncommitted source changes"):
            validate_clean_repository(self.repository)

        self.git("restore", "plain.txt")
        (self.repository / "untracked.txt").write_bytes(b"untracked\n")
        with self.assertRaisesRegex(RuntimeError, "uncommitted source changes"):
            validate_clean_repository(self.repository)

    def test_archive_reads_content_and_modes_from_the_commit_tree(self) -> None:
        (self.repository / "plain.txt").write_bytes(b"worktree-only\n")
        (self.repository / "tool.sh").unlink()
        (self.repository / "untracked.txt").write_bytes(b"untracked\n")

        output = self.root / "source.tar.gz"
        create_source_archive(self.repository, output, "gdox-test-source")
        repeated = self.root / "source-repeated.tar.gz"
        create_source_archive(
            self.repository,
            repeated,
            "gdox-test-source",
        )
        self.assertEqual(output.read_bytes(), repeated.read_bytes())

        with tarfile.open(output, "r:gz") as archive:
            members = {member.name: member for member in archive.getmembers()}
            self.assertEqual(
                set(members),
                {
                    "gdox-test-source/link",
                    "gdox-test-source/plain.txt",
                    "gdox-test-source/tool.sh",
                },
            )
            plain = members["gdox-test-source/plain.txt"]
            source = archive.extractfile(plain)
            self.assertIsNotNone(source)
            self.assertEqual(source.read(), b"committed\n")
            self.assertEqual(plain.mode, 0o644)
            self.assertEqual(members["gdox-test-source/tool.sh"].mode, 0o755)
            link = members["gdox-test-source/link"]
            self.assertTrue(link.issym())
            self.assertEqual(link.linkname, "plain.txt")

    def test_corresponding_source_combines_exact_components_once(self) -> None:
        component = self.root / "component.tar.gz"
        component.write_bytes(b"component source\n")
        digest = hashlib.sha256(component.read_bytes()).hexdigest()
        output = self.root / "gdox-0.2.0-corresponding-source.tar.gz"
        components = ((component.name, component, digest),)
        create_corresponding_source_archive(
            self.repository,
            output,
            "0.2.0",
            components,
        )
        repeated = self.root / "corresponding-repeated.tar.gz"
        create_corresponding_source_archive(
            self.repository,
            repeated,
            "0.2.0",
            components,
        )
        self.assertEqual(output.read_bytes(), repeated.read_bytes())

        definitions = ((component.name, component.stat().st_size, digest),)
        audit_corresponding_source_archive(
            self.repository,
            output,
            "0.2.0",
            definitions,
        )

        root = "gdox-0.2.0-corresponding-source"
        with tarfile.open(output, "r:gz") as archive:
            members = {member.name: member for member in archive.getmembers()}
            self.assertEqual(
                set(members),
                {
                    f"{root}/CONTENTS.txt",
                    f"{root}/components/component.tar.gz",
                    f"{root}/gdox/link",
                    f"{root}/gdox/plain.txt",
                    f"{root}/gdox/tool.sh",
                },
            )
            contents = archive.extractfile(members[f"{root}/CONTENTS.txt"])
            self.assertIsNotNone(contents)
            self.assertIn(
                f"{digest}  components/component.tar.gz",
                contents.read().decode("utf-8"),
            )

        empty_root = self.root / "empty"
        empty_root.mkdir()
        empty = empty_root / "gdox-0.2.0-corresponding-source.tar.gz"
        with tarfile.open(empty, "w:gz"):
            pass
        with self.assertRaisesRegex(ValueError, "missing, extra, or duplicate"):
            audit_corresponding_source_archive(
                self.repository,
                empty,
                "0.2.0",
                definitions,
            )


if __name__ == "__main__":
    unittest.main()
