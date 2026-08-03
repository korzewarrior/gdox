"""Define and validate Android corresponding-source provenance."""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
ANDROID_SOURCE_COMPONENT_ORDER = (
    "gdox",
    "xemu",
    "sdl2",
    "libusb",
    "glslang",
    "spirv_reflect",
    "tomlplusplus",
    "vma",
    "volk",
    "glib",
    "libffi",
    "proxy-libintl",
)
ANDROID_SOURCE_COMPONENTS = frozenset(ANDROID_SOURCE_COMPONENT_ORDER)
ANDROID_NATIVE_GIT_REVISION_KEYS = {
    "glslang": "GLSLANG_REVISION",
    "spirv_reflect": "SPIRV_REFLECT_REVISION",
    "tomlplusplus": "TOMLPLUSPLUS_REVISION",
    "vma": "VMA_REVISION",
    "volk": "VOLK_REVISION",
}
ANDROID_GLIB_GIT_SUBPROJECTS = {
    "libffi": (
        Path("subprojects/libffi"),
        "GLIB_LIBFFI_REVISION",
    ),
    "proxy-libintl": (
        Path("subprojects/proxy-libintl"),
        "GLIB_PROXYLIBINTL_REVISION",
    ),
}
ANDROID_COMPONENT_TREE_KEYS = {
    "xemu": "XEMU_SOURCE_TREE_SHA256",
    "sdl2": "SDL2_SOURCE_TREE_SHA256",
    "libusb": "LIBUSB_SOURCE_TREE_SHA256",
    "glslang": "GLSLANG_SOURCE_TREE_SHA256",
    "spirv_reflect": "SPIRV_REFLECT_SOURCE_TREE_SHA256",
    "tomlplusplus": "TOMLPLUSPLUS_SOURCE_TREE_SHA256",
    "vma": "VMA_SOURCE_TREE_SHA256",
    "volk": "VOLK_SOURCE_TREE_SHA256",
    "glib": "GLIB_SOURCE_TREE_SHA256",
    "libffi": "GLIB_LIBFFI_SOURCE_TREE_SHA256",
    "proxy-libintl": "GLIB_PROXYLIBINTL_SOURCE_TREE_SHA256",
}
TREE_DIGEST_HEADER = b"GDOX Android source tree v1\0"


@dataclass(frozen=True)
class SourceTreeEntry:
    path: str
    kind: str
    mode: int
    size: int = 0
    content_sha256: str = ""
    symlink_target: str = ""


def normalized_source_mode(kind: str, mode: int) -> int:
    if kind == "file":
        return 0o755 if mode & 0o111 else 0o644
    if kind == "symlink":
        return 0o777
    raise RuntimeError(f"unsupported Android source member type: {kind}")


def validate_component_path(path: str) -> bytes:
    normalized = PurePosixPath(path)
    if (
        not path
        or "\\" in path
        or normalized.is_absolute()
        or normalized.as_posix() != path
        or any(part in {"", ".", ".."} for part in normalized.parts)
    ):
        raise RuntimeError(f"invalid Android component source path: {path}")
    try:
        return path.encode("utf-8", errors="strict")
    except UnicodeEncodeError as error:
        raise RuntimeError(
            f"Android component source path is not UTF-8 encodable: {path}"
        ) from error


def update_digest_field(digest, value: bytes) -> None:
    digest.update(len(value).to_bytes(8, "big"))
    digest.update(value)


def canonical_tree_digest(entries: list[SourceTreeEntry]) -> str:
    """Hash a sorted, length-framed tree using normalized portable metadata.

    Regular-file records bind size and SHA-256 of the complete byte stream;
    symlink records bind the UTF-8 target. File modes collapse to 0644/0755,
    and symlink modes collapse to 0777.
    """
    prepared: list[tuple[bytes, SourceTreeEntry, int]] = []
    for entry in entries:
        path = validate_component_path(entry.path)
        mode = normalized_source_mode(entry.kind, entry.mode)
        if entry.kind == "file" and (
            entry.size < 0
            or re.fullmatch(r"[0-9a-f]{64}", entry.content_sha256) is None
            or entry.symlink_target
        ):
            raise RuntimeError(f"invalid regular-file identity for {entry.path}")
        if entry.kind == "symlink" and (
            entry.size != 0
            or entry.content_sha256
            or not entry.symlink_target
        ):
            raise RuntimeError(f"invalid symlink identity for {entry.path}")
        if "\0" in entry.symlink_target:
            raise RuntimeError(f"invalid symlink target for {entry.path}")
        prepared.append((path, entry, mode))
    prepared.sort(key=lambda item: item[0])

    digest = hashlib.sha256(TREE_DIGEST_HEADER)
    previous: bytes | None = None
    for path, entry, mode in prepared:
        if path == previous:
            raise RuntimeError(f"duplicate Android component path: {entry.path}")
        previous = path
        update_digest_field(digest, path)
        digest.update(b"F" if entry.kind == "file" else b"L")
        digest.update(mode.to_bytes(2, "big"))
        if entry.kind == "file":
            digest.update(entry.size.to_bytes(8, "big"))
            digest.update(bytes.fromhex(entry.content_sha256))
        else:
            update_digest_field(
                digest,
                entry.symlink_target.encode("utf-8", errors="strict"),
            )
    return digest.hexdigest()


