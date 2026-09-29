"""Regression tests for release-build tree reuse."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))

import build_release
from build_release import cmake_tree_reusable, configure_arguments, test_command


class BuildReleaseTests(unittest.TestCase):
    def test_build_accepts_the_ci_output_directory(self) -> None:
        with (
            mock.patch.object(sys, "argv", ["build_release.py", "--target",
                "x86_64-unknown-linux-gnu", "--build-dir", str(ROOT / "dist/.build/ci"), "--clean"]),
            mock.patch.object(build_release, "require_tool"),
            mock.patch.object(build_release, "configure_arguments", side_effect=RuntimeError("configuration reached")),
        ):
            with self.assertRaisesRegex(RuntimeError, "configuration reached"):
                build_release.main()

    def test_build_refuses_to_delete_source_or_unrecognized_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source = root / "source"
            source.mkdir()
            unrelated = root / "unrelated"
            unrelated.mkdir()
            sentinel = unrelated / "keep"
            sentinel.write_text("existing work", encoding="utf-8")
            targets = [
                (source, []), (source, ["--clean"]), (root, ["--clean"]),
                (source / "src", []), (unrelated, []),
            ]
            if sys.platform != "win32":
                alias = root / "alias"
                alias.symlink_to(unrelated, target_is_directory=True)
                targets.append((alias, ["--clean"]))
            for target, flags in targets:
                with (
                    self.subTest(target=target, flags=flags),
                    mock.patch.object(build_release, "SOURCE", source),
                    mock.patch.object(sys, "argv", ["build_release.py", "--target",
                        "x86_64-unknown-linux-gnu", "--build-dir", str(target), *flags]),
                    mock.patch.object(build_release, "require_tool"),
                    mock.patch.object(build_release, "configure_arguments", return_value=([], target / "gdox")),
                    mock.patch.object(build_release.shutil, "rmtree") as remove,
                ):
                    with self.assertRaisesRegex(SystemExit, "refusing|not an ordinary"):
                        build_release.main()
                    remove.assert_not_called()
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "existing work")

    def write_cache(
        self,
        build: Path,
        source: Path,
        *,
        generator: str = "Ninja",
    ) -> None:
        build.mkdir(parents=True, exist_ok=True)
        (build / "CMakeCache.txt").write_text(
            "\n".join(
                (
                    f"CMAKE_CACHEFILE_DIR:INTERNAL={build}",
                    f"CMAKE_GENERATOR:INTERNAL={generator}",
                    f"CMAKE_HOME_DIRECTORY:INTERNAL={source}",
                    "",
                )
            ),
            encoding="utf-8",
        )

    def test_accepts_empty_or_matching_build_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            build = root / "build"
            source.mkdir()

            self.assertTrue(cmake_tree_reusable(build, source))
            build.mkdir()
            self.assertTrue(cmake_tree_reusable(build, source))
            build.rmdir()
            self.write_cache(build, source)
            self.assertTrue(cmake_tree_reusable(build, source))

    def test_rejects_foreign_or_incomplete_build_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            build.mkdir()
            (build / "partial").write_text("incomplete", encoding="utf-8")
            self.assertFalse(cmake_tree_reusable(build, source))

            (build / "partial").unlink()
            self.write_cache(build, root / "container-source")
            self.assertFalse(cmake_tree_reusable(build, source))

            self.write_cache(build, source, generator="Unix Makefiles")
            self.assertFalse(cmake_tree_reusable(build, source))

    def test_rejects_non_directory_build_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            build.write_text("not a build directory", encoding="utf-8")

            self.assertFalse(cmake_tree_reusable(build, source))

    def test_serializes_wine_without_slowing_native_hosts(self) -> None:
        build = Path("build")
        mingw = test_command(
            build,
            "x86_64-pc-windows-gnu",
            skip_host_neutral=True,
        )
        linux = test_command(
            build,
            "x86_64-unknown-linux-gnu",
            skip_host_neutral=False,
        )

        self.assertEqual(mingw[mingw.index("--parallel") + 1], "1")
        self.assertEqual(linux[linux.index("--parallel") + 1], "4")
        self.assertEqual(mingw[-2:], ["--label-exclude", "host-neutral"])
        self.assertNotIn("--label-exclude", linux)

    def test_native_mingw_does_not_use_cross_emulation(self) -> None:
        build = Path("C:/build")
        with (
            mock.patch.object(build_release.sys, "platform", "win32"),
            mock.patch.object(build_release.os, "name", "nt"),
            mock.patch.object(
                build_release,
                "require_tool",
                side_effect=lambda name: f"C:\\tools\\{name}.exe",
            ),
        ):
            arguments, artifact = configure_arguments("x86_64-pc-windows-gnu", build)

        self.assertEqual(artifact, build / "gdox.exe")
        self.assertIn("-DCMAKE_C_COMPILER=C:/tools/gcc.exe", arguments)
        self.assertFalse(
            any("CMAKE_CROSSCOMPILING_EMULATOR" in value for value in arguments)
        )
        self.assertNotIn("-DCMAKE_SYSTEM_NAME=Windows", arguments)

    def test_ci_covers_the_supported_mingw_target(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        mingw_job = workflow.split("  windows-gnu:\n", 1)[1]

        self.assertIn("  windows-gnu:\n", workflow)
        self.assertIn(
            "runs-on: windows-2025",
            mingw_job,
        )
        self.assertIn("C:\\mingw64\\bin", mingw_job)
        self.assertIn("--target x86_64-pc-windows-gnu", mingw_job)
        self.assertIn("--skip-host-neutral-tests", mingw_job)


if __name__ == "__main__":
    unittest.main()
