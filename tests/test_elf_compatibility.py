"""Regression tests for the Linux release ELF compatibility gate."""

from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
from elf_compatibility import (
    EM_X86_64,
    LINUX_RELEASE_POLICY,
    LinuxCompatibilityError,
    validate_release_artifact,
)

LINUX_TARGET = "x86_64-unknown-linux-gnu"
DECK_TARGET = "x86_64-steamdeck-linux-gnu"
BASE_ADDRESS = 0x400000
DYNAMIC_OFFSET = 0x100
STRING_OFFSET = 0x180
REQUIREMENT_OFFSET = 0x1C0


def make_elf(
    symbols: tuple[str, ...],
    *,
    machine: int = EM_X86_64,
) -> bytes:
    strings = bytearray(b"\0libc.so.6\0")
    names: list[int] = []
    for symbol in symbols:
        names.append(len(strings))
        strings.extend(symbol.encode("ascii") + b"\0")

    version_data = bytearray(
        struct.pack("<HHIII", 1, len(symbols), 1, 16, 0)
    )
    for index, name in enumerate(names):
        version_data.extend(
            struct.pack(
                "<IHHII",
                0,
                0,
                index + 2,
                name,
                16 if index + 1 < len(names) else 0,
            )
        )

    dynamic = b"".join(
        (
            struct.pack("<qQ", 5, BASE_ADDRESS + STRING_OFFSET),
            struct.pack("<qQ", 10, len(strings)),
            struct.pack("<qQ", 0x6FFFFFFE, BASE_ADDRESS + REQUIREMENT_OFFSET),
            struct.pack("<qQ", 0x6FFFFFFF, 1),
            struct.pack("<qQ", 0, 0),
        )
    )
    file_bytes = REQUIREMENT_OFFSET + len(version_data)
    image = bytearray(file_bytes)
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x01\x01" + bytes(9),
        3,
        machine,
        1,
        0,
        64,
        0,
        0,
        64,
        56,
        2,
        0,
        0,
        0,
    )
    load_segment = struct.pack(
        "<IIQQQQQQ",
        1,
        5,
        0,
        BASE_ADDRESS,
        BASE_ADDRESS,
        file_bytes,
        file_bytes,
        0x1000,
    )
    dynamic_segment = struct.pack(
        "<IIQQQQQQ",
        2,
        4,
        DYNAMIC_OFFSET,
        BASE_ADDRESS + DYNAMIC_OFFSET,
        BASE_ADDRESS + DYNAMIC_OFFSET,
        len(dynamic),
        len(dynamic),
        8,
    )
    image[:64] = header
    image[64:120] = load_segment
    image[120:176] = dynamic_segment
    image[DYNAMIC_OFFSET : DYNAMIC_OFFSET + len(dynamic)] = dynamic
    image[STRING_OFFSET : STRING_OFFSET + len(strings)] = strings
    image[
        REQUIREMENT_OFFSET : REQUIREMENT_OFFSET + len(version_data)
    ] = version_data
    return bytes(image)


class ElfCompatibilityTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="gdox-elf-compatibility-"
        )
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def artifact(self, data: bytes, name: str = "gdox") -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_policy_is_shared_by_linux_and_steamdeck(self) -> None:
        self.assertEqual(LINUX_RELEASE_POLICY.maximum_glibc, (2, 38, 0))
        self.assertEqual(
            LINUX_RELEASE_POLICY.targets,
            frozenset({LINUX_TARGET, DECK_TARGET}),
        )
        artifact = self.artifact(
            make_elf(("GLIBC_2.17", "GLIBC_2.38"))
        )
        self.assertEqual(
            validate_release_artifact(LINUX_TARGET, artifact),
            (2, 38, 0),
        )
        self.assertEqual(
            validate_release_artifact(DECK_TARGET, artifact),
            (2, 38, 0),
        )

    def test_newer_glibc_requirement_is_rejected(self) -> None:
        for version in ("2.39", "2.43"):
            with self.subTest(version=version):
                artifact = self.artifact(
                    make_elf(("GLIBC_2.17", f"GLIBC_{version}")),
                    f"gdox-{version}",
                )
                with self.assertRaisesRegex(
                    LinuxCompatibilityError,
                    rf"requires GLIBC_{version}.*ceiling is GLIBC_2\.38",
                ):
                    validate_release_artifact(DECK_TARGET, artifact)

    def test_invalid_linux_artifacts_fail_closed(self) -> None:
        malformed = self.artifact(b"not an ELF", "malformed")
        with self.assertRaisesRegex(
            LinuxCompatibilityError,
            "not an ELF executable",
        ):
            validate_release_artifact(LINUX_TARGET, malformed)

        truncated = self.artifact(
            make_elf(("GLIBC_2.38",))[:-8],
            "truncated",
        )
        with self.assertRaisesRegex(
            LinuxCompatibilityError,
            "truncated",
        ):
            validate_release_artifact(LINUX_TARGET, truncated)

        wrong_machine = self.artifact(
            make_elf(("GLIBC_2.38",), machine=3),
            "wrong-machine",
        )
        with self.assertRaisesRegex(
            LinuxCompatibilityError,
            "not x86-64",
        ):
            validate_release_artifact(LINUX_TARGET, wrong_machine)

        no_glibc = self.artifact(
            make_elf(("GLIBCXX_3.4",)),
            "no-glibc",
        )
        with self.assertRaisesRegex(
            LinuxCompatibilityError,
            "declares no GLIBC",
        ):
            validate_release_artifact(LINUX_TARGET, no_glibc)

    def test_non_linux_target_is_not_subject_to_elf_policy(self) -> None:
        missing = self.root / "gdox.exe"
        self.assertIsNone(
            validate_release_artifact("x86_64-pc-windows-msvc", missing)
        )

    def test_package_and_audit_entry_points_reject_host_floor(self) -> None:
        artifact = self.artifact(make_elf(("GLIBC_2.43",)))
        output = self.root / "release"
        packaged = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "package_release.py"),
                "--target",
                DECK_TARGET,
                "--artifact",
                str(artifact),
                "--output",
                str(output),
                "--without-runtime",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(packaged.returncode, 0)
        self.assertIn("requires GLIBC_2.43", packaged.stderr)
        self.assertFalse(output.exists())

        audit_root = self.root / "audit"
        audit_root.mkdir()
        audited = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--path",
                str(audit_root),
                "--artifact",
                str(artifact),
                "--target",
                LINUX_TARGET,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(audited.returncode, 0)
        self.assertIn("requires GLIBC_2.43", audited.stderr)


if __name__ == "__main__":
    unittest.main()