def dependency_lock() -> dict[str, str]:
    values: dict[str, str] = {}
    path = ROOT / "android" / "dependencies.lock"
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(),
        1,
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if (
            not separator
            or re.fullmatch(r"[A-Z][A-Z0-9_]*", key) is None
            or not value
        ):
            raise RuntimeError(
                f"invalid Android dependency lock entry at {path}:{line_number}"
            )
        if key in values:
            raise RuntimeError(f"duplicate Android dependency lock key: {key}")
        values[key] = value
    return values


def locked_value(name: str) -> str:
    try:
        return dependency_lock()[name]
    except KeyError:
        raise RuntimeError(
            f"{name} is missing from android/dependencies.lock"
        ) from None


def expected_component_tree_digests(
    locked: dict[str, str] | None = None,
) -> dict[str, str]:
    values = dependency_lock() if locked is None else locked
    expected: dict[str, str] = {}
    for component, key in ANDROID_COMPONENT_TREE_KEYS.items():
        try:
            digest = values[key]
        except KeyError:
            raise RuntimeError(
                f"{key} is missing from android/dependencies.lock"
            ) from None
        if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise RuntimeError(
                f"{key} has an invalid value in android/dependencies.lock"
            )
        expected[component] = digest
    return expected


def patch_digest(patch_root: Path) -> str:
    digest = hashlib.sha256()
    series = patch_root / "series"
    for line in series.read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        patch = patch_root / name
        if not patch.is_file():
            raise RuntimeError(f"patch listed in series is missing: {patch}")
        digest.update(name.encode())
        digest.update(b"\0")
        digest.update(patch.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def canonical_manifest_lines(gdox_revision: str) -> tuple[str, ...]:
    if re.fullmatch(r"[0-9a-f]{40}", gdox_revision) is None:
        raise RuntimeError("GDOX source revision is not a full Git commit ID")
    locked = dependency_lock()

    def require(name: str, pattern: str) -> str:
        try:
            value = locked[name]
        except KeyError:
            raise RuntimeError(
                f"{name} is missing from android/dependencies.lock"
            ) from None
        if re.fullmatch(pattern, value) is None:
            raise RuntimeError(
                f"{name} has an invalid value in android/dependencies.lock"
            )
        return value

    revisions = {
        key: require(key, r"[0-9a-f]{40}")
        for key in (
            "XEMU_ANDROID_BASE_REVISION",
            "SDL2_REVISION",
            "LIBUSB_REVISION",
            *ANDROID_NATIVE_GIT_REVISION_KEYS.values(),
            *(key for _, key in ANDROID_GLIB_GIT_SUBPROJECTS.values()),
        )
    }
    glib_version = require("GLIB_VERSION", r"[0-9]+\.[0-9]+\.[0-9]+")
    glib_digest = require("GLIB_SOURCE_SHA256", r"[0-9a-f]{64}")
    tree_digests = expected_component_tree_digests(locked)
    lines = [
        "GDOX Android corresponding source",
        f"gdox {gdox_revision}",
        (
            f"xemu {revisions['XEMU_ANDROID_BASE_REVISION']} + patch series "
            f"{patch_digest(ROOT / 'android' / 'emulator' / 'patches')} "
            f"tree sha256 {tree_digests['xemu']}"
        ),
        (
            f"sdl2 {revisions['SDL2_REVISION']} + patch series "
            f"{patch_digest(ROOT / 'android' / 'emulator' / 'sdl2' / 'patches')} "
            f"tree sha256 {tree_digests['sdl2']}"
        ),
        (
            f"libusb {revisions['LIBUSB_REVISION']} tree sha256 "
            f"{tree_digests['libusb']}"
        ),
    ]
    lines.extend(
        (
            f"{component} {revisions[key]} tree sha256 "
            f"{tree_digests[component]}"
        )
        for component, key in ANDROID_NATIVE_GIT_REVISION_KEYS.items()
    )
    lines.extend(
        (
            (
                f"glib {glib_version} sha256 {glib_digest} tree sha256 "
                f"{tree_digests['glib']}"
            ),
            (
                f"libffi {revisions['GLIB_LIBFFI_REVISION']} tree sha256 "
                f"{tree_digests['libffi']}"
            ),
            (
                "proxy-libintl "
                f"{revisions['GLIB_PROXYLIBINTL_REVISION']} tree sha256 "
                f"{tree_digests['proxy-libintl']}"
            ),
        )
    )
    return tuple(lines)


def manifest_revision(lines: list[str]) -> str | None:
    if len(lines) != len(ANDROID_SOURCE_COMPONENT_ORDER) + 1:
        return None
    match = re.fullmatch(r"gdox ([0-9a-f]{40})", lines[1])
    if match is None:
        return None
    revision = match.group(1)
    try:
        expected = canonical_manifest_lines(revision)
    except (KeyError, OSError, RuntimeError):
        return None
    return revision if tuple(lines) == expected else None
