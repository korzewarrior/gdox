#!/usr/bin/env python3
"""Package the exact source corresponding to a desktop GDOX release."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import subprocess
import sys
import tarfile
from io import BytesIO
from pathlib import Path

sys.dont_write_bytecode = True
from fetch_runtime import fetch, load_manifest
from package_source import commit_entries, git_output, validate_clean_repository
from project_version import validated_project_version
from release_archive import file_sha256
from release_paths import cache_root, output_root

ROOT = Path(__file__).resolve().parent.parent


def component_definitions() -> tuple[tuple[str, int, str], ...]:
    manifest = load_manifest()
    definitions = (
        manifest["xemu"]["source"],
        manifest["linux_bridge"]["source"],
        manifest["linux_bridge"]["recipe"],
    )
    return tuple(
        (definition["name"], definition["size"], definition["sha256"])
        for definition in definitions
    )


def contents_text(
    commit: str,
    components: tuple[tuple[str, int, str], ...],
) -> bytes:
    lines = [f"gdox commit {commit}", ""]
    lines.extend(
        f"{digest}  components/{name}" for name, _, digest in components
    )
    lines.append("")
    return "\n".join(lines).encode("utf-8")


def add_bytes(
    archive: tarfile.TarFile,
    name: str,
    data: bytes,
    *,
    mode: int = 0o644,
) -> None:
    info = tarfile.TarInfo(name)
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mode = mode
    info.size = len(data)
    archive.addfile(info, BytesIO(data))


def create_corresponding_source_archive(
    repository: Path,
    output: Path,
    version: str,
    components: tuple[tuple[str, Path, str], ...],
    *,
    revision: str = "HEAD",
) -> None:
    root = f"gdox-{version}-corresponding-source"
    commit = git_output(repository, "rev-parse", revision).decode("ascii").strip()
    names = [name for name, _, _ in components]
    if len(names) != len(set(names)) or any(
        name != Path(name).name or not name for name in names
    ):
        raise ValueError("corresponding-source component names must be unique files")
    for name, path, digest in components:
        if not path.is_file() or file_sha256(path) != digest:
            raise ValueError(f"corresponding-source component changed: {name}")
    definitions = tuple(
        (name, path.stat().st_size, digest) for name, path, digest in components
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
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
        add_bytes(
            archive,
            f"{root}/CONTENTS.txt",
            contents_text(commit, definitions),
        )
        for relative, mode, data in commit_entries(repository, revision):
            if mode == 0o120000:
                info = tarfile.TarInfo(f"{root}/gdox/{relative}")
                info.mtime = 0
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.type = tarfile.SYMTYPE
                info.mode = 0o777
                info.linkname = data.decode("utf-8")
                archive.addfile(info)
                continue
            add_bytes(
                archive,
                f"{root}/gdox/{relative}",
                data,
                mode=0o755 if mode == 0o100755 else 0o644,
            )
        for name, path, _ in components:
            add_bytes(
                archive,
                f"{root}/components/{name}",
                path.read_bytes(),
            )


def audit_corresponding_source_archive(
    repository: Path,
    archive_path: Path,
    version: str,
    components: tuple[tuple[str, int, str], ...] | None = None,
    *,
    revision: str = "HEAD",
) -> None:
    root = f"gdox-{version}-corresponding-source"
    expected_name = f"{root}.tar.gz"
    if archive_path.name != expected_name:
        raise ValueError("corresponding-source archive name is invalid")
    definitions = components if components is not None else component_definitions()
    names = [name for name, _, _ in definitions]
    if len(names) != len(set(names)) or any(
        name != Path(name).name or not name for name in names
    ):
        raise ValueError("corresponding-source component definitions are invalid")
    commit = git_output(repository, "rev-parse", revision).decode("ascii").strip()
    tree = commit_entries(repository, revision)
    expected = {
        f"{root}/CONTENTS.txt",
        *(f"{root}/gdox/{relative}" for relative, _, _ in tree),
        *(f"{root}/components/{name}" for name, _, _ in definitions),
    }

    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            members = archive.getmembers()
            observed = [member.name for member in members]
            if len(observed) != len(set(observed)) or set(observed) != expected:
                raise ValueError(
                    "corresponding-source archive has missing, extra, or duplicate members"
                )
            indexed = {member.name: member for member in members}
            contents = indexed[f"{root}/CONTENTS.txt"]
            source = archive.extractfile(contents) if contents.isfile() else None
            if source is None or source.read() != contents_text(commit, definitions):
                raise ValueError("corresponding-source inventory is invalid")
            for relative, mode, data in tree:
                member = indexed[f"{root}/gdox/{relative}"]
                if mode == 0o120000:
                    try:
                        target = data.decode("utf-8")
                    except UnicodeDecodeError as error:
                        raise ValueError(
                            "corresponding-source link target is invalid"
                        ) from error
                    if not member.issym() or member.linkname != target:
                        raise ValueError(
                            "corresponding-source GDOX tree link changed"
                        )
                    continue
                source = archive.extractfile(member) if member.isfile() else None
                expected_mode = 0o755 if mode == 0o100755 else 0o644
                if (
                    source is None
                    or member.mode != expected_mode
                    or source.read() != data
                ):
                    raise ValueError("corresponding-source GDOX tree changed")
            for name, size, digest in definitions:
                member = indexed[f"{root}/components/{name}"]
                source = archive.extractfile(member) if member.isfile() else None
                component_digest = hashlib.sha256()
                component_size = 0
                if source is None:
                    raise ValueError("corresponding-source component is invalid")
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    component_size += len(chunk)
                    component_digest.update(chunk)
                if component_size != size or component_digest.hexdigest() != digest:
                    raise ValueError("corresponding-source component changed")
    except (OSError, tarfile.TarError) as error:
        raise ValueError(f"could not read corresponding-source archive: {error}") from None


def component_sources() -> tuple[tuple[str, Path, str], ...]:
    manifest = load_manifest()
    definitions = (
        manifest["xemu"]["source"],
        manifest["linux_bridge"]["source"],
        manifest["linux_bridge"]["recipe"],
    )
    cache = cache_root()
    return tuple(
        (definition["name"], fetch(definition, cache), definition["sha256"])
        for definition in definitions
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        help="release version; defaults to and must match the CMake project",
    )
    parser.add_argument(
        "--output",
        default=output_root() / "release",
        type=Path,
    )
    arguments = parser.parse_args()
    try:
        version = validated_project_version(arguments.version)
        validate_clean_repository()
    except (
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        ValueError,
    ) as error:
        parser.error(str(error))

    archive = arguments.output / f"gdox-{version}-corresponding-source.tar.gz"
    create_corresponding_source_archive(
        ROOT,
        archive,
        version,
        component_sources(),
    )
    audit_corresponding_source_archive(ROOT, archive, version)
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--artifact",
            str(archive),
        ],
        check=True,
    )
    print(archive)


if __name__ == "__main__":
    main()
