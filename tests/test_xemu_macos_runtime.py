"""Regression tests for the maintained universal macOS xemu packager."""

from __future__ import annotations

import json
import stat
import struct
import sys
import tempfile
import unittest
import uuid
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "packaging/xemu"))

from build_macos import validate_build_arguments
from package_xemu_macos_runtime import (
    ARCHIVE_TIMESTAMP,
    expected_capability_bytes,
    macho_uuids,
    normalize_macho_uuid,
    parse_load_commands,
    parse_rpaths,
    remove_macho_signature,
    validate_archive,
)


def archive_member(name: str, *, mode: int) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, ARCHIVE_TIMESTAMP)
    info.create_system = 3
    info.external_attr = mode << 16
    return info


def write_fixture_archive(
    path: Path,
    *,
    executable_name: str = "xemu.app/Contents/MacOS/xemu",
    executable_mode: int = stat.S_IFREG | 0o755,
) -> None:
    members = (
        ("LICENSE.txt", stat.S_IFREG | 0o644, b"license\n"),
        ("xemu.app/", stat.S_IFDIR | 0o755, b""),
        ("xemu.app/Contents/", stat.S_IFDIR | 0o755, b""),
        ("xemu.app/Contents/MacOS/", stat.S_IFDIR | 0o755, b""),
        (executable_name, executable_mode, b"runtime"),
    )
    with zipfile.ZipFile(path, "w") as archive:
        for name, mode, content in members:
            archive.writestr(archive_member(name, mode=mode), content)


class XemuMacosRuntimeTest(unittest.TestCase):
    def test_build_arguments_pin_compile_and_link_deployment_targets(self) -> None:
        definition = {
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
        }
        compile_arguments = [
            "-arch",
            "arm64",
            "-target",
            "arm64-apple-macos14.0",
            "-isysroot",
            "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk",
            "-mmacosx-version-min=14.0",
        ]
        options = [
            {"name": "c_args", "value": compile_arguments},
            {"name": "cpp_args", "value": compile_arguments},
            {"name": "c_link_args", "value": definition["link_arguments"]},
            {"name": "cpp_link_args", "value": definition["link_arguments"]},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            metadata = source / "build/meson-info"
            metadata.mkdir(parents=True)
            path = metadata / "intro-buildoptions.json"
            path.write_text(json.dumps(options), encoding="utf-8")
            validate_build_arguments(source, "arm64", definition)

            options[-1]["value"] = ["-arch", "arm64"]
            path.write_text(json.dumps(options), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "linker arguments differ"):
                validate_build_arguments(source, "arm64", definition)

    def test_capability_bytes_are_exact(self) -> None:
        integration = json.loads(
            (ROOT / "packaging/xemu/integration.json").read_text(encoding="utf-8")
        )
        response = expected_capability_bytes(integration)
        self.assertEqual(len(response), 680)
        self.assertTrue(response.endswith(b"}\n"))
        self.assertEqual(
            json.loads(response), integration["capability_query"]["response"]
        )

    def test_load_command_parser_exposes_uuid(self) -> None:
        output = """
Load command 1
      cmd LC_BUILD_VERSION
  cmdsize 32
Load command 2
      cmd LC_UUID
  cmdsize 24
"""
        self.assertEqual(
            parse_load_commands(output),
            ("LC_BUILD_VERSION", "LC_UUID"),
        )

    @patch(
        "package_xemu_macos_runtime.executable_load_commands",
        return_value=("LC_BUILD_VERSION", "LC_UUID"),
    )
    @patch("package_xemu_macos_runtime.run")
    def test_signature_removal_is_explicit_and_verified(
        self,
        run_command,
        load_commands,
    ) -> None:
        executable = Path("/tmp/xemu.app/Contents/MacOS/xemu")
        remove_macho_signature(executable, ("arm64", "x86_64"))
        run_command.assert_called_once_with(
            ["codesign", "--remove-signature", str(executable)]
        )
        self.assertEqual(
            load_commands.call_args_list[0].args,
            (executable, "arm64"),
        )
        self.assertEqual(
            load_commands.call_args_list[1].args,
            (executable, "x86_64"),
        )

    @patch(
        "package_xemu_macos_runtime.executable_load_commands",
        return_value=("LC_UUID", "LC_CODE_SIGNATURE"),
    )
    @patch("package_xemu_macos_runtime.run")
    def test_signature_removal_fails_closed(
        self,
        _run_command,
        _load_commands,
    ) -> None:
        with self.assertRaisesRegex(RuntimeError, "retained an inherited"):
            remove_macho_signature(Path("xemu"), ("arm64",))

    def test_macho_uuid_normalization_is_exact(self) -> None:
        original = "00000000-0000-0000-0000-000000000001"
        expected = "4a183fcc-19de-54e5-a2db-8642ff10d4c0"
        header = struct.pack(
            "<IIIIIIII",
            0xFEEDFACF,
            0x0100000C,
            0,
            2,
            1,
            24,
            0,
            0,
        )
        command = struct.pack("<II16s", 0x1B, 24, uuid.UUID(original).bytes)
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "xemu"
            executable.write_bytes(header + command)
            self.assertEqual(macho_uuids(executable), {"arm64": original})
            normalize_macho_uuid(executable, "arm64", expected)
            self.assertEqual(macho_uuids(executable), {"arm64": expected})

    def test_rpath_parser_preserves_duplicate_entries(self) -> None:
        output = """
Load command 1
          cmd LC_RPATH
      cmdsize 56
         path @executable_path/../Libraries/arm64/ (offset 12)
Load command 2
          cmd LC_RPATH
      cmdsize 56
         path @executable_path/../Libraries/arm64/ (offset 12)
"""
        self.assertEqual(
            parse_rpaths(output),
            (
                "@executable_path/../Libraries/arm64/",
                "@executable_path/../Libraries/arm64/",
            ),
        )

    def test_rpath_parser_rejects_incomplete_entry(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "omitted"):
            parse_rpaths("cmd LC_RPATH\ncmdsize 48\n")

    def test_archive_inventory_is_exact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "runtime.zip"
            write_fixture_archive(archive)
            validate_archive(archive, {"Contents/MacOS/xemu"})

    def test_archive_rejects_case_collision(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "runtime.zip"
            write_fixture_archive(
                archive,
                executable_name="xemu.app/Contents/MacOS/XEMU",
            )
            with zipfile.ZipFile(archive, "a") as output:
                output.writestr(
                    archive_member(
                        "xemu.app/Contents/MacOS/xemu",
                        mode=stat.S_IFREG | 0o755,
                    ),
                    b"runtime",
                )
            with self.assertRaisesRegex(RuntimeError, "case-colliding"):
                validate_archive(archive, {"Contents/MacOS/xemu"})

    def test_archive_rejects_unsorted_members(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.zip"
            archive = root / "runtime.zip"
            write_fixture_archive(source)
            with zipfile.ZipFile(source) as input_archive:
                members = [
                    (info, input_archive.read(info.filename))
                    for info in input_archive.infolist()
                ]
            with zipfile.ZipFile(archive, "w") as output:
                for info, content in reversed(members):
                    output.writestr(info, content)
            with self.assertRaisesRegex(RuntimeError, "not sorted"):
                validate_archive(archive, {"Contents/MacOS/xemu"})

    def test_archive_rejects_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "runtime.zip"
            write_fixture_archive(
                archive,
                executable_mode=stat.S_IFLNK | 0o777,
            )
            with self.assertRaisesRegex(RuntimeError, "symlink"):
                validate_archive(archive, {"Contents/MacOS/xemu"})


if __name__ == "__main__":
    unittest.main()
