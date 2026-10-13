"""Validate and stage the reviewed Linux Dolphin distribution."""
from __future__ import annotations

import hashlib
import json
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINUX_TARGET = "x86_64-unknown-linux-gnu"
VERSION = "2609a-gdox1"
# The upstream identity is independent of the packaged executable identity.
UPSTREAM_COMMIT = "409881c34357d0f105fd473167d15ab0bd9c1628"
INVENTORY = ROOT / "packaging/dolphin/linux-files.json"


def identity(data: bytes) -> dict:
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def validate_identity(value: object, label: str) -> None:
    if not isinstance(value, dict) or set(value) != {"size", "sha256"}:
        raise SystemExit(f"{label}: invalid file identity")
    size, digest = value["size"], value["sha256"]
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        raise SystemExit(f"{label}: invalid byte length")
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise SystemExit(f"{label}: invalid SHA-256")


def inventory(manifest: dict) -> dict:
    pin = manifest["dolphin"]["inventory"]
    validate_identity(pin, "Dolphin inventory")
    data = INVENTORY.read_bytes()
    if identity(data) != pin:
        raise SystemExit("Dolphin file inventory differs from its reviewed identity")
    files = json.loads(data)
    if not isinstance(files, dict) or not files:
        raise SystemExit("Dolphin file inventory is empty")
    for name, definition in files.items():
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or name != path.as_posix():
            raise SystemExit("Dolphin inventory contains an unsafe path")
        validate_identity(definition, f"Dolphin {name}")
    if not {"dolphin", "bin/dolphin-emu", "bin/nbdfuse", "bin/qt.conf", "licenses/Dolphin-COPYING", "dependencies.json", "FILES.json"}.issubset(files):
        raise SystemExit("Dolphin inventory omits required runtime files")
    return files


def validate_runtime_manifest(manifest: dict) -> None:
    definition = manifest.get("dolphin")
    if not isinstance(definition, dict) or set(definition) != {
        "version", "upstream_commit", "license", "release_state", "source", "dependencies", "targets", "inventory"
    }:
        raise SystemExit("Dolphin definition has missing or unknown fields")
    if definition["version"] != VERSION or definition["upstream_commit"] != UPSTREAM_COMMIT or definition["license"] != "GPL-2.0-or-later":
        raise SystemExit("Dolphin source identity is not the reviewed release")
    if definition["release_state"] not in {"candidate-only", "published"}:
        raise SystemExit("Dolphin release state is invalid")
    if set(definition["targets"]) != {LINUX_TARGET}:
        raise SystemExit("Dolphin must define only the reviewed Linux runtime")
    assets = [definition["source"], definition["dependencies"], definition["targets"][LINUX_TARGET]]
    for asset in assets:
        if not isinstance(asset, dict) or set(asset) != {"name", "url", "size", "sha256"}:
            raise SystemExit("Dolphin asset has missing or unknown fields")
        validate_identity({key: asset[key] for key in ("size", "sha256")}, "Dolphin asset")
        if asset["size"] == 0:
            raise SystemExit("Dolphin asset is empty")
        if asset["name"] != Path(asset["name"]).name or not asset["name"].endswith(".tar.gz"):
            raise SystemExit("Dolphin asset name is invalid")
        if not asset["url"].startswith("https://github.com/korzewarrior/gdox/releases/download/runtime-") or not asset["url"].endswith("/" + asset["name"]):
            raise SystemExit("Dolphin asset must identify an immutable reviewed distribution")
    inventory(manifest)


def require_publishable(target: str, manifest: dict) -> None:
    validate_runtime_manifest(manifest)
    if target in {LINUX_TARGET, "x86_64-steamdeck-linux-gnu"} and manifest["dolphin"]["release_state"] != "published":
        raise SystemExit("Dolphin is still a validation candidate")


def validate_stage(target: str, stage: Path, manifest: dict) -> None:
    if target not in {LINUX_TARGET, "x86_64-steamdeck-linux-gnu"}:
        return
    root = stage / "runtime/dolphin"
    expected = inventory(manifest)
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if not p.is_dir()}
    if actual != set(expected):
        raise SystemExit("Dolphin has missing or unexpected runtime files")
    for name, pin in expected.items():
        path = root / name
        if path.is_symlink() or not path.is_file() or identity(path.read_bytes()) != pin:
            raise SystemExit(f"Dolphin runtime file differs: {name}")
    for name in ("dolphin", "bin/dolphin-emu", "bin/nbdfuse"):
        if not (root / name).stat().st_mode & 0o111:
            raise SystemExit(f"Dolphin executable is not executable: {name}")


def bundle(target: str, destination: Path, cache: Path, manifest: dict, fetch_asset) -> bool:
    if target != LINUX_TARGET:
        return False
    files = inventory(manifest)
    archive = fetch_asset(manifest["dolphin"]["targets"][target], cache)
    root = destination / "dolphin"
    root.mkdir()
    seen = set()
    with tarfile.open(archive, "r:gz") as source:
        for member in source:
            name = member.name.removeprefix("./")
            if name == "dolphin" and member.isdir():
                continue
            if not name.startswith("dolphin/"):
                raise SystemExit("Dolphin archive contains an unexpected root")
            relative = name.removeprefix("dolphin/")
            if member.isdir():
                if ".." in Path(relative).parts:
                    raise SystemExit("Dolphin archive contains an unsafe directory")
                continue
            if not member.isfile() or relative not in files or relative in seen or member.size != files[relative]["size"]:
                raise SystemExit("Dolphin archive contains an unexpected file")
            seen.add(relative)
            output = root / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            data = source.extractfile(member).read()
            if identity(data) != files[relative]:
                raise SystemExit(f"Dolphin archive file failed verification: {relative}")
            output.write_bytes(data)
            output.chmod(0o755 if member.mode & 0o111 else 0o644)
    if seen != set(files):
        raise SystemExit("Dolphin archive is incomplete")
    return True
