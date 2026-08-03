"""Regression tests for Android corresponding-source discovery."""

from __future__ import annotations

import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))

from android_source_provenance import (
    ANDROID_GLIB_GIT_SUBPROJECTS,
    ANDROID_NATIVE_GIT_REVISION_KEYS,
    SourceTreeEntry,
    canonical_tree_digest,
)
from package_android_source import (
    add_pristine_tar_source,
    discover_glib_git_subprojects,
    discover_native_dependency_repositories,
    validate_component_set,
    validate_exact_revision,
    validate_native_git_inventory,
    validate_sha256,
)


class AndroidSourcePackagingTest(unittest.TestCase):
    @staticmethod
    def create_native_sources(native_build: Path) -> None:
        glib = native_build / "glib-src" / "subprojects"
        for component in ("libffi", "proxy-libintl"):
            (glib / component / ".git").mkdir(parents=True)
        dependencies = native_build / "_deps"
        for component in (
            "glslang",
            "spirv_reflect",
            "tomlplusplus",
            "vma",
            "volk",
        ):
            (dependencies / f"{component}-src" / ".git").mkdir(
                parents=True
            )

    def test_native_source_discovery_is_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native_build = Path(temporary)
            self.create_native_sources(native_build)
            expected = {
                "glslang",
                "spirv_reflect",
                "tomlplusplus",
                "vma",
                "volk",
            }

            repositories, glib_source = (
                discover_native_dependency_repositories(native_build)
            )
            self.assertEqual({name for name, _ in repositories}, expected)
            self.assertEqual(glib_source, native_build / "glib-src")
            glib_repositories = discover_glib_git_subprojects(glib_source)
            self.assertEqual(
                {name for name, _ in glib_repositories},
                {"libffi", "proxy-libintl"},
            )
            validate_native_git_inventory(
                native_build,
                repositories,
                glib_repositories,
            )

            all_repositories = [
                (name, native_build / name)
                for name in ("gdox", "xemu", "sdl2", "libusb")
            ] + repositories + glib_repositories
            validate_component_set(all_repositories)

            dependencies = native_build / "_deps"
            (dependencies / "future-src").mkdir()
            with self.assertRaisesRegex(
                RuntimeError,
                "future-src is not a Git source repository",
            ):
                discover_native_dependency_repositories(native_build)

    def test_unknown_git_dependency_is_rejected_by_component_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native_build = Path(temporary)
            self.create_native_sources(native_build)
            (native_build / "_deps" / "future-src" / ".git").mkdir(
                parents=True
            )
            repositories, _ = discover_native_dependency_repositories(
                native_build
            )
            all_repositories = [
                (name, native_build / name)
                for name in ("gdox", "xemu", "sdl2", "libusb")
            ] + repositories + [
                ("libffi", native_build / "libffi"),
                ("proxy-libintl", native_build / "proxy-libintl"),
            ]
            with self.assertRaisesRegex(
                RuntimeError,
                "component mismatch",
            ):
                validate_component_set(all_repositories)

    def test_unused_or_unknown_nested_git_repository_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            native_build = Path(temporary)
            self.create_native_sources(native_build)
            repositories, glib_source = (
                discover_native_dependency_repositories(native_build)
            )
            glib_repositories = discover_glib_git_subprojects(glib_source)
            (
                native_build
                / "_deps/spirv_reflect-src/third_party/googletest/.git"
            ).mkdir(parents=True)
            with self.assertRaisesRegex(
                RuntimeError,
                "Android native Git dependency mismatch",
            ):
                validate_native_git_inventory(
                    native_build,
                    repositories,
                    glib_repositories,
                )

    def test_exact_revision_match_and_mismatch_for_all_categories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary) / "repository"
            repository.mkdir()
            subprocess.run(
                ["git", "-C", str(repository), "init", "--quiet"],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repository),
                    "-c",
                    "user.name=GDOX tests",
                    "-c",
                    "user.email=tests@example.com",
                    "commit",
                    "--allow-empty",
                    "--quiet",
                    "-m",
                    "source",
                ],
                check=True,
            )
            actual = subprocess.run(
                ["git", "-C", str(repository), "rev-parse", "HEAD"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            categories = (
                "xemu",
                "SDL2",
                "libusb",
                *ANDROID_NATIVE_GIT_REVISION_KEYS,
                *ANDROID_GLIB_GIT_SUBPROJECTS,
            )
            for category in categories:
                with self.subTest(category=category, case="match"):
                    validate_exact_revision(category, repository, actual)
                with self.subTest(
                    category=category, case="mismatch"
                ), self.assertRaisesRegex(
                    RuntimeError,
                    "expected pinned revision",
                ):
                    validate_exact_revision(category, repository, "0" * 40)

    def test_corrupt_glib_archive_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "glib.tar.xz"
            archive.write_bytes(b"corrupt")
            with self.assertRaisesRegex(RuntimeError, "has SHA-256"):
                validate_sha256(archive, "0" * 64)

    def test_pristine_tar_source_accepts_directories_and_regular_files(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source_file = root / "source.txt"
            source_file.write_bytes(b"source\n")
            source_archive = root / "glib.tar.xz"
            with tarfile.open(source_archive, "w:xz") as archive:
                archive.add(source_file, "glib-2.66.8/glib/source.txt")
            output = root / "output.tar"
            with tarfile.open(output, "w") as archive:
                add_pristine_tar_source(
                    archive,
                    source_archive,
                    "glib-2.66.8",
                    Path("release/source/glib"),
                )
            with tarfile.open(output, "r") as archive:
                member = archive.getmember(
                    "release/source/glib/glib/source.txt"
                )
                contents = archive.extractfile(member)
                self.assertIsNotNone(contents)
                assert contents is not None
                self.assertEqual(contents.read(), b"source\n")

    def test_canonical_tree_digest_binds_content_and_symlinks(self) -> None:
        content_digest = "1" * 64
        entries = [
            SourceTreeEntry(
                "bin/tool",
                "file",
                0o700,
                size=12,
                content_sha256=content_digest,
            ),
            SourceTreeEntry(
                "tool-link",
                "symlink",
                0o600,
                symlink_target="bin/tool",
            ),
        ]
        baseline = canonical_tree_digest(entries)
        normalized_modes = [
            SourceTreeEntry(
                "bin/tool",
                "file",
                0o755,
                size=12,
                content_sha256=content_digest,
            ),
            SourceTreeEntry(
                "tool-link",
                "symlink",
                0o777,
                symlink_target="bin/tool",
            ),
        ]
        self.assertEqual(baseline, canonical_tree_digest(normalized_modes))
        changed = list(entries)
        changed[0] = SourceTreeEntry(
            "bin/tool",
            "file",
            0o700,
            size=12,
            content_sha256="2" * 64,
        )
        self.assertNotEqual(baseline, canonical_tree_digest(changed))
        changed = list(entries)
        changed[1] = SourceTreeEntry(
            "tool-link",
            "symlink",
            0o600,
            symlink_target="bin/other",
        )
        self.assertNotEqual(baseline, canonical_tree_digest(changed))
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            canonical_tree_digest([entries[0], entries[0]])
        with self.assertRaisesRegex(RuntimeError, "unsupported"):
            canonical_tree_digest(
                [SourceTreeEntry("device", "fifo", 0o644)]
            )


if __name__ == "__main__":
    unittest.main()
