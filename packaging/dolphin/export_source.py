#!/usr/bin/env python3
"""Export the pinned pristine Dolphin tree, including every pinned submodule."""
from __future__ import annotations

import argparse
import gzip
import subprocess
import tarfile
from pathlib import Path

COMMIT = "409881c34357d0f105fd473167d15ab0bd9c1628"
PREFIX = "dolphin-2609a-source"


def export(repository: Path, output: Path) -> None:
    if output.exists():
        raise ValueError("source archive already exists")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as raw, gzip.GzipFile(fileobj=raw, filename="", mode="wb", mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode="w|") as destination:
            def append(repo: Path, commit: str, relative: str) -> None:
                actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
                if actual != commit:
                    raise ValueError(f"source revision differs: {relative or PREFIX}")
                process = subprocess.Popen(["git", "archive", "--format=tar", commit], cwd=repo, stdout=subprocess.PIPE)
                with tarfile.open(fileobj=process.stdout, mode="r|") as source:
                    for member in source:
                        if member.isdir():
                            continue
                        if not member.isfile() and not member.issym():
                            raise ValueError(f"unexpected source member: {member.name}")
                        data = source.extractfile(member) if member.isfile() else None
                        member.name = f"{PREFIX}/{relative}{member.name}"
                        member.uid = member.gid = member.mtime = 0
                        member.uname = member.gname = ""
                        member.pax_headers = {}
                        destination.addfile(member, data)
                if process.wait() != 0:
                    raise ValueError("git source export failed")
                entries = subprocess.check_output(["git", "ls-tree", "-rz", commit], cwd=repo)
                for entry in entries.split(b"\0"):
                    if not entry:
                        continue
                    metadata, path = entry.split(b"\t", 1)
                    mode, kind, revision = metadata.split()
                    if mode == b"160000":
                        name = path.decode("utf-8")
                        append(repo / name, revision.decode("ascii"), f"{relative}{name}/")
            append(repository.resolve(), COMMIT, "")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    export(args.repository, args.output)
