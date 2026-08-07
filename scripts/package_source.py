"""Read and archive the exact committed GDOX source tree."""

from __future__ import annotations

import gzip
import subprocess
import sys
import tarfile
from io import BytesIO
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent


def git_output(repository: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", *arguments],
        cwd=repository,
        check=True,
        capture_output=True,
    )
    return result.stdout


def validate_clean_repository(repository: Path = ROOT) -> None:
    status = git_output(
        repository,
        "status",
        "--porcelain=v1",
        "--untracked-files=all",
        "--ignore-submodules=none",
        "-z",
    )
    if status:
        raise RuntimeError(
            "GDOX contains uncommitted source changes; public source "
            "archives must be created from a clean commit"
        )


def commit_entries(
    repository: Path = ROOT,
    revision: str = "HEAD",
) -> list[tuple[str, int, bytes]]:
    entries: list[tuple[str, int, bytes]] = []
    tree = git_output(
        repository,
        "ls-tree",
        "-rz",
        "--full-tree",
        revision,
    )
    for record in tree.split(b"\0"):
        if not record:
            continue
        try:
            metadata, encoded_path = record.split(b"\t", 1)
            encoded_mode, object_type, object_id = metadata.split(b" ", 2)
            relative = encoded_path.decode("utf-8")
            mode = int(encoded_mode, 8)
        except (UnicodeDecodeError, ValueError) as error:
            raise RuntimeError(
                "the commit tree contains an invalid entry"
            ) from error
        if object_type != b"blob" or mode not in {0o100644, 0o100755, 0o120000}:
            raise RuntimeError(
                f"unsupported source-tree entry in {revision}: {relative}"
            )
        data = git_output(
            repository,
            "cat-file",
            "blob",
            object_id.decode("ascii"),
        )
        entries.append((relative, mode, data))
    return entries


def create_source_archive(
    repository: Path,
    output: Path,
    name: str,
    revision: str = "HEAD",
) -> None:
    with output.open("wb") as raw, gzip.GzipFile(
        filename="",
        fileobj=raw,
        mode="wb",
        mtime=0,
    ) as compressed, tarfile.open(
        fileobj=compressed,
        mode="w",
        format=tarfile.PAX_FORMAT,
    ) as archive:
        for relative, mode, data in commit_entries(
            repository,
            revision,
        ):
            info = tarfile.TarInfo(
                str(Path(name) / Path(relative)).replace("\\", "/")
            )
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            if mode == 0o120000:
                try:
                    info.linkname = data.decode("utf-8")
                except UnicodeDecodeError as error:
                    raise RuntimeError(
                        f"source-tree link target is not UTF-8: {relative}"
                    ) from error
                info.type = tarfile.SYMTYPE
                info.mode = 0o777
                archive.addfile(info)
            else:
                info.mode = 0o755 if mode == 0o100755 else 0o644
                info.size = len(data)
                archive.addfile(info, BytesIO(data))
