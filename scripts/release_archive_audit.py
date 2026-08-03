"""Inspect release archives for unsafe, ambiguous, or private contents."""

from __future__ import annotations

import shutil
import stat
import subprocess
import tarfile
import tempfile
import unicodedata
import zipfile
from collections.abc import Callable
from pathlib import Path, PurePosixPath

ContentInspector = Callable[[str, bytes, list[str]], None]
PathInspector = Callable[[str, str, list[str]], None]
WINDOWS_FORBIDDEN_CHARACTERS = frozenset('<>"|?*')
WINDOWS_RESERVED_NAMES = frozenset(
    {
        "AUX",
        "CON",
        "NUL",
        "PRN",
        *(f"COM{number}" for number in range(1, 10)),
        *(f"LPT{number}" for number in range(1, 10)),
    }
)


def is_unsafe_windows_component(component: str) -> bool:
    portable = component.rstrip(" .")
    return (
        not portable
        or portable != component
        or any(character in WINDOWS_FORBIDDEN_CHARACTERS for character in component)
        or portable.split(".", 1)[0].upper() in WINDOWS_RESERVED_NAMES
    )


def normalized_member_name(member_name: str) -> str:
    normalized = member_name.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    return normalized.rstrip("/")


def portable_member_key(member_name: str) -> str:
    path = PurePosixPath(normalized_member_name(member_name))
    return "/".join(
        unicodedata.normalize("NFC", component).casefold()
        for component in path.parts
    )


class ArchiveMemberTracker:
    """Reject duplicate or cross-platform-ambiguous archive names."""

    def __init__(self) -> None:
        self._exact_names: set[str] = set()
        self._portable_names: dict[str, tuple[str, bool]] = {}

    def inspect(
        self,
        label: str,
        member_name: str,
        is_directory: bool,
        findings: list[str],
    ) -> None:
        if member_name in self._exact_names:
            findings.append(f"{label}: duplicate archive member name")
        else:
            self._exact_names.add(member_name)

        key = portable_member_key(member_name)
        previous = self._portable_names.get(key)
        if previous is not None and previous != (member_name, is_directory):
            findings.append(f"{label}: portable archive member path collision")
        else:
            self._portable_names[key] = (member_name, is_directory)

        parts = key.split("/") if key else []
        for depth in range(1, len(parts)):
            ancestor = self._portable_names.get("/".join(parts[:depth]))
            if ancestor is not None and not ancestor[1]:
                findings.append(
                    f"{label}: archive member has a non-directory ancestor"
                )
                break
        if not is_directory:
            descendant_prefix = f"{key}/"
            if any(
                known.startswith(descendant_prefix)
                for known in self._portable_names
                if known != key
            ):
                findings.append(
                    f"{label}: non-directory archive member has descendants"
                )


def inspect_archive_member_path(
    label: str,
    member_name: str,
    findings: list[str],
) -> bool:
    normalized = member_name.replace("\\", "/")
    stripped = normalized_member_name(member_name)
    path = PurePosixPath(stripped)
    unsafe_windows_component = any(
        is_unsafe_windows_component(component)
        for component in path.parts
    )
    unsafe = (
        not stripped
        or "\\" in member_name
        or normalized.startswith("/")
        or ":" in normalized
        or any(
            ord(character) < 32 or ord(character) == 127
            for character in normalized
        )
        or unsafe_windows_component
        or ".." in path.parts
        or path.as_posix() != stripped
    )
    if unsafe:
        findings.append(f"{label}: unsafe archive member path")
    return not unsafe


def link_target_escapes(
    member_name: str,
    target: str,
    symbolic: bool,
) -> bool:
    normalized_target = target.replace("\\", "/")
    if (
        not normalized_target
        or "\\" in target
        or normalized_target.startswith("/")
        or ":" in normalized_target
        or any(
            character in WINDOWS_FORBIDDEN_CHARACTERS
            or ord(character) < 32
            or ord(character) == 127
            for character in normalized_target
        )
    ):
        return True
    if any(
        component not in {".", ".."}
        and is_unsafe_windows_component(component)
        for component in PurePosixPath(normalized_target).parts
    ):
        return True
    member = PurePosixPath(member_name.replace("\\", "/"))
    member_root = member.parts[0] if member.parts else ""
    resolved = list(member.parent.parts) if symbolic else []
    for component in PurePosixPath(normalized_target).parts:
        if component in {"", "."}:
            continue
        if component == "..":
            if not resolved:
                return True
            resolved.pop()
        else:
            resolved.append(component)
    return not resolved or resolved[0] != member_root


def inspect_archive_link(
    label: str,
    member_name: str,
    target: str,
    symbolic: bool,
    findings: list[str],
    inspect_bytes: ContentInspector,
    inspect_release_path: PathInspector,
) -> None:
    if link_target_escapes(member_name, target, symbolic):
        findings.append(f"{label}: unsafe archive link target")
    inspect_release_path(f"{label} link target", target, findings)
    inspect_bytes(f"{label} link target", target.encode(), findings)


def inspect_archive_metadata(
    label: str,
    values: list[bytes],
    findings: list[str],
    inspect_bytes: ContentInspector,
) -> None:
    for index, value in enumerate(values):
        if value:
            inspect_bytes(f"{label} metadata {index}", value, findings)


