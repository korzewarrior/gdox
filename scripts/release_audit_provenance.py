"""Identify release content whose provenance grants narrow audit exemptions."""

from __future__ import annotations

import hashlib
import re
import tarfile
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from android_source_provenance import (
    ANDROID_SOURCE_COMPONENTS,
    SourceTreeEntry,
    canonical_tree_digest,
    expected_component_tree_digests,
    manifest_revision,
    normalized_source_mode,
    validate_component_path,
)
from fetch_runtime import load_manifest

ROOT = Path(__file__).resolve().parent.parent
ANDROID_SOURCE_ARCHIVE_PATTERN = re.compile(
    r"gdox-[0-9]+\.[0-9]+\.[0-9]+-android-corresponding-source\.tar\.gz"
)


RUNTIME_MANIFEST = load_manifest()
XENIA_RUNTIME_FILES = tuple(
    (
        f"runtime/xenia/{revision}/{asset['executable']}",
        asset["archive_name"],
        asset["executable"],
        asset["executable_size"],
        asset["executable_sha256"],
    )
    for revision, definition in RUNTIME_MANIFEST["xenia"]["revisions"].items()
    for asset in definition["targets"].values()
)
XEMU_BUILD_PATH_FILES = tuple(
    (
        file["member"],
        file["size"],
        file["sha256"],
    )
    for file in RUNTIME_MANIFEST["xemu"]["embedded_build_path_files"]
)
XEMU_PRIVACY_FILES = tuple(
    (
        file["member"],
        file["size"],
        file["sha256"],
    )
    for file in RUNTIME_MANIFEST["xemu"]["embedded_privacy_files"]
)
XEMU_KNOWN_TEST_KEY_MEMBER = (
    "runtime/xemu/AppDir/usr/lib/libgnutls.so.30"
)
XEMU_NOTICE_FILES = tuple(
    (
        file["member"],
        file["size"],
        file["sha256"],
    )
    for file in RUNTIME_MANIFEST["xemu"]["embedded_notice_files"]
)
THIRD_PARTY_NOTICE_FILES = (
    *XEMU_NOTICE_FILES,
    *(
        (
            f"runtime/xemu/licenses/{license['name']}",
            license["size"],
            license["sha256"],
        )
        for license in RUNTIME_MANIFEST["xemu"]["licenses"]
    ),
    (
        f"runtime/hdd/{RUNTIME_MANIFEST['hdd']['license']['name']}",
        RUNTIME_MANIFEST["hdd"]["license"]["size"],
        RUNTIME_MANIFEST["hdd"]["license"]["sha256"],
    ),
)


@dataclass(frozen=True)
class AndroidSourceValidation:
    components: frozenset[str]
    trusted_payloads: frozenset[tuple[str, int, str]]


@lru_cache(maxsize=16)
def cached_file_identity(
    path_text: str,
    size: int,
    modified_ns: int,
    changed_ns: int,
) -> tuple[int, str] | None:
    del modified_ns, changed_ns
    try:
        digest = hashlib.sha256()
        with Path(path_text).open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError:
        return None
    return size, digest.hexdigest()


def file_identity(path_text: str) -> tuple[int, str] | None:
    try:
        metadata = Path(path_text).stat()
    except OSError:
        return None
    return cached_file_identity(
        path_text,
        metadata.st_size,
        metadata.st_mtime_ns,
        metadata.st_ctime_ns,
    )


def asset_matches_file(path: str, asset: dict) -> bool:
    return file_identity(path) == (asset["size"], asset["sha256"])


def stream_sha256(source) -> str:
    digest = hashlib.sha256()
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


