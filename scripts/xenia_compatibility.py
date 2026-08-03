#!/usr/bin/env python3
"""Load and validate the reviewed Xenia compatibility policy."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = ROOT / "packaging" / "xenia-compatibility.json"
SCHEMA = "gdox.xenia-compatibility/v4"

_REVISION = re.compile(r"[0-9a-f]{9,40}\Z")
_IDENTIFIER = re.compile(r"[0-9A-F]{8}\Z")
_MODULE = re.compile(r"[A-Za-z0-9_.-]{1,95}\Z")

_RUNTIME_KEYS = {"revision"}
_POLICY_KEYS = {"revision", "launch_module", "patch_set", "settings"}
_TITLE_KEYS = {
    "title_id",
    "media_id",
    "disc_number",
    "disc_count",
    *_POLICY_KEYS,
}
_SETTINGS_KEYS = {
    "d3d12_allow_variable_refresh_rate_and_tearing",
    "vsync",
    "apu_max_queued_frames",
    "gpu_allow_invalid_fetch_constants",
    "occlusion_query",
    "occlusion_query_saturation",
    "readback_resolve",
    "protect_zero",
    "xma_decoder",
    "use_dedicated_xma_thread",
    "async_shader_compilation",
    "use_handheld_custom_resolution",
}

_PATCH_SETS = {"none", "mass-effect-world-rendering-v1"}
_OCCLUSION_SATURATION_SCALE = 10_000


class CompatibilityError(ValueError):
    """The compatibility manifest is malformed or internally inconsistent."""


@dataclass(frozen=True)
class XeniaRuntime:
    revision: str


@dataclass(frozen=True)
class XeniaSettings:
    allow_tearing: bool
    vsync: bool
    max_queued_frames: int
    allow_invalid_fetch_constants: bool
    occlusion_query: str
    occlusion_query_saturation_basis_points: int
    readback_resolve: str
    protect_zero: bool
    xma_decoder: str
    use_dedicated_xma_thread: bool
    async_shader_compilation: bool
    use_handheld_custom_resolution: bool


@dataclass(frozen=True)
class XeniaPolicy:
    revision: str
    launch_module: str
    patch_set: str
    settings: XeniaSettings


@dataclass(frozen=True)
class XeniaTitlePolicy(XeniaPolicy):
    title_id: int
    media_id: int
    disc_number: int
    disc_count: int


@dataclass(frozen=True)
class XeniaCompatibility:
    runtimes: tuple[XeniaRuntime, ...]
    default: XeniaPolicy
    titles: tuple[XeniaTitlePolicy, ...]

    def runtime_by_revision(self, revision: str) -> XeniaRuntime:
        for runtime in self.runtimes:
            if runtime.revision == revision:
                return runtime
        raise KeyError(revision)


def _fail(path: str, message: str) -> NoReturn:
    raise CompatibilityError(f"{path}: {message}")


def _reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise CompatibilityError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _object(value: object, path: str) -> dict[str, object]:
    if not isinstance(value, dict):
        _fail(path, "must be an object")
    return value


def _array(value: object, path: str) -> list[object]:
    if not isinstance(value, list):
        _fail(path, "must be an array")
    return value


def _exact_keys(value: dict[str, object], expected: set[str], path: str) -> None:
    missing = sorted(expected - value.keys())
    unknown = sorted(value.keys() - expected)
    if missing:
        _fail(path, f"missing field(s): {', '.join(missing)}")
    if unknown:
        _fail(path, f"unknown field(s): {', '.join(unknown)}")


def _string(value: object, path: str) -> str:
    if not isinstance(value, str):
        _fail(path, "must be a string")
    return value


def _boolean(value: object, path: str) -> bool:
    if not isinstance(value, bool):
        _fail(path, "must be a boolean")
    return value


def _byte(value: object, path: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        _fail(path, "must be an integer")
    if value < 1 or value > 255:
        _fail(path, "must be between 1 and 255")
    return value


def _positive_u64(value: object, path: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        _fail(path, "must be an integer")
    if value < 1 or value > 0xFFFFFFFFFFFFFFFF:
        _fail(path, "must be between 1 and 18446744073709551615")
    return value


def _occlusion_saturation(value: object, path: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, Decimal)):
        _fail(path, "must be a decimal number")
    try:
        decimal = Decimal(value)
    except (InvalidOperation, ValueError):
        _fail(path, "must be a decimal number")
    if not decimal.is_finite() or decimal <= 0 or decimal > 1:
        _fail(path, "must be greater than 0.0 and at most 1.0")
    scaled = decimal * _OCCLUSION_SATURATION_SCALE
    if scaled != scaled.to_integral_value():
        _fail(path, "must have at most four fractional decimal places")
    return int(scaled)


def _matching(value: object, pattern: re.Pattern[str], path: str) -> str:
    result = _string(value, path)
    if pattern.fullmatch(result) is None:
        _fail(path, "has an invalid format")
    return result


def _launch_module(value: object, path: str) -> str:
    result = _matching(value, _MODULE, path)
    if result in {".", ".."} or not result.lower().endswith(".xex"):
        _fail(path, "must be a safe XEX file name")
    return result


def _settings(value: object, path: str) -> XeniaSettings:
    fields = _object(value, path)
    _exact_keys(fields, _SETTINGS_KEYS, path)
    occlusion = _string(fields["occlusion_query"], f"{path}.occlusion_query")
    if occlusion not in {"default", "strict", "fast-alt"}:
        _fail(
            f"{path}.occlusion_query",
            "must be default, strict, or fast-alt",
        )
    readback = _string(fields["readback_resolve"], f"{path}.readback_resolve")
    if readback not in {"none", "fast", "full"}:
        _fail(f"{path}.readback_resolve", "must be none, fast, or full")
    xma_decoder = _string(fields["xma_decoder"], f"{path}.xma_decoder")
    if xma_decoder not in {"old", "new"}:
        _fail(f"{path}.xma_decoder", "must be old or new")
    max_queued_frames = _positive_u64(
        fields["apu_max_queued_frames"], f"{path}.apu_max_queued_frames"
    )
    if max_queued_frames < 4 or max_queued_frames > 64:
        _fail(f"{path}.apu_max_queued_frames", "must be between 4 and 64")
    return XeniaSettings(
        allow_tearing=_boolean(
            fields["d3d12_allow_variable_refresh_rate_and_tearing"],
            f"{path}.d3d12_allow_variable_refresh_rate_and_tearing",
        ),
        vsync=_boolean(fields["vsync"], f"{path}.vsync"),
        max_queued_frames=max_queued_frames,
        allow_invalid_fetch_constants=_boolean(
            fields["gpu_allow_invalid_fetch_constants"],
            f"{path}.gpu_allow_invalid_fetch_constants",
        ),
        occlusion_query=occlusion,
        occlusion_query_saturation_basis_points=_occlusion_saturation(
            fields["occlusion_query_saturation"],
            f"{path}.occlusion_query_saturation",
        ),
        readback_resolve=readback,
        protect_zero=_boolean(fields["protect_zero"], f"{path}.protect_zero"),
        xma_decoder=xma_decoder,
        use_dedicated_xma_thread=_boolean(
            fields["use_dedicated_xma_thread"],
            f"{path}.use_dedicated_xma_thread",
        ),
        async_shader_compilation=_boolean(
            fields["async_shader_compilation"],
            f"{path}.async_shader_compilation",
        ),
        use_handheld_custom_resolution=_boolean(
            fields["use_handheld_custom_resolution"],
            f"{path}.use_handheld_custom_resolution",
        ),
    )


def _policy(value: object, path: str) -> XeniaPolicy:
    fields = _object(value, path)
    _exact_keys(fields, _POLICY_KEYS, path)
    patch_set = _string(fields["patch_set"], f"{path}.patch_set")
    if patch_set not in _PATCH_SETS:
        _fail(
            f"{path}.patch_set",
            "must be none or mass-effect-world-rendering-v1",
        )
    return XeniaPolicy(
        revision=_matching(fields["revision"], _REVISION, f"{path}.revision"),
        launch_module=_launch_module(fields["launch_module"], f"{path}.launch_module"),
        patch_set=patch_set,
        settings=_settings(fields["settings"], f"{path}.settings"),
    )


def load_compatibility_manifest(path: Path = DEFAULT_MANIFEST) -> XeniaCompatibility:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
            parse_float=Decimal,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise CompatibilityError(f"{path}: {error}") from error

    root = _object(document, "manifest")
    _exact_keys(root, {"schema", "runtimes", "default", "titles"}, "manifest")
    if root["schema"] != SCHEMA:
        _fail("manifest.schema", f"must be {SCHEMA}")

    runtime_values = _array(root["runtimes"], "manifest.runtimes")
    if not runtime_values:
        _fail("manifest.runtimes", "must not be empty")
    runtimes: list[XeniaRuntime] = []
    revisions: set[str] = set()
    for index, value in enumerate(runtime_values):
        item_path = f"manifest.runtimes[{index}]"
        fields = _object(value, item_path)
        _exact_keys(fields, _RUNTIME_KEYS, item_path)
        runtime = XeniaRuntime(
            revision=_matching(fields["revision"], _REVISION, f"{item_path}.revision"),
        )
        if runtime.revision in revisions:
            _fail(f"{item_path}.revision", "duplicates another runtime")
        revisions.add(runtime.revision)
        runtimes.append(runtime)

    default = _policy(root["default"], "manifest.default")
    if default.revision not in revisions:
        _fail("manifest.default.revision", "does not identify a declared runtime")

    title_values = _array(root["titles"], "manifest.titles")
    if not title_values:
        _fail("manifest.titles", "must not be empty")
    titles: list[XeniaTitlePolicy] = []
    identities: set[tuple[int, int, int, int]] = set()
    for index, value in enumerate(title_values):
        item_path = f"manifest.titles[{index}]"
        fields = _object(value, item_path)
        _exact_keys(fields, _TITLE_KEYS, item_path)
        base = _policy(
            {key: fields[key] for key in _POLICY_KEYS},
            item_path,
        )
        disc_number = _byte(fields["disc_number"], f"{item_path}.disc_number")
        disc_count = _byte(fields["disc_count"], f"{item_path}.disc_count")
        if disc_number > disc_count:
            _fail(f"{item_path}.disc_number", "must not exceed disc_count")
        title = XeniaTitlePolicy(
            revision=base.revision,
            launch_module=base.launch_module,
            patch_set=base.patch_set,
            settings=base.settings,
            title_id=int(
                _matching(fields["title_id"], _IDENTIFIER, f"{item_path}.title_id"),
                16,
            ),
            media_id=int(
                _matching(fields["media_id"], _IDENTIFIER, f"{item_path}.media_id"),
                16,
            ),
            disc_number=disc_number,
            disc_count=disc_count,
        )
        identity = (
            title.title_id,
            title.media_id,
            title.disc_number,
            title.disc_count,
        )
        if identity in identities:
            _fail(item_path, "duplicates another title identity")
        if title.revision not in revisions:
            _fail(f"{item_path}.revision", "does not identify a declared runtime")
        identities.add(identity)
        titles.append(title)

    return XeniaCompatibility(
        runtimes=tuple(sorted(runtimes, key=lambda item: item.revision)),
        default=default,
        titles=tuple(
            sorted(
                titles,
                key=lambda item: (
                    item.title_id,
                    item.media_id,
                    item.disc_number,
                    item.disc_count,
                ),
            )
        ),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()
    try:
        compatibility = load_compatibility_manifest(args.manifest)
    except CompatibilityError as error:
        parser.exit(1, f"{error}\n")
    print(
        f"validated {len(compatibility.runtimes)} runtime(s) and "
        f"{len(compatibility.titles)} title policy entry(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
