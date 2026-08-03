"""Stage a verified desktop emulator bundle from policy-approved assets."""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import zipfile
from collections.abc import Callable
from pathlib import Path

FetchAsset = Callable[[dict, Path], Path]
BundleXeniaAssets = Callable[[str, Path, Path, dict], int]


def safe_extract(
    archive_path: Path,
    destination: Path,
    members: list[str] | None = None,
) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        for info in archive.infolist():
            if members is not None and info.filename not in members:
                continue
            relative = Path(info.filename)
            if relative.is_absolute() or ".." in relative.parts:
                raise SystemExit(
                    f"unsafe path in runtime archive: {info.filename}"
                )
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


def write_runtime_notes(
    destination: Path,
    manifest: dict,
    xenia_bundled: bool,
) -> None:
    xemu = manifest["xemu"]
    source = xemu["source"]
    xenia_note = ""
    if xenia_bundled:
        xenia_note = (
            "This bundle also contains exact, reviewed Xenia Canary\n"
            "executables. Their upstream commits, distribution origins, patch\n"
            "and build provenance when applicable, sizes, and SHA-256 digests\n"
            "are recorded in `VERSIONS.json`. Xenia is distributed under the\n"
            "BSD 3-Clause license retained beside each executable. The GDOX\n"
            "source archive contains the recorded integration patches and\n"
            "Windows build recipe.\n\n"
        )
    (destination / "SOURCE.md").write_text(
        f"""# Corresponding source

This bundle contains the reviewed GDOX-patched xemu {xemu['version']} executable
as a separate program. Its exact corresponding source and the maintained GDOX
patch series are distributed with the GDOX release sources; the pristine source
archive is distributed beside the release archives as `{source['name']}`.

{xenia_note}Upstream source URL: {source['url']}

SHA-256: `{source['sha256']}`
""",
        encoding="utf-8",
    )
    (destination / "VERSIONS.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def stage_runtime_bundle(
    target: str,
    destination: Path,
    cache: Path,
    manifest: dict,
    fetch_asset: FetchAsset,
    bundle_xenia_assets: BundleXeniaAssets,
) -> None:
    try:
        asset = manifest["xemu"]["targets"][target]
    except KeyError:
        raise SystemExit(
            f"no bundled xemu runtime is defined for {target}"
        ) from None

    shutil.rmtree(destination, ignore_errors=True)
    xemu_directory = destination / "xemu"
    xemu_directory.mkdir(parents=True)
    download = fetch_asset(asset, cache)
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
            safe_extract(
                download,
                xemu_directory,
                ["xemu.exe", "LICENSE.txt"],
            )
        case "macos-zip":
            safe_extract(download, xemu_directory)
        case other:
            raise SystemExit(f"unsupported runtime format: {other}")

    licenses = xemu_directory / "licenses"
    licenses.mkdir()
    for license_asset in manifest["xemu"]["licenses"]:
        shutil.copy2(
            fetch_asset(license_asset, cache),
            licenses / license_asset["name"],
        )

    hdd = manifest["hdd"]
    hdd_directory = destination / "hdd"
    hdd_directory.mkdir()
    shutil.copy2(fetch_asset(hdd, cache), hdd_directory / hdd["name"])
    shutil.copy2(
        fetch_asset(hdd["license"], cache),
        hdd_directory / hdd["license"]["name"],
    )
    xenia_bundled = bundle_xenia_assets(
        target,
        destination,
        cache,
        manifest,
    )
    if xenia_bundled == 0 and target not in {
        "x86_64-apple-darwin",
        "aarch64-apple-darwin",
    }:
        raise SystemExit(f"no bundled Xenia runtime is defined for {target}")
    write_runtime_notes(destination, manifest, xenia_bundled != 0)