@lru_cache(maxsize=8)
def cached_android_source_validation(
    archive_text: str,
    archive_size: int,
    archive_sha256: str,
) -> AndroidSourceValidation | None:
    del archive_size, archive_sha256
    archive_path = Path(archive_text)
    archive_name = archive_path.name
    root = archive_name.removesuffix(".tar.gz")
    manifest_name = f"{root}/SOURCE_MANIFEST.txt"
    entries: dict[str, list[SourceTreeEntry]] = {
        component: [] for component in ANDROID_SOURCE_COMPONENTS
    }
    seen_paths: set[tuple[str, str]] = set()
    trusted_payloads: list[tuple[str, str, int, str]] = []
    try:
        with tarfile.open(archive_path, "r:*") as archive:
            source_prefix = f"{root}/source/"
            manifest_members = []
            for member in archive.getmembers():
                if member.name == manifest_name:
                    manifest_members.append(member)
                    continue
                if not member.name.startswith(source_prefix):
                    return None
                relative = member.name.removeprefix(source_prefix)
                component, separator, component_path = relative.partition("/")
                if (
                    not separator
                    or component not in ANDROID_SOURCE_COMPONENTS
                ):
                    return None
                validate_component_path(component_path)
                identity = (component, component_path)
                if identity in seen_paths:
                    return None
                seen_paths.add(identity)
                member_name_data = member.name.encode("utf-8", errors="strict")
                trusted_payloads.append(
                    (
                        component,
                        f"{member.name} member name",
                        len(member_name_data),
                        hashlib.sha256(member_name_data).hexdigest(),
                    )
                )
                for metadata_index, (key, value) in enumerate(
                    sorted(member.pax_headers.items()),
                    2,
                ):
                    if not (
                        (key == "path" and value == member.name)
                        or (
                            key == "linkpath"
                            and member.type == tarfile.SYMTYPE
                            and value == member.linkname
                        )
                    ):
                        continue
                    metadata = f"{key}={value}".encode("utf-8", errors="strict")
                    trusted_payloads.append(
                        (
                            component,
                            f"{member.name} metadata {metadata_index}",
                            len(metadata),
                            hashlib.sha256(metadata).hexdigest(),
                        )
                    )
                if member.type == tarfile.REGTYPE:
                    if member.mode != normalized_source_mode("file", member.mode):
                        return None
                    source = archive.extractfile(member)
                    if source is None:
                        return None
                    content_digest = stream_sha256(source)
                    entries[component].append(
                        SourceTreeEntry(
                            component_path,
                            "file",
                            member.mode,
                            size=member.size,
                            content_sha256=content_digest,
                        )
                    )
                    trusted_payloads.append(
                        (component, member.name, member.size, content_digest)
                    )
                elif member.type == tarfile.SYMTYPE:
                    if member.mode != normalized_source_mode(
                        "symlink",
                        member.mode,
                    ):
                        return None
                    entries[component].append(
                        SourceTreeEntry(
                            component_path,
                            "symlink",
                            member.mode,
                            symlink_target=member.linkname,
                        )
                    )
                    target = member.linkname.encode("utf-8", errors="strict")
                    trusted_payloads.append(
                        (
                            component,
                            f"{member.name} link target",
                            len(target),
                            hashlib.sha256(target).hexdigest(),
                        )
                    )
                else:
                    return None
            if (
                len(manifest_members) != 1
                or manifest_members[0].type != tarfile.REGTYPE
                or manifest_members[0].mode != 0o644
            ):
                return None
            source = archive.extractfile(manifest_members[0])
            if source is None:
                return None
            lines = source.read().decode("utf-8").splitlines()
    except (
        OSError,
        RuntimeError,
        UnicodeDecodeError,
        UnicodeEncodeError,
        tarfile.TarError,
    ):
        return None
    if manifest_revision(lines) is None:
        return None
    components = [line.partition(" ")[0] for line in lines[1:]]
    component_set = frozenset(components)
    if (
        len(components) != len(component_set)
        or component_set != ANDROID_SOURCE_COMPONENTS
        or any(not component_entries for component_entries in entries.values())
    ):
        return None
    try:
        actual = {
            component: canonical_tree_digest(component_entries)
            for component, component_entries in entries.items()
        }
        expected = expected_component_tree_digests()
    except RuntimeError:
        return None
    if any(actual[component] != digest for component, digest in expected.items()):
        return None
    return AndroidSourceValidation(
        component_set,
        frozenset(
            (label, size, digest)
            for component, label, size, digest in trusted_payloads
            if component.casefold() != "gdox"
        ),
    )


def android_source_validation(
    archive_text: str,
) -> AndroidSourceValidation | None:
    identity = file_identity(archive_text)
    if identity is None:
        return None
    size, digest = identity
    return cached_android_source_validation(archive_text, size, digest)


def android_source_components(archive_text: str) -> frozenset[str]:
    validation = android_source_validation(archive_text)
    return validation.components if validation is not None else frozenset()


def is_valid_android_source_archive(path: Path) -> bool:
    return (
        ANDROID_SOURCE_ARCHIVE_PATTERN.fullmatch(path.name) is not None
        and android_source_components(str(path)) == ANDROID_SOURCE_COMPONENTS
    )


def is_android_core_patch(label: str) -> bool:
    normalized = label.replace("\\", "/")
    android_patch = "android/emulator/patches/0001-android-core.patch"
    if "!" not in normalized:
        try:
            return Path(normalized).resolve() == (ROOT / android_patch).resolve()
        except OSError:
            return False
    archive, member = normalized.split("!", 1)
    member = member.removeprefix("./")
    archive_name = Path(archive).name
    android_source = ANDROID_SOURCE_ARCHIVE_PATTERN.fullmatch(archive_name)
    if android_source is not None:
        root = archive_name.removesuffix(".tar.gz")
        return member == f"{root}/source/gdox/{android_patch}"
    source_archive = re.fullmatch(
        r"gdox-[0-9]+\.[0-9]+\.[0-9]+-source\.tar\.gz",
        archive_name,
    )
    if source_archive is None:
        return False
    root = archive_name.removesuffix(".tar.gz")
    return member == f"{root}/{android_patch}"


