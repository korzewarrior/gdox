#!/usr/bin/env python3
"""Create and verify one deterministic patched Xenia Windows archive."""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from fetch_runtime import load_manifest
from xenia_distribution import (
    WINDOWS_TARGET,
    file_sha256,
    validate_archive,
    verify_storage_capability,
)

ARCHIVE_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def _member(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, ARCHIVE_TIMESTAMP)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def write_archive(executable: Path, license_file: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    with zipfile.ZipFile(output, "w", allowZip64=True) as archive:
        archive.writestr(_member("LICENSE"), license_file.read_bytes())
        archive.writestr(_member("xenia_canary.exe"), executable.read_bytes())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--license", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    manifest = load_manifest()
    try:
        definition = manifest["xenia"]["revisions"][args.revision]
        asset = definition["targets"][WINDOWS_TARGET]
    except KeyError:
        parser.error(f"unknown Windows Xenia revision: {args.revision}")
    if asset["origin"] != "gdox-patched":
        parser.error(f"Xenia {args.revision} is not a GDOX-patched runtime")
    if args.output.name != asset["archive_name"]:
        parser.error(f"output must be named {asset['archive_name']}")

    for path, size, digest, label in (
        (
            args.executable,
            asset["executable_size"],
            asset["executable_sha256"],
            "executable",
        ),
        (
            args.license,
            definition["license"]["size"],
            definition["license"]["sha256"],
            "license",
        ),
    ):
        if not path.is_file():
            parser.error(f"{label} does not exist: {path}")
        if path.stat().st_size != size or file_sha256(path) != digest:
            parser.error(f"{label} does not match the reviewed manifest")

    verify_storage_capability(
        args.executable,
        f"Xenia {args.revision} candidate",
    )

    write_archive(args.executable, args.license, args.output)

    validate_archive(
        args.output,
        asset,
        f"Xenia {args.revision} candidate archive",
    )
    metadata = {
        "archive_name": args.output.name,
        "executable_sha256": asset["executable_sha256"],
        "executable_size": asset["executable_size"],
        "sha256": file_sha256(args.output),
        "size": args.output.stat().st_size,
    }
    print(json.dumps(metadata, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
