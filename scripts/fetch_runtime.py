#!/usr/bin/env python3
"""Fetch and verify the exact open-source runtime used in GDOX releases."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

sys.dont_write_bytecode = True
from release_paths import cache_root


ROOT = Path(__file__).resolve().parent.parent
MANIFEST_PATH = ROOT / "packaging" / "runtime-manifest.json"
USER_AGENT = "GDOX release builder (https://gdox.korze.org)"


def load_manifest() -> dict:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise SystemExit("unsupported runtime manifest")
    return manifest


def verify(path: Path, asset: dict) -> bool:
    if not path.is_file() or path.stat().st_size != asset["size"]:
        return False
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest() == asset["sha256"]


def fetch(asset: dict, cache: Path) -> Path:
    name = asset.get("name") or asset["url"].rsplit("/", 1)[-1]
    cached = cache / f"{asset['sha256']}-{name}"
    cache.mkdir(parents=True, exist_ok=True)
    if verify(cached, asset):
        return cached
    cached.unlink(missing_ok=True)
    temporary = cached.with_suffix(cached.suffix + f".{os.getpid()}.part")
    request = urllib.request.Request(asset["url"], headers={"User-Agent": USER_AGENT})
    digest = hashlib.sha256()
    size = 0
    try:
        with urllib.request.urlopen(request) as response, temporary.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
                digest.update(chunk)
                size += len(chunk)
        if size != asset["size"] or digest.hexdigest() != asset["sha256"]:
            raise SystemExit(f"verification failed for {name}")
        temporary.replace(cached)
    finally:
        temporary.unlink(missing_ok=True)
    return cached


def safe_extract(archive_path: Path, destination: Path, members: list[str] | None = None) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        for info in archive.infolist():
            if members is not None and info.filename not in members:
                continue
            relative = Path(info.filename)
            if relative.is_absolute() or ".." in relative.parts:
                raise SystemExit(f"unsafe path in runtime archive: {info.filename}")
            output = destination / relative
            if info.is_dir():
                output.mkdir(parents=True, exist_ok=True)
                continue
            output.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, output.open("wb") as target:
                shutil.copyfileobj(source, target)
            mode = (info.external_attr >> 16) & 0o777
            if mode:
                output.chmod(mode)


def write_runtime_notes(destination: Path, manifest: dict) -> None:
    xemu = manifest["xemu"]
    source = xemu["source"]
    (destination / "SOURCE.md").write_text(
        f"""# Corresponding source

This bundle contains the unmodified xemu {xemu['version']} executable as a
separate program. Its exact corresponding source is distributed beside GDOX
release archives as `{source['name']}`.

Upstream source URL: {source['url']}

SHA-256: `{source['sha256']}`
""",
        encoding="utf-8",
    )
    (destination / "VERSIONS.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def bundle(target: str, destination: Path, cache: Path) -> None:
    manifest = load_manifest()
    try:
        asset = manifest["xemu"]["targets"][target]
    except KeyError:
        raise SystemExit(f"no bundled xemu runtime is defined for {target}") from None

    shutil.rmtree(destination, ignore_errors=True)
    xemu_directory = destination / "xemu"
    xemu_directory.mkdir(parents=True)
    download = fetch(asset, cache)
    match asset["format"]:
        case "appimage":
            download.chmod(0o755)
            with tempfile.TemporaryDirectory() as temporary:
                subprocess.run(
                    [download, "--appimage-extract"],
                    cwd=temporary,
                    check=True,
                    stdout=subprocess.DEVNULL,
                )
                shutil.copytree(
                    Path(temporary) / "squashfs-root",
                    xemu_directory / "AppDir",
                    symlinks=True,
                    copy_function=shutil.copy2,
                )
        case "windows-zip":
            safe_extract(download, xemu_directory, ["xemu.exe", "LICENSE.txt"])
        case "macos-zip":
            safe_extract(download, xemu_directory)
        case other:
            raise SystemExit(f"unsupported runtime format: {other}")

    licenses = xemu_directory / "licenses"
    licenses.mkdir()
    for license_asset in manifest["xemu"]["licenses"]:
        shutil.copy2(fetch(license_asset, cache), licenses / license_asset["name"])

    hdd = manifest["hdd"]
    hdd_directory = destination / "hdd"
    hdd_directory.mkdir()
    shutil.copy2(fetch(hdd, cache), hdd_directory / hdd["name"])
    shutil.copy2(fetch(hdd["license"], cache), hdd_directory / hdd["license"]["name"])
    write_runtime_notes(destination, manifest)


def source(output: Path, cache: Path) -> None:
    asset = load_manifest()["xemu"]["source"]
    output.mkdir(parents=True, exist_ok=True)
    archive = output / asset["name"]
    shutil.copy2(fetch(asset, cache), archive)
    checksum = archive.with_name(archive.name + ".sha256")
    checksum.write_text(f"{asset['sha256']}  {archive.name}\n", encoding="utf-8")
    print(archive)
    print(checksum)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cache",
        default=cache_root(),
        type=Path,
        help="verified download cache",
    )
    commands = parser.add_subparsers(dest="command", required=True)
    bundle_parser = commands.add_parser("bundle")
    bundle_parser.add_argument("--target", required=True)
    bundle_parser.add_argument("--destination", required=True, type=Path)
    source_parser = commands.add_parser("source")
    source_parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    cache = args.cache.resolve()

    if args.command == "bundle":
        bundle(args.target, args.destination, cache)
    else:
        source(args.output, cache)


if __name__ == "__main__":
    main()