def is_pinned_source_member(label: str) -> bool:
    normalized = label.replace("\\", "/")
    if "!" not in normalized:
        return False
    archive, _ = normalized.split("!", 1)
    archive_name = Path(archive).name
    assets = (
        RUNTIME_MANIFEST["xemu"]["source"],
        RUNTIME_MANIFEST["linux_bridge"]["source"],
    )
    return any(
        archive_name == asset["name"] and asset_matches_file(archive, asset)
        for asset in assets
    )


def is_third_party_source(label: str, data: bytes) -> bool:
    normalized = label.replace("\\", "/")
    bridge = RUNTIME_MANIFEST["linux_bridge"]
    recipe = bridge["recipe"]
    if (
        normalized.endswith(f"/{recipe['name']}")
        and len(data) == recipe["size"]
        and hashlib.sha256(data).hexdigest() == recipe["sha256"]
    ):
        return True
    if "!" not in normalized:
        return False
    archive, member = normalized.split("!", 1)
    archive_name = Path(archive).name
    android_source = ANDROID_SOURCE_ARCHIVE_PATTERN.fullmatch(archive_name)
    if android_source is not None:
        validation = android_source_validation(archive)
        if validation is not None:
            digest = hashlib.sha256(data).hexdigest()
            return (member, len(data), digest) in validation.trusted_payloads
        return False
    member = member.removeprefix("./")
    xemu_source = RUNTIME_MANIFEST["xemu"]["source"]
    if (
        archive_name == xemu_source["name"]
        and asset_matches_file(archive, xemu_source)
    ):
        return True
    bridge_source = bridge["source"]
    return (
        archive_name == bridge_source["name"]
        and asset_matches_file(archive, bridge_source)
        and member.startswith("libnbd-1.22.1/")
    )


def is_verified_xemu_build_path_file(label: str, data: bytes) -> bool:
    normalized = label.replace("\\", "/")
    candidates = tuple(
        (size, expected_digest)
        for member, size, expected_digest in XEMU_BUILD_PATH_FILES
        if normalized == member or normalized.endswith(f"/{member}")
    )
    if not candidates:
        return False
    digest = hashlib.sha256(data).hexdigest()
    return any(
        len(data) == size and digest == expected_digest
        for size, expected_digest in candidates
    )


def is_verified_xemu_privacy_file(label: str, data: bytes) -> bool:
    normalized = label.replace("\\", "/")
    candidates = tuple(
        (size, expected_digest)
        for member, size, expected_digest in XEMU_PRIVACY_FILES
        if normalized == member or normalized.endswith(f"/{member}")
    )
    if not candidates:
        return False
    digest = hashlib.sha256(data).hexdigest()
    return any(
        len(data) == size and digest == expected_digest
        for size, expected_digest in candidates
    )


def is_verified_xemu_known_test_key_file(label: str, data: bytes) -> bool:
    normalized = label.replace("\\", "/")
    if not (
        normalized == XEMU_KNOWN_TEST_KEY_MEMBER
        or normalized.endswith(f"/{XEMU_KNOWN_TEST_KEY_MEMBER}")
    ):
        return False
    return is_verified_xemu_privacy_file(normalized, data)


def is_verified_xenia_runtime_file(label: str, data: bytes) -> bool:
    """Match a manifest-pinned executable at its staged or archive member."""
    normalized = label.replace("\\", "/")
    digest = hashlib.sha256(data).hexdigest()
    for (
        staged_member,
        archive_name,
        archive_member,
        expected_size,
        expected_digest,
    ) in XENIA_RUNTIME_FILES:
        if len(data) != expected_size or digest != expected_digest:
            continue
        if normalized == staged_member or normalized.endswith(
            f"/{staged_member}"
        ):
            return True
        if "!" in normalized:
            archive, member = normalized.split("!", 1)
            if (
                Path(archive).name == archive_name
                and member.removeprefix("./") == archive_member
            ):
                return True
    return False


def is_verified_third_party_notice(label: str, data: bytes) -> bool:
    normalized = label.replace("\\", "/")
    candidates = tuple(
        (size, expected_digest)
        for member, size, expected_digest in THIRD_PARTY_NOTICE_FILES
        if normalized == member or normalized.endswith(f"/{member}")
    )
    if not candidates:
        return False
    digest = hashlib.sha256(data).hexdigest()
    return any(
        len(data) == size
        and digest == expected_digest
        for size, expected_digest in candidates
    )
