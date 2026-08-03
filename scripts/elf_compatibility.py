#!/usr/bin/env python3
"""Enforce the Linux release ELF and glibc compatibility contract."""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

sys.dont_write_bytecode = True


ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
EM_X86_64 = 62
PT_LOAD = 1
PT_DYNAMIC = 2
DT_NULL = 0
DT_STRTAB = 5
DT_STRSZ = 10
DT_VERNEED = 0x6FFFFFFE
DT_VERNEEDNUM = 0x6FFFFFFF
GLIBC_VERSION = re.compile(rb"GLIBC_(\d+)\.(\d+)(?:\.(\d+))?")


@dataclass(frozen=True)
class LinuxReleasePolicy:
    targets: frozenset[str]
    maximum_glibc: tuple[int, int, int]
    elf_class: int
    data_encoding: int
    machine: int


# This is the reviewed compatibility floor for both public Linux packages.
# Raising it requires an explicit platform-policy change and target testing.
LINUX_RELEASE_POLICY = LinuxReleasePolicy(
    targets=frozenset(
        {
            "x86_64-unknown-linux-gnu",
            "x86_64-steamdeck-linux-gnu",
        }
    ),
    maximum_glibc=(2, 38, 0),
    elf_class=ELFCLASS64,
    data_encoding=ELFDATA2LSB,
    machine=EM_X86_64,
)


class ElfFormatError(ValueError):
    """The artifact is not a structurally valid dynamic ELF image."""


class LinuxCompatibilityError(ValueError):
    """The artifact violates the Linux release compatibility contract."""


@dataclass(frozen=True)
class _Segment:
    kind: int
    offset: int
    virtual_address: int
    file_bytes: int


