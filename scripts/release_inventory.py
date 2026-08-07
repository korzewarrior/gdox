#!/usr/bin/env python3
"""Define and verify the exact public desktop release inventory."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from package_release import PLATFORMS, release_package_name
from package_corresponding_source import audit_corresponding_source_archive
from project_version import validated_project_version

PUBLIC_TARGETS = (
    "aarch64-apple-darwin",
    "x86_64-apple-darwin",
    "x86_64-pc-windows-msvc",
    "x86_64-steamdeck-linux-gnu",
    "x86_64-unknown-linux-gnu",
)


def archive_suffix(target: str) -> str:
    return ".zip" if PLATFORMS[target] == "windows" else ".tar.gz"


def payload_names(version: str) -> tuple[str, ...]:
    binaries = tuple(
        release_package_name(version, target, without_runtime=False)
        + archive_suffix(target)
        for target in PUBLIC_TARGETS
    )
    return tuple(
        sorted((*binaries, f"gdox-{version}-corresponding-source.tar.gz"))
    )


def release_names(version: str, *, signed: bool) -> tuple[str, ...]:
    names = payload_names(version)
    if not signed:
        return names
    return tuple(sorted((*names, "SHA256SUMS", "SHA256SUMS.minisig")))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checksum_text(directory: Path, version: str) -> str:
    return "".join(
        f"{file_sha256(directory / name)}  {name}\n"
        for name in payload_names(version)
    )


def audit(directory: Path, version: str, *, signed: bool) -> None:
    expected = set(release_names(version, signed=signed))
    try:
        entries = tuple(directory.iterdir())
    except OSError as error:
        raise ValueError(f"could not read release directory: {error}") from None
    actual = {entry.name for entry in entries}
    unsafe = {
        entry.name for entry in entries if entry.is_symlink() or not entry.is_file()
    }
    if actual != expected or unsafe:
        differences = sorted(actual.symmetric_difference(expected) | unsafe)
        raise ValueError(
            "public release inventory has missing, extra, or unsafe files: "
            + ", ".join(differences)
        )
    audit_corresponding_source_archive(
        Path(__file__).resolve().parent.parent,
        directory / f"gdox-{version}-corresponding-source.tar.gz",
        version,
    )
    if signed:
        observed = (directory / "SHA256SUMS").read_text(encoding="utf-8")
        if observed != checksum_text(directory, version):
            raise ValueError("SHA256SUMS does not match the public release payload")


def write_checksums(directory: Path, version: str) -> None:
    audit(directory, version, signed=False)
    (directory / "SHA256SUMS").write_text(
        checksum_text(directory, version),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("audit", "checksums", "list"))
    parser.add_argument("--path", required=True, type=Path)
    parser.add_argument("--version")
    parser.add_argument("--signed", action="store_true")
    arguments = parser.parse_args()
    try:
        version = validated_project_version(arguments.version)
        directory = arguments.path.resolve()
        if arguments.command == "checksums":
            if arguments.signed:
                raise ValueError("checksums cannot require an existing signature")
            write_checksums(directory, version)
        else:
            audit(directory, version, signed=arguments.signed)
            if arguments.command == "list":
                for name in release_names(version, signed=arguments.signed):
                    print(directory / name)
    except (OSError, UnicodeError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
