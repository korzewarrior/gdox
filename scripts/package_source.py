#!/usr/bin/env python3
"""Create a deterministic archive of the tracked GDOX source tree."""

from __future__ import annotations

import argparse
import gzip
import hashlib
from pathlib import Path
import subprocess
import sys
import tarfile

sys.dont_write_bytecode = True
from release_paths import output_root


ROOT = Path(__file__).resolve().parent.parent


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [
        Path(name.decode())
        for name in result.stdout.split(b"\0")
        if name
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument(
        "--output",
        default=output_root() / "release",
        type=Path,
    )
    args = parser.parse_args()
    version = args.version.removeprefix("v")
    name = f"gdox-{version}-source"
    output = args.output / f"{name}.tar.gz"
    args.output.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)

    with output.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for relative in tracked_files():
                    path = ROOT / relative
                    if not path.is_file():
                        continue
                    info = archive.gettarinfo(path, arcname=str(Path(name) / relative))
                    info.mtime = 0
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    with path.open("rb") as source:
                        archive.addfile(info, source)

    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--artifact",
            str(output),
        ],
        check=True,
    )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    checksum = output.with_name(output.name + ".sha256")
    checksum.write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    print(output)
    print(checksum)


if __name__ == "__main__":
    main()
