"""Validate and stage the pinned Steam Deck physical-disc bridge."""

from __future__ import annotations

import hashlib
import json
import subprocess
from collections.abc import Callable
from pathlib import Path

DECK_TARGET = "x86_64-steamdeck-linux-gnu"
BRIDGE_FIELDS = {"version", "license", "source", "recipe", "targets"}
ASSET_FIELDS = {"url", "sha256", "size", "format", "files"}
FILE_FIELDS = {"member", "sha256", "size"}
SOURCE_FIELDS = {"url", "sha256", "size", "name", "license_member"}
FetchAsset = Callable[[dict, Path], Path]


def _require_digest(value: object, label: str) -> None:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise SystemExit(f"{label} must be a lowercase SHA-256 digest")


def _require_size(value: object, label: str) -> None:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise SystemExit(f"{label} must be a positive integer")


def _validate_file(value: object, label: str) -> None:
    if not isinstance(value, dict) or set(value) != FILE_FIELDS:
        raise SystemExit(f"{label} has missing or unknown fields")
    member = value["member"]
    if (
        not isinstance(member, str)
        or not member.startswith("usr/")
        or ".." in Path(member).parts
    ):
        raise SystemExit(f"{label}.member is invalid")
    _require_digest(value["sha256"], f"{label}.sha256")
    _require_size(value["size"], f"{label}.size")


def validate_runtime_manifest(manifest: dict) -> None:
    bridge = manifest.get("linux_bridge")
    if not isinstance(bridge, dict) or set(bridge) != BRIDGE_FIELDS:
        raise SystemExit("Linux bridge definition has missing or unknown fields")
    if bridge["version"] != "1.22.1" or bridge["license"] != "LGPL-2.1-or-later":
        raise SystemExit("Linux bridge version or license is unsupported")
    source = bridge["source"]
    if not isinstance(source, dict) or set(source) != SOURCE_FIELDS:
        raise SystemExit("Linux bridge source has missing or unknown fields")
    if source["url"] != (
        "https://download.libguestfs.org/libnbd/1.22-stable/"
        "libnbd-1.22.1.tar.gz"
    ):
        raise SystemExit("Linux bridge source URL is not the reviewed upstream archive")
    if source["name"] != "libnbd-1.22.1.tar.gz" or source["license_member"] != (
        "libnbd-1.22.1/COPYING.LIB"
    ):
        raise SystemExit("Linux bridge source metadata is invalid")
    _require_digest(source["sha256"], "linux_bridge.source.sha256")
    _require_size(source["size"], "linux_bridge.source.size")
    recipe = bridge["recipe"]
    if not isinstance(recipe, dict) or set(recipe) != {
        "url", "sha256", "size", "name"
    }:
        raise SystemExit("Linux bridge build recipe has missing or unknown fields")
    if recipe["url"] != (
        "https://gitlab.archlinux.org/archlinux/packaging/packages/libnbd/-/raw/"
        "a5a1d2927c4fd6b69a8b0cfd46b396232f5087be/PKGBUILD"
    ) or recipe["name"] != "libnbd-1.22.1-2-PKGBUILD":
        raise SystemExit("Linux bridge build recipe is not the reviewed commit")
    _require_digest(recipe["sha256"], "linux_bridge.recipe.sha256")
    _require_size(recipe["size"], "linux_bridge.recipe.size")

    targets = bridge["targets"]
    if not isinstance(targets, dict) or set(targets) != {DECK_TARGET}:
        raise SystemExit("Linux bridge must define only the reviewed Steam Deck target")
    asset = targets[DECK_TARGET]
    if not isinstance(asset, dict) or set(asset) != ASSET_FIELDS:
        raise SystemExit("Steam Deck bridge asset has missing or unknown fields")
    if asset["url"] != (
        "https://steamdeck-packages.steamos.cloud/archlinux-mirror/"
        "extra-3.8.1x/os/x86_64/libnbd-1.22.1-2-x86_64.pkg.tar.zst"
    ) or asset["format"] != "arch-package-zstd":
        raise SystemExit("Steam Deck bridge asset is not the reviewed package")
    _require_digest(asset["sha256"], "linux_bridge target sha256")
    _require_size(asset["size"], "linux_bridge target size")
    files = asset["files"]
    if not isinstance(files, dict) or set(files) != {"executable", "library"}:
        raise SystemExit("Steam Deck bridge files are incomplete")
    _validate_file(files["executable"], "linux_bridge executable")
    _validate_file(files["library"], "linux_bridge library")
    if files["executable"]["member"] != "usr/bin/nbdfuse" or (
        files["library"]["member"] != "usr/lib/libnbd.so.0.0.0"
    ):
        raise SystemExit("Steam Deck bridge package members are unsupported")


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _extract_member(archive: Path, member: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as output:
        completed = subprocess.run(
            ["tar", "-xOf", str(archive), member],
            stdout=output,
            stderr=subprocess.PIPE,
            check=False,
        )
    if completed.returncode != 0:
        destination.unlink(missing_ok=True)
        message = completed.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"could not extract {member}: {message}")