class _ElfImage:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if len(data) < 16 or data[:4] != ELF_MAGIC:
            raise ElfFormatError("artifact is not an ELF executable")
        self.elf_class = data[4]
        self.data_encoding = data[5]
        if self.elf_class not in {1, 2}:
            raise ElfFormatError("ELF class is unsupported")
        if self.data_encoding not in {1, 2}:
            raise ElfFormatError("ELF byte order is unsupported")
        if data[6] != 1:
            raise ElfFormatError("ELF identification version is invalid")
        self.byte_order = "<" if self.data_encoding == 1 else ">"
        self.machine, program_offset, entry_bytes, entry_count = (
            self._read_header()
        )
        self.segments = self._read_segments(
            program_offset,
            entry_bytes,
            entry_count,
        )

    def _unpack(self, format_body: str, offset: int, label: str):
        layout = self.byte_order + format_body
        size = struct.calcsize(layout)
        if offset < 0 or offset + size > len(self.data):
            raise ElfFormatError(f"{label} is outside the ELF file")
        return struct.unpack_from(layout, self.data, offset)

    def _read_header(self) -> tuple[int, int, int, int]:
        if self.elf_class == 2:
            fields = self._unpack(
                "16sHHIQQQIHHHHHH",
                0,
                "ELF header",
            )
            machine = fields[2]
            program_offset = fields[5]
            header_bytes = fields[8]
            entry_bytes = fields[9]
            entry_count = fields[10]
            expected_header = 64
            expected_entry = 56
        else:
            fields = self._unpack(
                "16sHHIIIIIHHHHHH",
                0,
                "ELF header",
            )
            machine = fields[2]
            program_offset = fields[5]
            header_bytes = fields[8]
            entry_bytes = fields[9]
            entry_count = fields[10]
            expected_header = 52
            expected_entry = 32
        if fields[1] not in {2, 3} or fields[3] != 1:
            raise ElfFormatError("ELF executable type or version is invalid")
        if header_bytes < expected_header:
            raise ElfFormatError("ELF header size is invalid")
        if entry_count == 0 or entry_count == 0xFFFF:
            raise ElfFormatError("ELF program-header count is unsupported")
        if entry_bytes < expected_entry:
            raise ElfFormatError("ELF program-header size is invalid")
        if program_offset + entry_bytes * entry_count > len(self.data):
            raise ElfFormatError("ELF program-header table is truncated")
        return machine, program_offset, entry_bytes, entry_count

    def _read_segments(
        self,
        program_offset: int,
        entry_bytes: int,
        entry_count: int,
    ) -> tuple[_Segment, ...]:
        segments: list[_Segment] = []
        for index in range(entry_count):
            offset = program_offset + index * entry_bytes
            if self.elf_class == 2:
                fields = self._unpack(
                    "IIQQQQQQ",
                    offset,
                    f"ELF program header {index}",
                )
                kind = fields[0]
                file_offset = fields[2]
                virtual_address = fields[3]
                file_bytes = fields[5]
            else:
                fields = self._unpack(
                    "IIIIIIII",
                    offset,
                    f"ELF program header {index}",
                )
                kind = fields[0]
                file_offset = fields[1]
                virtual_address = fields[2]
                file_bytes = fields[4]
            if file_offset + file_bytes > len(self.data):
                raise ElfFormatError(
                    f"ELF program segment {index} is truncated"
                )
            segments.append(
                _Segment(kind, file_offset, virtual_address, file_bytes)
            )
        return tuple(segments)

    def _virtual_offset(self, address: int, size: int, label: str) -> int:
        for segment in self.segments:
            if segment.kind != PT_LOAD or address < segment.virtual_address:
                continue
            within = address - segment.virtual_address
            if within <= segment.file_bytes and size <= segment.file_bytes - within:
                return segment.offset + within
        raise ElfFormatError(f"{label} is not backed by an ELF load segment")

    def _require_loaded_file_range(
        self,
        offset: int,
        size: int,
        label: str,
    ) -> None:
        for segment in self.segments:
            if segment.kind != PT_LOAD or offset < segment.offset:
                continue
            within = offset - segment.offset
            if within <= segment.file_bytes and size <= segment.file_bytes - within:
                return
        raise ElfFormatError(f"{label} is not contained in an ELF load segment")

    def _dynamic_values(self) -> dict[int, int]:
        dynamic_segments = [
            segment for segment in self.segments if segment.kind == PT_DYNAMIC
        ]
        if len(dynamic_segments) != 1:
            raise ElfFormatError("ELF must contain one dynamic segment")
        segment = dynamic_segments[0]
        if self._virtual_offset(
            segment.virtual_address,
            segment.file_bytes,
            "ELF dynamic segment",
        ) != segment.offset:
            raise ElfFormatError("ELF dynamic segment mapping is inconsistent")
        entry_format = "qQ" if self.elf_class == 2 else "iI"
        entry_bytes = struct.calcsize(self.byte_order + entry_format)
        values: dict[int, int] = {}
        terminated = False
        for relative in range(0, segment.file_bytes, entry_bytes):
            if relative + entry_bytes > segment.file_bytes:
                break
            tag, value = self._unpack(
                entry_format,
                segment.offset + relative,
                "ELF dynamic entry",
            )
            if tag == DT_NULL:
                terminated = True
                break
            if tag in {DT_STRTAB, DT_STRSZ, DT_VERNEED, DT_VERNEEDNUM}:
                if tag in values and values[tag] != value:
                    raise ElfFormatError("ELF dynamic metadata conflicts")
                values[tag] = value
        if not terminated:
            raise ElfFormatError("ELF dynamic segment is not terminated")
        return values

    def _string(
        self,
        table_offset: int,
        table_bytes: int,
        string_offset: int,
    ) -> bytes:
        if string_offset >= table_bytes:
            raise ElfFormatError("ELF version string offset is invalid")
        begin = table_offset + string_offset
        end = self.data.find(b"\0", begin, table_offset + table_bytes)
        if end < 0:
            raise ElfFormatError("ELF version string is not terminated")
        return self.data[begin:end]

    def required_glibc_versions(self) -> frozenset[tuple[int, int, int]]:
        dynamic = self._dynamic_values()
        version_keys = {DT_VERNEED, DT_VERNEEDNUM}
        if not version_keys.intersection(dynamic):
            return frozenset()
        required = {DT_STRTAB, DT_STRSZ, *version_keys}
        if not required.issubset(dynamic):
            raise ElfFormatError("ELF version requirements are incomplete")
        requirement_count = dynamic[DT_VERNEEDNUM]
        if requirement_count == 0 or requirement_count > 65535:
            raise ElfFormatError("ELF version-requirement count is invalid")
        string_bytes = dynamic[DT_STRSZ]
        if string_bytes == 0:
            raise ElfFormatError("ELF dynamic string table is empty")
        string_offset = self._virtual_offset(
            dynamic[DT_STRTAB],
            string_bytes,
            "ELF dynamic string table",
        )
        requirement_offset = self._virtual_offset(
            dynamic[DT_VERNEED],
            16,
            "ELF version requirements",
        )
        versions: set[tuple[int, int, int]] = set()
        visited_requirements: set[int] = set()
        for requirement_index in range(requirement_count):
            if requirement_offset in visited_requirements:
                raise ElfFormatError("ELF version requirements contain a loop")
            visited_requirements.add(requirement_offset)
            self._require_loaded_file_range(
                requirement_offset,
                16,
                "ELF version requirement",
            )
            version, auxiliary_count, file_name, auxiliary, next_entry = (
                self._unpack(
                    "HHIII",
                    requirement_offset,
                    "ELF version requirement",
                )
            )
            if version != 1 or auxiliary_count == 0 or auxiliary < 16:
                raise ElfFormatError("ELF version requirement is invalid")
            self._string(string_offset, string_bytes, file_name)
            auxiliary_offset = requirement_offset + auxiliary
            visited_auxiliary: set[int] = set()
            for auxiliary_index in range(auxiliary_count):
                if auxiliary_offset in visited_auxiliary:
                    raise ElfFormatError("ELF version auxiliaries contain a loop")
                visited_auxiliary.add(auxiliary_offset)
                self._require_loaded_file_range(
                    auxiliary_offset,
                    16,
                    "ELF version auxiliary",
                )
                _, _, _, name, next_auxiliary = self._unpack(
                    "IHHII",
                    auxiliary_offset,
                    "ELF version auxiliary",
                )
                symbol = self._string(string_offset, string_bytes, name)
                match = GLIBC_VERSION.fullmatch(symbol)
                if match is not None:
                    versions.add(
                        (
                            int(match.group(1)),
                            int(match.group(2)),
                            int(match.group(3) or b"0"),
                        )
                    )
                if auxiliary_index + 1 < auxiliary_count:
                    if next_auxiliary < 16:
                        raise ElfFormatError(
                            "ELF version auxiliary chain is truncated"
                        )
                    auxiliary_offset += next_auxiliary
            if requirement_index + 1 < requirement_count:
                if next_entry < 16:
                    raise ElfFormatError(
                        "ELF version-requirement chain is truncated"
                    )
                requirement_offset += next_entry
        return frozenset(versions)


