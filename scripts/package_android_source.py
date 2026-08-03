#!/usr/bin/env python3
"""Package the exact corresponding source used by the Android APK."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import os
import stat
import subprocess
import sys
import tarfile
from io import BytesIO
from pathlib import Path, PurePosixPath

sys.dont_write_bytecode = True
from android_patchset import source_files, validate_applied
from android_source_provenance import (
    ANDROID_GLIB_GIT_SUBPROJECTS,
    ANDROID_NATIVE_GIT_REVISION_KEYS,
    ANDROID_SOURCE_COMPONENT_ORDER,
    ANDROID_SOURCE_COMPONENTS,
    SourceTreeEntry,
    canonical_manifest_lines,
    canonical_tree_digest,
    expected_component_tree_digests,
    locked_value,
    normalized_source_mode,
)
from project_version import validated_project_version
from release_paths import output_root

ROOT = Path(__file__).resolve().parent.parent
PRIVATE_REPOSITORY_FILES = {
    Path("key.properties"),
    Path("local.properties"),
}


def run(*arguments: str) -> bytes:
    return subprocess.run(
        arguments,
        check=True,
        capture_output=True,
    ).stdout


def git_output(repository: Path, *arguments: str) -> bytes:
    return run("git", "-C", str(repository), *arguments)


def repository_files(
    repository: Path,
    *,
    tracked_only: bool = False,
) -> list[Path]:
    if tracked_only:
        return sorted(
            Path(name.decode())
            for name in git_output(repository, "ls-files", "-z").split(b"\0")
            if name
        )
    return [
        path for path in source_files(repository)
        if path not in PRIVATE_REPOSITORY_FILES
    ]


def revision(repository: Path) -> str:
    return git_output(repository, "rev-parse", "HEAD").decode().strip()


def prepared_source(script: str) -> Path:
    path = run(str(ROOT / "scripts" / script)).decode().strip()
    source = Path(path).resolve()
    if not source.is_dir():
        raise RuntimeError(f"{script} returned an invalid source path: {path}")
    return source


def native_build_root(xemu: Path) -> Path:
    builds = [
        path
        for path in (xemu / "android" / "app" / ".cxx").glob("*/*/arm64-v8a")
        if (path / "glib-src").is_dir() and (path / "_deps").is_dir()
    ]
    if not builds:
        raise RuntimeError("run scripts/build_android.sh before packaging source")
    if len(builds) != 1:
        found = ", ".join(str(path) for path in sorted(builds))
        raise RuntimeError(
            "multiple Android native source trees are present: " + found
        )
    return builds[0]


def validate_clean_repository(name: str, repository: Path) -> None:
    if git_output(repository, "status", "--porcelain"):
        raise RuntimeError(f"{name} contains uncommitted source changes")


def validate_tracked_repository(name: str, repository: Path) -> None:
    if git_output(
        repository,
        "status",
        "--porcelain",
        "--untracked-files=no",
    ):
        raise RuntimeError(f"{name} contains modified tracked source")


def validate_exact_revision(
    name: str,
    repository: Path,
    expected: str,
) -> None:
    actual = revision(repository)
    if actual != expected:
        raise RuntimeError(
            f"{name} is at {actual}, expected pinned revision {expected}"
        )


def git_repository_roots(root: Path) -> set[Path]:
    return {
        metadata.parent.resolve()
        for metadata in root.rglob(".git")
        if metadata.is_dir() or metadata.is_file()
    }


def discover_native_dependency_repositories(
    native_build: Path,
) -> tuple[list[tuple[str, Path]], Path]:
    glib_source = native_build / "glib-src"
    if not glib_source.is_dir():
        raise RuntimeError("the pinned glib source tree is missing")
    repositories: list[tuple[str, Path]] = []
    for source in sorted((native_build / "_deps").glob("*-src")):
        component = source.name.removesuffix("-src")
        if not source.is_dir() or not (source / ".git").exists():
            raise RuntimeError(
                f"{source.name} is not a Git source repository"
            )
        if component == "glib":
            raise RuntimeError("glib must use the pinned native source tree")
        repositories.append((component, source))
    return repositories, glib_source


def discover_glib_git_subprojects(glib_source: Path) -> list[tuple[str, Path]]:
    repositories = [
        (component, (glib_source / relative).resolve())
        for component, (relative, _) in ANDROID_GLIB_GIT_SUBPROJECTS.items()
    ]
    expected = {repository for _, repository in repositories}
    actual = git_repository_roots(glib_source)
    if actual != expected:
        missing = ", ".join(str(path) for path in sorted(expected - actual))
        extra = ", ".join(str(path) for path in sorted(actual - expected))
        raise RuntimeError(
            "GLib Git subproject mismatch: "
            f"missing [{missing or 'none'}]; extra [{extra or 'none'}]"
        )
    return repositories


def validate_native_git_inventory(
    native_build: Path,
    repositories: list[tuple[str, Path]],
    glib_repositories: list[tuple[str, Path]],
) -> None:
    expected = {
        repository.resolve()
        for _, repository in (*repositories, *glib_repositories)
    }
    actual = git_repository_roots(native_build / "_deps") | {
        repository.resolve()
        for _, repository in glib_repositories
    }
    if actual != expected:
        missing = ", ".join(str(path) for path in sorted(expected - actual))
        extra = ", ".join(str(path) for path in sorted(actual - expected))
        raise RuntimeError(
            "Android native Git dependency mismatch: "
            f"missing [{missing or 'none'}]; extra [{extra or 'none'}]"
        )


def validate_component_set(
    repositories: list[tuple[str, Path]],
) -> None:
    component_names = [component for component, _ in repositories]
    actual_components = set(component_names) | {"glib"}
    if (
        len(component_names) != len(set(component_names))
        or actual_components != ANDROID_SOURCE_COMPONENTS
    ):
        actual = ", ".join(sorted(actual_components))
        expected = ", ".join(sorted(ANDROID_SOURCE_COMPONENTS))
        raise RuntimeError(
            "Android corresponding-source component mismatch: "
            f"expected {expected}; found {actual}"
        )


def validate_inputs(xemu: Path, sdl2: Path, libusb: Path) -> None:
    validate_clean_repository("GDOX", ROOT)
    validate_exact_revision(
        "xemu",
        xemu,
        locked_value("XEMU_ANDROID_BASE_REVISION"),
    )
    validate_applied(xemu, ROOT / "android" / "emulator" / "patches")
    validate_exact_revision("SDL2", sdl2, locked_value("SDL2_REVISION"))
    validate_applied(sdl2, ROOT / "android" / "emulator" / "sdl2" / "patches")
    validate_exact_revision("libusb", libusb, locked_value("LIBUSB_REVISION"))
    validate_clean_repository("libusb", libusb)


def validate_sha256(path: Path, expected: str) -> None:
    digest = file_sha256(path)
    if digest != expected:
        raise RuntimeError(
            f"{path} has SHA-256 {digest}, expected {expected}"
        )


def file_sha256(path: Path) -> str:
    with path.open("rb") as source:
        return stream_sha256(source)


def stream_sha256(source) -> str:
    digest = hashlib.sha256()
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def add_bytes(
    archive: tarfile.TarFile,
    name: str,
    data: bytes,
    mode: int = 0o644,
) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    archive.addfile(info, BytesIO(data))


def repository_tree_entries(
    repository: Path,
    *,
    tracked_only: bool = False,
) -> list[SourceTreeEntry]:
    entries: list[SourceTreeEntry] = []
    for relative in repository_files(repository, tracked_only=tracked_only):
        source = repository / relative
        try:
            metadata = source.lstat()
        except FileNotFoundError:
            continue
        mode = stat.S_IMODE(metadata.st_mode)
        if stat.S_ISDIR(metadata.st_mode):
            continue
        if stat.S_ISLNK(metadata.st_mode):
            entries.append(
                SourceTreeEntry(
                    relative.as_posix(),
                    "symlink",
                    mode,
                    symlink_target=os.fsdecode(os.readlink(source)),
                )
            )
            continue
        if not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"unsupported source file type: {source}")
        entries.append(
            SourceTreeEntry(
                relative.as_posix(),
                "file",
                mode,
                size=metadata.st_size,
                content_sha256=file_sha256(source),
            )
        )
    return entries


def validate_component_tree(
    component: str,
    entries: list[SourceTreeEntry],
    expected: dict[str, str],
) -> None:
    if not entries:
        raise RuntimeError(f"Android source component is empty: {component}")
    actual = canonical_tree_digest(entries)
    pinned = expected.get(component)
    if pinned is not None and actual != pinned:
        raise RuntimeError(
            f"{component} source tree has SHA-256 {actual}, expected {pinned}"
        )


def add_repository_tree_entries(
    archive: tarfile.TarFile,
    entries: list[SourceTreeEntry],
    repository: Path,
    destination: Path,
) -> None:
    for entry in entries:
        name = (destination / entry.path).as_posix()
        if entry.kind == "file":
            source = repository / entry.path
            info = tarfile.TarInfo(name)
            info.size = entry.size
            info.mode = normalized_source_mode(entry.kind, entry.mode)
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with source.open("rb") as contents:
                archive.addfile(info, contents)
            continue
        if entry.kind != "symlink":
            raise RuntimeError(f"unsupported source member type: {entry.kind}")
        info = tarfile.TarInfo(name)
        info.type = tarfile.SYMTYPE
        info.linkname = entry.symlink_target
        info.mode = normalized_source_mode(entry.kind, entry.mode)
        info.mtime = 0
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        archive.addfile(info)


def pristine_tar_entries(
    source_archive: Path,
    source_root: str,
) -> list[SourceTreeEntry]:
    entries: list[SourceTreeEntry] = []
    with tarfile.open(source_archive, "r:*") as source:
        for member in sorted(source.getmembers(), key=lambda item: item.name):
            path = PurePosixPath(member.name)
            if (
                path.is_absolute()
                or ".." in path.parts
                or not path.parts
                or path.parts[0] != source_root
            ):
                raise RuntimeError(
                    f"unsafe or unexpected source member: {member.name}"
                )
            relative = Path(*path.parts[1:])
            if not relative.parts or member.isdir():
                continue
            if member.issym():
                entries.append(
                    SourceTreeEntry(
                        relative.as_posix(),
                        "symlink",
                        member.mode,
                        symlink_target=member.linkname,
                    )
                )
                continue
            if not member.isfile():
                raise RuntimeError(
                    f"unsupported source member type: {member.name}"
                )
            contents = source.extractfile(member)
            if contents is None:
                raise RuntimeError(f"could not read source member: {member.name}")
            entries.append(
                SourceTreeEntry(
                    relative.as_posix(),
                    "file",
                    member.mode,
                    size=member.size,
                    content_sha256=stream_sha256(contents),
                )
            )
    if not entries:
        raise RuntimeError(f"{source_archive} contains no source files")
    canonical_tree_digest(entries)
    return entries


def add_pristine_tar_source(
    archive: tarfile.TarFile,
    source_archive: Path,
    source_root: str,
    destination: Path,
) -> None:
    with tarfile.open(source_archive, "r:*") as source:
        for member in sorted(source.getmembers(), key=lambda item: item.name):
            path = PurePosixPath(member.name)
            if not path.parts or path.parts[0] != source_root:
                raise RuntimeError(
                    f"unsafe or unexpected source member: {member.name}"
                )
            relative = Path(*path.parts[1:])
            if not relative.parts or member.isdir():
                continue
            name = (destination / relative).as_posix()
            if member.issym():
                info = tarfile.TarInfo(name)
                info.type = tarfile.SYMTYPE
                info.linkname = member.linkname
                info.mode = normalized_source_mode("symlink", member.mode)
                info.mtime = 0
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                archive.addfile(info)
                continue
            if not member.isfile():
                raise RuntimeError(
                    f"unsupported source member type: {member.name}"
                )
            contents = source.extractfile(member)
            if contents is None:
                raise RuntimeError(f"could not read source member: {member.name}")
            info = tarfile.TarInfo(name)
            info.size = member.size
            info.mode = normalized_source_mode("file", member.mode)
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            archive.addfile(info, contents)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        help="release version; defaults to and must match the CMake project",
    )
    args = parser.parse_args()
    try:
        version = validated_project_version(args.version)
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))

    xemu = prepared_source("prepare_android_emulator.sh")
    sdl2 = prepared_source("prepare_android_sdl2.sh")
    libusb = (
        output_root()
        / "build"
        / "android-emulator"
        / "source"
        / f"libusb-{locked_value('LIBUSB_REVISION')[:12]}"
    ).resolve()
    if not libusb.is_dir():
        raise RuntimeError("run scripts/build_android.sh before packaging source")
    native_build = native_build_root(xemu)

    validate_inputs(xemu, sdl2, libusb)

    repositories: list[tuple[str, Path]] = [
        ("gdox", ROOT),
        ("xemu", xemu),
        ("sdl2", sdl2),
        ("libusb", libusb),
    ]
    native_repositories, glib_source = (
        discover_native_dependency_repositories(native_build)
    )
    glib_repositories = discover_glib_git_subprojects(glib_source)
    repositories.extend(native_repositories)
    repositories.extend(glib_repositories)
    validate_component_set(repositories)
    validate_native_git_inventory(
        native_build,
        native_repositories,
        glib_repositories,
    )
    for component, source in native_repositories:
        validate_exact_revision(
            component,
            source,
            locked_value(ANDROID_NATIVE_GIT_REVISION_KEYS[component]),
        )
        validate_clean_repository(component, source)
    for component, source in glib_repositories:
        _, revision_key = ANDROID_GLIB_GIT_SUBPROJECTS[component]
        validate_exact_revision(
            component,
            source,
            locked_value(revision_key),
        )
        validate_tracked_repository(component, source)

    glib_version = locked_value("GLIB_VERSION")
    glib_archive = native_build / "downloads" / f"glib-{glib_version}.tar.xz"
    if not glib_archive.is_file():
        raise RuntimeError(f"the pinned GLib source archive is missing: {glib_archive}")
    validate_sha256(glib_archive, locked_value("GLIB_SOURCE_SHA256"))

    expected_trees = expected_component_tree_digests()
    component_repositories = dict(repositories)
    component_entries = {
        component: repository_tree_entries(
            repository,
            tracked_only=(component in ANDROID_GLIB_GIT_SUBPROJECTS),
        )
        for component, repository in repositories
    }
    component_entries["glib"] = pristine_tar_entries(
        glib_archive,
        f"glib-{glib_version}",
    )
    for component in ANDROID_SOURCE_COMPONENT_ORDER:
        validate_component_tree(
            component,
            component_entries[component],
            expected_trees,
        )

    name = f"gdox-{version}-android-corresponding-source"
    release = output_root() / "release" / "android"
    output = release / f"{name}.tar.gz"
    release.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)

    manifest = canonical_manifest_lines(revision(ROOT))

    with (
        output.open("wb") as raw,
        gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed,
        tarfile.open(
            fileobj=compressed,
            mode="w",
            format=tarfile.PAX_FORMAT,
        ) as archive,
    ):
                archive_root = Path(name)
                add_bytes(
                    archive,
                    (archive_root / "SOURCE_MANIFEST.txt").as_posix(),
                    ("\n".join(manifest) + "\n").encode(),
                )
                for component in ANDROID_SOURCE_COMPONENT_ORDER:
                    destination = archive_root / "source" / component
                    if component == "glib":
                        add_pristine_tar_source(
                            archive,
                            glib_archive,
                            f"glib-{glib_version}",
                            destination,
                        )
                    else:
                        add_repository_tree_entries(
                            archive,
                            component_entries[component],
                            component_repositories[component],
                            destination,
                        )

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
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Android source packaging failed: {error}", file=sys.stderr)
        raise SystemExit(1)
