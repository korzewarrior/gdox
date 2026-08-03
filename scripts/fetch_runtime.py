#!/usr/bin/env python3
"""Fetch and verify the exact open-source runtime used in GDOX releases."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import urllib.request
from pathlib import Path, PurePosixPath

sys.dont_write_bytecode = True
from linux_bridge_distribution import (
    validate_runtime_manifest as validate_linux_bridge_manifest,
)
from release_paths import cache_root
from runtime_bundle_distribution import stage_runtime_bundle
from xemu_integration import require_publishable as require_xemu_publishable
from xenia_distribution import (
    bundle_publishable as bundle_xenia,
)
from xenia_distribution import (
    require_publishable as require_xenia_publishable,
)
from xenia_distribution import (
    validate_runtime_manifest as validate_xenia_runtime_manifest,
)

ROOT = Path(__file__).resolve().parent.parent
MANIFEST_PATH = ROOT / "packaging" / "runtime-manifest.json"
USER_AGENT = "GDOX release builder (https://gdox.korze.org)"
XEMU_TARGET_FORMATS = {
    "x86_64-unknown-linux-gnu": "appimage",
    "x86_64-pc-windows-msvc": "windows-zip",
    "x86_64-apple-darwin": "macos-zip",
    "aarch64-apple-darwin": "macos-zip",
}


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise SystemExit(f"duplicate key in runtime manifest: {key}")
        result[key] = value
    return result


def require_asset(asset: object, label: str) -> dict:
    if not isinstance(asset, dict):
        raise SystemExit(f"{label} must be an object")
    required = {"url", "sha256", "size"}
    missing = sorted(required - asset.keys())
    if missing:
        raise SystemExit(f"{label} is missing {', '.join(missing)}")
    if not isinstance(asset["url"], str) or not asset["url"]:
        raise SystemExit(f"{label}.url must be a non-empty string")
    if not asset["url"].startswith("https://"):
        raise SystemExit(f"{label}.url must use HTTPS")
    if (
        not isinstance(asset["sha256"], str)
        or len(asset["sha256"]) != 64
        or any(character not in "0123456789abcdef" for character in asset["sha256"])
    ):
        raise SystemExit(f"{label}.sha256 must be a lowercase SHA-256 digest")
    if (
        not isinstance(asset["size"], int)
        or isinstance(asset["size"], bool)
        or asset["size"] <= 0
    ):
        raise SystemExit(f"{label}.size must be a positive integer")
    name = asset.get("name")
    if name is not None and (
        not isinstance(name, str)
        or not name
        or name != Path(name).name
        or "\\" in name
    ):
        raise SystemExit(f"{label}.name must be a plain file name")
    return asset


def require_embedded_file(file: object, label: str) -> dict:
    if not isinstance(file, dict):
        raise SystemExit(f"{label} must be an object")
    required = {"member", "sha256", "size"}
    unexpected = sorted(file.keys() - required)
    if unexpected:
        raise SystemExit(f"{label} has unexpected {', '.join(unexpected)}")
    missing = sorted(required - file.keys())
    if missing:
        raise SystemExit(f"{label} is missing {', '.join(missing)}")
    member = file["member"]
    if not isinstance(member, str) or not member.startswith("runtime/xemu/"):
        raise SystemExit(f"{label}.member must be below runtime/xemu")
    path = PurePosixPath(member)
    if (
        path.is_absolute()
        or ".." in path.parts
        or "\\" in member
        or path.as_posix() != member
    ):
        raise SystemExit(f"{label}.member must be a normalized relative path")
    if (
        not isinstance(file["sha256"], str)
        or len(file["sha256"]) != 64
        or any(
            character not in "0123456789abcdef"
            for character in file["sha256"]
        )
    ):
        raise SystemExit(f"{label}.sha256 must be a lowercase SHA-256 digest")
    if (
        not isinstance(file["size"], int)
        or isinstance(file["size"], bool)
        or file["size"] <= 0
    ):
        raise SystemExit(f"{label}.size must be a positive integer")
    return file


def validate_manifest(manifest: dict) -> None:
    if not isinstance(manifest, dict) or set(manifest) != {
        "hdd",
        "linux_bridge",
        "schema",
        "xemu",
        "xenia",
    }:
        raise SystemExit("runtime manifest has missing or unknown fields")
    if manifest.get("schema") != 1:
        raise SystemExit("unsupported runtime manifest")
    xemu = manifest.get("xemu")
    if not isinstance(xemu, dict) or set(xemu) != {
        "embedded_build_path_files",
        "embedded_notice_files",
        "embedded_privacy_files",
        "licenses",
        "source",
        "targets",
        "version",
    }:
        raise SystemExit("xemu definition has missing or unknown fields")
    if (
        not isinstance(xemu["version"], str)
        or re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", xemu["version"])
        is None
    ):
        raise SystemExit("xemu.version must be a semantic release version")
    embedded_files = xemu.get("embedded_build_path_files")
    if not isinstance(embedded_files, list) or not embedded_files:
        raise SystemExit("xemu embedded build-path file list is missing")
    embedded_members: set[str] = set()
    for index, embedded_file in enumerate(embedded_files):
        definition = require_embedded_file(
            embedded_file,
            f"xemu.embedded_build_path_files[{index}]",
        )
        if definition["member"] in embedded_members:
            raise SystemExit("duplicate xemu embedded build-path member")
        embedded_members.add(definition["member"])
    privacy_files = xemu.get("embedded_privacy_files")
    if not isinstance(privacy_files, list) or not privacy_files:
        raise SystemExit("xemu embedded privacy file list is missing")
    privacy_members: set[str] = set()
    for index, privacy_file in enumerate(privacy_files):
        definition = require_embedded_file(
            privacy_file,
            f"xemu.embedded_privacy_files[{index}]",
        )
        if definition["member"] in privacy_members:
            raise SystemExit("duplicate xemu embedded privacy member")
        privacy_members.add(definition["member"])
    notice_files = xemu.get("embedded_notice_files")
    if not isinstance(notice_files, list) or not notice_files:
        raise SystemExit("xemu embedded notice file list is missing")
    notice_identities: set[tuple[str, int, str]] = set()
    for index, notice_file in enumerate(notice_files):
        definition = require_embedded_file(
            notice_file,
            f"xemu.embedded_notice_files[{index}]",
        )
        identity = (
            definition["member"],
            definition["size"],
            definition["sha256"],
        )
        if identity in notice_identities:
            raise SystemExit("duplicate xemu embedded notice file identity")
        notice_identities.add(identity)
    source = require_asset(xemu.get("source"), "xemu.source")
    if set(source) != {"name", "sha256", "size", "url"}:
        raise SystemExit("xemu.source has missing or unknown fields")
    targets = xemu.get("targets")
    if not isinstance(targets, dict) or set(targets) != set(XEMU_TARGET_FORMATS):
        raise SystemExit("xemu.targets must define every supported desktop target")
    for target, asset in targets.items():
        definition = require_asset(asset, f"xemu.targets.{target}")
        if set(definition) != {"format", "sha256", "size", "url"}:
            raise SystemExit(
                f"xemu.targets.{target} has missing or unknown fields"
            )
        if definition["format"] != XEMU_TARGET_FORMATS[target]:
            raise SystemExit(f"xemu.targets.{target}.format is invalid")
    licenses = xemu.get("licenses")
    if not isinstance(licenses, list) or not licenses:
        raise SystemExit("runtime manifest has no xemu licenses")
    license_names: set[str] = set()
    for index, asset in enumerate(licenses):
        definition = require_asset(asset, f"xemu.licenses[{index}]")
        if set(definition) != {"name", "sha256", "size", "url"}:
            raise SystemExit(
                f"xemu.licenses[{index}] has missing or unknown fields"
            )
        if definition["name"] in license_names:
            raise SystemExit("xemu license file names must be unique")
        license_names.add(definition["name"])
    libusb_license = next(
        (
            definition
            for definition in licenses
            if definition["name"] == "COPYING.LIB"
        ),
        None,
    )
    packaged_libusb_license = (
        ROOT / "packaging/licenses/libusb-LGPL-2.1.txt"
    )
    if (
        libusb_license is None
        or not verify(packaged_libusb_license, libusb_license)
    ):
        raise SystemExit("the packaged libusb license differs from its pin")
    hdd = require_asset(manifest.get("hdd"), "hdd")
    if set(hdd) != {"license", "name", "sha256", "size", "url", "version"}:
        raise SystemExit("hdd has missing or unknown fields")
    if not isinstance(hdd["version"], str) or not hdd["version"]:
        raise SystemExit("hdd.version must be a non-empty string")
    hdd_license = require_asset(hdd.get("license"), "hdd.license")
    if set(hdd_license) != {"name", "sha256", "size", "url"}:
        raise SystemExit("hdd.license has missing or unknown fields")
    validate_xenia_runtime_manifest(manifest)
    validate_linux_bridge_manifest(manifest)


def load_manifest() -> dict:
    try:
        manifest = json.loads(
            MANIFEST_PATH.read_text(encoding="utf-8"),
            object_pairs_hook=unique_object,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"could not read runtime manifest: {error}") from None
    validate_manifest(manifest)
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
                if size > asset["size"]:
                    raise SystemExit(f"verification failed for {name}: oversized")
        if size != asset["size"] or digest.hexdigest() != asset["sha256"]:
            raise SystemExit(f"verification failed for {name}")
        temporary.replace(cached)
    finally:
        temporary.unlink(missing_ok=True)
    return cached


def bundle(
    target: str,
    destination: Path,
    cache: Path,
) -> None:
    manifest = load_manifest()
    require_xemu_publishable(target=target)
    require_xenia_publishable(target, manifest)

    def bundle_published_xenia(
        runtime_target: str,
        runtime_destination: Path,
        runtime_cache: Path,
        runtime_manifest: dict,
    ) -> int:
        return bundle_xenia(
            runtime_target,
            runtime_destination,
            runtime_cache,
            runtime_manifest,
            fetch,
        )

    stage_runtime_bundle(
        target,
        destination,
        cache,
        manifest,
        fetch,
        bundle_published_xenia,
    )


def source(output: Path, cache: Path) -> None:
    manifest = load_manifest()
    asset = manifest["xemu"]["source"]
    output.mkdir(parents=True, exist_ok=True)
    for definition in (
        asset,
        manifest["linux_bridge"]["source"],
        manifest["linux_bridge"]["recipe"],
    ):
        archive = output / definition["name"]
        shutil.copy2(fetch(definition, cache), archive)
        checksum = archive.with_name(archive.name + ".sha256")
        checksum.write_text(
            f"{definition['sha256']}  {archive.name}\n",
            encoding="utf-8",
        )
        print(archive)
        print(checksum)


def materialize_asset(name: str, output: Path, cache: Path) -> None:
    manifest = load_manifest()
    assets = {
        "hdd": manifest["hdd"],
        "hdd-license": manifest["hdd"]["license"],
    }
    asset = assets[name]
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.{os.getpid()}.part")
    try:
        shutil.copy2(fetch(asset, cache), temporary)
        if not verify(temporary, asset):
            raise SystemExit(f"verification failed for {asset['name']}")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def publication_runtime_assets(manifest: dict) -> dict[str, dict]:
    assets: dict[str, dict] = {}
    includes_patched_xemu = False

    def add(asset: dict, name: str) -> None:
        existing = assets.get(name)
        if existing is not None and (
            existing["url"], existing["sha256"], existing["size"]
        ) != (asset["url"], asset["sha256"], asset["size"]):
            raise SystemExit(f"runtime release repeats {name} inconsistently")
        assets[name] = asset

    downstream_prefix = (
        "https://github.com/korzewarrior/gdox/releases/download/runtime-"
    )
    for asset in manifest["xemu"]["targets"].values():
        if asset["url"].startswith(downstream_prefix):
            add(asset, asset["url"].rsplit("/", 1)[-1])
            includes_patched_xemu = True
    if includes_patched_xemu:
        source_asset = manifest["xemu"]["source"]
        add(source_asset, source_asset["name"])
    for definition in manifest["xenia"]["revisions"].values():
        for asset in definition["targets"].values():
            if (
                asset["origin"] == "gdox-patched"
                and asset["release_state"] == "published"
            ):
                add(asset, asset["archive_name"])
    if not assets:
        raise SystemExit("runtime manifest defines no downstream release assets")
    return dict(sorted(assets.items()))


def audit_runtime_release(directory: Path) -> None:
    manifest = load_manifest()
    assets = publication_runtime_assets(manifest)
    directory = directory.resolve()
    if not directory.is_dir():
        raise SystemExit(f"runtime release directory is unavailable: {directory}")
    expected_names = set(assets) | {"SHA256SUMS"}
    entries = list(directory.iterdir())
    actual_names = {entry.name for entry in entries}
    unsafe = [entry.name for entry in entries if entry.is_symlink()]
    if unsafe or actual_names != expected_names or any(
        not entry.is_file() for entry in entries
    ):
        details = sorted(actual_names.symmetric_difference(expected_names) | set(unsafe))
        raise SystemExit(
            "runtime release directory has missing, extra, or unsafe entries: "
            + ", ".join(details)
        )
    for name, asset in assets.items():
        if not verify(directory / name, asset):
            raise SystemExit(f"runtime release asset failed verification: {name}")
    expected_checksums = "".join(
        f"{asset['sha256']}  {name}\n" for name, asset in assets.items()
    )
    try:
        observed_checksums = (directory / "SHA256SUMS").read_text(
            encoding="utf-8"
        )
    except (OSError, UnicodeError) as error:
        raise SystemExit(f"could not read runtime release checksums: {error}") from None
    if observed_checksums != expected_checksums:
        raise SystemExit("runtime release SHA256SUMS does not match the manifest")
    print(f"Runtime release contains {len(assets)} exact reviewed assets.")


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
    asset_parser = commands.add_parser("asset")
    asset_parser.add_argument(
        "--name", required=True, choices=("hdd", "hdd-license")
    )
    asset_parser.add_argument("--output", required=True, type=Path)
    publishable_parser = commands.add_parser("publishable")
    publishable_parser.add_argument("--target", required=True)
    audit_runtime_parser = commands.add_parser("audit-runtime-release")
    audit_runtime_parser.add_argument("--path", required=True, type=Path)
    commands.add_parser("validate")
    args = parser.parse_args()
    cache = args.cache.resolve()

    if args.command == "bundle":
        bundle(args.target, args.destination, cache)
    elif args.command == "source":
        source(args.output, cache)
    elif args.command == "asset":
        materialize_asset(args.name, args.output, cache)
    elif args.command == "publishable":
        manifest = load_manifest()
        require_xemu_publishable(target=args.target)
        require_xenia_publishable(args.target, manifest)
        print(f"Runtime assets are publishable for {args.target}.")
    elif args.command == "audit-runtime-release":
        audit_runtime_release(args.path)
    else:
        load_manifest()
        print("Runtime manifest is valid.")


if __name__ == "__main__":
    main()