def _version_text(version: tuple[int, int, int]) -> str:
    if version[2] == 0:
        return f"{version[0]}.{version[1]}"
    return ".".join(str(component) for component in version)


def validate_release_artifact(
    target: str,
    artifact: Path,
) -> tuple[int, int, int] | None:
    """Validate a target artifact and return its highest required glibc."""

    policy = LINUX_RELEASE_POLICY
    if target not in policy.targets:
        return None
    try:
        image = _ElfImage(artifact.read_bytes())
        if image.elf_class != policy.elf_class:
            raise ElfFormatError("Linux release artifact is not ELF64")
        if image.data_encoding != policy.data_encoding:
            raise ElfFormatError("Linux release artifact is not little-endian")
        if image.machine != policy.machine:
            raise ElfFormatError("Linux release artifact is not x86-64")
        versions = image.required_glibc_versions()
    except (OSError, ElfFormatError) as error:
        raise LinuxCompatibilityError(
            f"{target} artifact is incompatible: {error}"
        ) from error
    if not versions:
        raise LinuxCompatibilityError(
            f"{target} artifact declares no GLIBC symbol-version requirements"
        )
    maximum = max(versions)
    if maximum > policy.maximum_glibc:
        raise LinuxCompatibilityError(
            f"{target} artifact requires GLIBC_{_version_text(maximum)}; "
            f"the release ceiling is GLIBC_{_version_text(policy.maximum_glibc)}"
        )
    return maximum


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True)
    parser.add_argument("--artifact", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        version = validate_release_artifact(
            arguments.target,
            arguments.artifact,
        )
    except LinuxCompatibilityError as error:
        parser.error(str(error))
    if version is not None:
        print(f"maximum required glibc: {_version_text(version)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