def _verify_file(path: Path, definition: dict, label: str) -> None:
    if path.stat().st_size != definition["size"] or (
        _digest(path) != definition["sha256"]
    ):
        path.unlink(missing_ok=True)
        raise SystemExit(f"{label} failed its independent integrity check")


def bundle(
    target: str,
    stage: Path,
    cache: Path,
    manifest: dict,
    fetch_asset: FetchAsset,
) -> bool:
    validate_runtime_manifest(manifest)
    if target != DECK_TARGET:
        return False
    bridge = manifest["linux_bridge"]
    asset = bridge["targets"][target]
    package = fetch_asset(asset, cache)
    outputs = {
        "executable": stage / "libexec" / "nbdfuse.bin",
        "library": stage / "runtime" / "bridge" / "lib" / "libnbd.so.0",
    }
    for label, destination in outputs.items():
        definition = asset["files"][label]
        _extract_member(package, definition["member"], destination)
        _verify_file(destination, definition, f"Steam Deck bridge {label}")
    outputs["executable"].chmod(0o755)

    source_definition = bridge["source"]
    source = fetch_asset(source_definition, cache)
    license_path = stage / "runtime" / "bridge" / "LICENSE.txt"
    _extract_member(source, source_definition["license_member"], license_path)
    recipe = bridge["recipe"]
    (stage / "runtime" / "bridge" / "SOURCE.md").write_text(
        "# Steam Deck disc bridge source\n\n"
        "This directory contains unmodified nbdfuse and libnbd files from "
        "the pinned SteamOS package. The exact corresponding libnbd source "
        "archive and Arch PKGBUILD are published beside the GDOX release.\n\n"
        f"Source: {source_definition['url']}\n\n"
        f"Source SHA-256: `{source_definition['sha256']}`\n\n"
        f"Build recipe: {recipe['url']}\n\n"
        f"PKGBUILD SHA-256: `{recipe['sha256']}`\n",
        encoding="utf-8",
    )
    (stage / "runtime" / "bridge" / "VERSION.json").write_text(
        json.dumps(bridge, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return True


def validate_stage(target: str, stage: Path, manifest: dict) -> None:
    if target != DECK_TARGET:
        return
    bridge = manifest["linux_bridge"]
    files = bridge["targets"][target]["files"]
    expected = {
        "executable": stage / "libexec" / "nbdfuse.bin",
        "library": stage / "runtime" / "bridge" / "lib" / "libnbd.so.0",
    }
    for label, path in expected.items():
        if not path.is_file():
            raise SystemExit(f"Steam Deck bridge {label} is missing")
        _verify_file(path, files[label], f"Steam Deck bridge {label}")
    for path in (
        stage / "libexec" / "nbdfuse",
        stage / "runtime" / "bridge" / "LICENSE.txt",
        stage / "runtime" / "bridge" / "SOURCE.md",
        stage / "runtime" / "bridge" / "VERSION.json",
    ):
        if not path.is_file():
            raise SystemExit(f"Steam Deck bridge file is missing: {path}")
    bridge_root = stage / "runtime" / "bridge"
    actual = {
        path.relative_to(bridge_root).as_posix()
        for path in bridge_root.rglob("*")
        if path.is_file() or path.is_symlink()
    }
    expected_bridge_files = {
        "LICENSE.txt",
        "SOURCE.md",
        "VERSION.json",
        "lib/libnbd.so.0",
    }
    if actual != expected_bridge_files:
        raise SystemExit(
            "Steam Deck bridge has missing or unexpected contents"
        )
