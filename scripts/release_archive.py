"""Deterministic filesystem helpers shared by GDOX release tooling."""

from __future__ import annotations

import gzip
import os
from pathlib import Path
import shutil
import tarfile
import zipfile


def copy_file(source: Path, destination: Path, *, executable: bool = False) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if os.name != "nt":
        destination.chmod(0o755 if executable else 0o644)


def create_archive(stage: Path, output: Path, *, windows: bool) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if windows:
        _create_zip(stage, output)
    else:
        _create_tar(stage, output)


def _create_zip(stage: Path, output: Path) -> None:
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(stage.rglob("*")):
            if not path.is_file():
                continue
            name = str(Path(stage.name) / path.relative_to(stage)).replace(
                "\\", "/"
            )
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o100755 if path.stat().st_mode & 0o111 else 0o100644
            info.external_attr = mode << 16
            archive.writestr(info, path.read_bytes())


def _create_tar(stage: Path, output: Path) -> None:
    def normalize(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.mtime = 0
        info.uid = 0
        info.gid = 0
        info.uname = ""
        info.gname = ""
        if info.isdir():
            info.mode = 0o755
        elif info.issym():
            info.mode = 0o777
        elif info.isfile():
            info.mode = 0o755 if info.mode & 0o111 else 0o644
        return info

    with output.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed:
            with tarfile.open(
                fileobj=compressed,
                mode="w",
                format=tarfile.PAX_FORMAT,
            ) as archive:
                archive.add(
                    stage,
                    arcname=stage.name,
                    recursive=True,
                    filter=normalize,
                )