def inspect_archive_member_name(
    label: str,
    member_name: str,
    findings: list[str],
    inspect_bytes: ContentInspector,
    inspect_release_path: PathInspector,
    tracker: ArchiveMemberTracker,
    is_directory: bool,
) -> None:
    tracker.inspect(label, member_name, is_directory, findings)
    inspect_archive_member_path(label, member_name, findings)
    inspect_release_path(label, member_name, findings)
    inspect_bytes(f"{label} member name", member_name.encode(), findings)


def inspect_tar_contents(
    path: Path,
    archive: tarfile.TarFile,
    findings: list[str],
    inspect_bytes: ContentInspector,
    inspect_release_path: PathInspector,
) -> None:
    tracker = ArchiveMemberTracker()
    inspect_archive_metadata(
        f"{path}!archive",
        [
            f"{key}={value}".encode()
            for key, value in sorted(archive.pax_headers.items())
        ],
        findings,
        inspect_bytes,
    )
    for member in archive:
        label = f"{path}!{member.name}"
        inspect_archive_metadata(
            label,
            [
                member.uname.encode(),
                member.gname.encode(),
                *(
                    f"{key}={value}".encode()
                    for key, value in sorted(member.pax_headers.items())
                ),
            ],
            findings,
            inspect_bytes,
        )
        inspect_archive_member_name(
            label,
            member.name,
            findings,
            inspect_bytes,
            inspect_release_path,
            tracker,
            member.isdir(),
        )
        if member.isfile():
            source = archive.extractfile(member)
            if source is not None:
                inspect_bytes(label, source.read(), findings)
        elif member.issym() or member.islnk():
            inspect_archive_link(
                label,
                member.name,
                member.linkname,
                member.issym(),
                findings,
                inspect_bytes,
                inspect_release_path,
            )
        elif not member.isdir():
            findings.append(f"{label}: unsupported archive member type")


def inspect_zstd_tar(
    path: Path,
    findings: list[str],
    inspect_bytes: ContentInspector,
    inspect_release_path: PathInspector,
) -> None:
    zstd = shutil.which("zstd")
    if zstd is None:
        findings.append(f"{path}: zstd is required to inspect this archive")
        return
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(
            [zstd, "--quiet", "--decompress", "--stdout", "--", str(path)],
            stdout=subprocess.PIPE,
            stderr=errors,
        )
        if process.stdout is None:
            process.kill()
            process.wait()
            raise OSError("could not read zstd output")
        try:
            with process.stdout, tarfile.open(
                fileobj=process.stdout, mode="r|"
            ) as archive:
                inspect_tar_contents(
                    path,
                    archive,
                    findings,
                    inspect_bytes,
                    inspect_release_path,
                )
        except BaseException:
            process.kill()
            process.wait()
            raise
        return_code = process.wait()
        if return_code != 0:
            errors.seek(0)
            detail = errors.read().decode("utf-8", errors="replace").strip()
            raise tarfile.ReadError(detail or "zstd decompression failed")


def inspect_archive(
    path: Path,
    findings: list[str],
    *,
    inspect_bytes: ContentInspector,
    inspect_release_path: PathInspector,
) -> None:
    if zipfile.is_zipfile(path):
        tracker = ArchiveMemberTracker()
        with zipfile.ZipFile(path) as archive:
            inspect_archive_metadata(
                f"{path}!archive",
                [archive.comment],
                findings,
                inspect_bytes,
            )
            for info in archive.infolist():
                label = f"{path}!{info.filename}"
                file_type = (
                    stat.S_IFMT(info.external_attr >> 16)
                    if info.create_system == 3
                    else 0
                )
                directory_name = info.filename.endswith("/")
                is_directory = (
                    file_type == stat.S_IFDIR
                    or file_type == 0 and directory_name
                )
                inspect_archive_metadata(
                    label,
                    [info.comment, info.extra],
                    findings,
                    inspect_bytes,
                )
                inspect_archive_member_name(
                    label,
                    info.filename,
                    findings,
                    inspect_bytes,
                    inspect_release_path,
                    tracker,
                    is_directory,
                )
                if file_type not in {
                    0,
                    stat.S_IFDIR,
                    stat.S_IFLNK,
                    stat.S_IFREG,
                }:
                    findings.append(
                        f"{label}: unsupported archive member type"
                    )
                    continue
                if (
                    file_type == stat.S_IFDIR and not directory_name
                    or file_type in {stat.S_IFLNK, stat.S_IFREG}
                    and directory_name
                ):
                    findings.append(
                        f"{label}: archive member type conflicts with name"
                    )
                if file_type == stat.S_IFLNK:
                    data = archive.read(info)
                    inspect_archive_link(
                        label,
                        info.filename,
                        data.decode("utf-8", errors="replace"),
                        True,
                        findings,
                        inspect_bytes,
                        inspect_release_path,
                    )
                elif not directory_name and file_type != stat.S_IFDIR:
                    inspect_bytes(label, archive.read(info), findings)
        return
    if path.name.casefold().endswith((".tar.zst", ".tzst")):
        inspect_zstd_tar(
            path,
            findings,
            inspect_bytes,
            inspect_release_path,
        )
        return
    if tarfile.is_tarfile(path):
        with tarfile.open(path, "r:*") as archive:
            inspect_tar_contents(
                path,
                archive,
                findings,
                inspect_bytes,
                inspect_release_path,
            )
        return
    inspect_bytes(str(path), path.read_bytes(), findings)
