#!/usr/bin/env python3
"""Package the exact corresponding source used by the Android APK."""

from __future__ import annotations

import argparse
import gzip
import hashlib
from io import BytesIO
from pathlib import Path
import subprocess
import sys
import tarfile

sys.dont_write_bytecode = True
from android_patchset import source_files, validate_applied
from release_paths import output_root
from project_version import validated_project_version


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


def repository_files(repository: Path) -> list[Path]:
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


def locked_value(name: str) -> str:
    for line in (ROOT / "android" / "dependencies.lock").read_text(
        encoding="utf-8"
    ).splitlines():
        key, separator, value = line.partition("=")
        if separator and key == name:
            return value
    raise RuntimeError(f"{name} is missing from android/dependencies.lock")


def native_build_root(xemu: Path) -> Path:
    builds = [
        path
        for path in (xemu / "android" / "app" / ".cxx").glob("*/*/arm64-v8a")
        if (path / "glib-src").is_dir() and (path / "_deps").is_dir()
    ]
    if not builds:
        raise RuntimeError("run scripts/build_android.sh before packaging source")
    return max(builds, key=lambda path: path.stat().st_mtime_ns)


def validate_clean_repository(name: str, repository: Path) -> None:
    if git_output(repository, "status", "--porcelain"):
        raise RuntimeError(f"{name} contains uncommitted source changes")


def validate_inputs(xemu: Path, sdl2: Path, libusb: Path) -> None:
    validate_clean_repository("GDOX", ROOT)
    validate_applied(xemu, ROOT / "android" / "emulator" / "patches")
    validate_applied(sdl2, ROOT / "android" / "emulator" / "sdl2" / "patches")
    validate_clean_repository("libusb", libusb)


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


def add_file(
    archive: tarfile.TarFile,
    source: Path,
    destination: Path,
) -> None:
    info = archive.gettarinfo(source, arcname=destination.as_posix())
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    if info.isfile():
        with source.open("rb") as contents:
            archive.addfile(info, contents)
    else:
        archive.addfile(info)


def add_repository(
    archive: tarfile.TarFile,
    repository: Path,
    destination: Path,
) -> None:
    for relative in repository_files(repository):
        source = repository / relative
        if source.exists() or source.is_symlink():
            add_file(archive, source, destination / relative)


def add_source_tree(
    archive: tarfile.TarFile,
    source: Path,
    destination: Path,
) -> None:
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if ".git" in relative.parts or not (path.is_file() or path.is_symlink()):
            continue
        add_file(archive, path, destination / relative)


def patch_digest(patch_root: Path) -> str:
    digest = hashlib.sha256()
    for line in (patch_root / "series").read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        digest.update(name.encode())
        digest.update(b"\0")
        digest.update((patch_root / name).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


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
    for source in sorted((native_build / "_deps").glob("*-src")):
        if (source / ".git").exists():
            validate_clean_repository(source.name, source)
            repositories.append((source.name.removesuffix("-src"), source))

    name = f"gdox-{version}-android-corresponding-source"
    release = output_root() / "release" / "android"
    output = release / f"{name}.tar.gz"
    release.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)

    manifest = [
        "GDOX Android corresponding source",
        f"gdox {revision(ROOT)}",
        (
            f"xemu {revision(xemu)} + patch series "
            f"{patch_digest(ROOT / 'android' / 'emulator' / 'patches')}"
        ),
        (
            f"sdl2 {revision(sdl2)} + patch series "
            f"{patch_digest(ROOT / 'android' / 'emulator' / 'sdl2' / 'patches')}"
        ),
        f"libusb {revision(libusb)}",
    ]
    for component, repository in repositories[4:]:
        manifest.append(f"{component} {revision(repository)}")
    manifest.extend(
        (
            "glib 2.66.8 sha256 "
            "97bc87dd91365589af5cbbfea2574833aea7a1b71840fd365ecd2852c76b9c8b",
            "",
        )
    )

    with output.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed:
            with tarfile.open(
                fileobj=compressed,
                mode="w",
                format=tarfile.PAX_FORMAT,
            ) as archive:
                archive_root = Path(name)
                add_bytes(
                    archive,
                    (archive_root / "SOURCE_MANIFEST.txt").as_posix(),
                    "\n".join(manifest).encode(),
                )
                for component, repository in repositories:
                    add_repository(
                        archive,
                        repository,
                        archive_root / "source" / component,
                    )
                add_source_tree(
                    archive,
                    native_build / "glib-src",
                    archive_root / "source" / "glib",
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
