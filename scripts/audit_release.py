#!/usr/bin/env python3
"""Reject incompatible binaries, private data, and unsafe release contents."""

from __future__ import annotations

import argparse
import os
import re
import socket
import subprocess
import sys
import tarfile
import zipfile
from collections.abc import Iterable
from pathlib import Path

sys.dont_write_bytecode = True
from elf_compatibility import (
    LinuxCompatibilityError,
    validate_release_artifact,
)
from release_archive_audit import inspect_archive as inspect_release_archive
from release_audit_provenance import (
    is_android_core_patch,
    is_pinned_source_member,
    is_third_party_source,
    is_valid_android_source_archive,
    is_verified_third_party_notice,
    is_verified_xemu_build_path_file,
    is_verified_xemu_known_test_key_file,
    is_verified_xemu_privacy_file,
    is_verified_xenia_runtime_file,
)

ROOT = Path(__file__).resolve().parent.parent
EXCLUDED_DIRECTORIES = {
    ".git",
    ".gdox-lab",
    "__pycache__",
    "dist",
    "target",
}
FORBIDDEN_RELEASE_COMPONENTS = {"research", "site", "website", "workspace"}
DEVELOPER_NO_RUNTIME_MARKER = "-developer-no-runtime"

TEXT_PATH_RULES = (
    (
        "absolute Linux home path",
        re.compile(r"/" + r"home/(?!user/|<)[^/\x00\r\n]{1,255}/"),
    ),
    (
        "absolute macOS home path",
        re.compile(
            r"/" + r"Users/(?!user/|<)[^/\x00\r\n]{1,255}/",
            re.IGNORECASE,
        ),
    ),
    (
        "absolute Windows profile path",
        re.compile(
            r"[A-Z]:[\\/]+Users[\\/]+(?!user[\\/]|<)"
            r"[^<>:\"/\\|?*\x00-\x1f]{1,255}[\\/]",
            re.IGNORECASE,
        ),
    ),
)

PRIVATE_KEY_HEADER_PATTERN = (
    r"-----BEGIN (?:(?:(?:RSA|EC|DSA|OPENSSH|ENCRYPTED) )?"
    r"PRIVATE KEY|PGP "
    r"PRIVATE KEY BLOCK)-----"
)
PRIVATE_KEY_BINARY_PATTERN = (
    r"(?:"
    r"-----BEGIN (?:(?:RSA|EC|DSA|OPENSSH|ENCRYPTED) )?PRIVATE KEY-----"
    r"\r?\n[A-Za-z0-9+/]{40,}={0,2}"
    r"|"
    r"-----BEGIN PGP "
    r"PRIVATE KEY BLOCK-----\r?\n"
    r"(?:[A-Za-z][A-Za-z0-9-]{0,31}:[^\r\n]{0,200}\r?\n){0,8}"
    r"\r?\n[A-Za-z0-9+/]{40,}={0,2}"
    r")"
)


TEXT_RULES = TEXT_PATH_RULES + (
    (
        "host-assigned BSG address",
        re.compile(r"/dev/bsg/\d+:\d+:\d+:\d+"),
    ),
    (
        "host-assigned runtime UID",
        re.compile(r"/run/user/\d+"),
    ),
    (
        "desktop/monitor placement rule",
        re.compile(
            "(?i)(bottom[- ]left "
            + "monitor|kwin-"
            + "gdox|monitor "
            + "assignment)"
        ),
    ),
    (
        "private key material",
        re.compile(PRIVATE_KEY_HEADER_PATTERN),
    ),
    (
        "credential-shaped token",
        re.compile(
            r"(?i)\b(?:api[_-]?key|access[_-]?token|"
            r"client[_-]?secret|password)\s*[:=]\s*[\"'][^\"']{8,}"
        ),
    ),
    (
        "personal email address",
        re.compile(
            r"\b(?![^@\s]+@example\.(?:com|org|net)\b)"
            r"[A-Za-z0-9._%+-]{1,64}@"
            r"[A-Za-z0-9.-]{1,253}\.[A-Za-z]{2,63}\b"
        ),
    ),
)

BINARY_PRIVACY_RULES = (
    (
        "absolute Linux home path",
        re.compile(
            rb"/" + rb"home/(?!user/|<)[^/\x00\r\n]{1,255}/"
        ),
    ),
    (
        "absolute macOS home path",
        re.compile(
            rb"/" + rb"Users/(?!user/|<)[^/\x00\r\n]{1,255}/",
            re.IGNORECASE,
        ),
    ),
    (
        "absolute Windows profile path",
        re.compile(
            rb"[A-Z]:[\\/]+Users[\\/]+(?!user[\\/]|<)"
            rb"[^<>:\"/\\|?*\x00-\x1f]{1,255}[\\/]",
            re.IGNORECASE,
        ),
    ),
    (
        "host-assigned BSG address",
        re.compile(rb"/dev/bsg/\d+:\d+:\d+:\d+"),
    ),
    (
        "host-assigned runtime UID",
        re.compile(rb"/run/user/\d+"),
    ),
    (
        "desktop/monitor placement rule",
        re.compile(
            b"(bottom[- ]left "
            + b"monitor|kwin-"
            + b"gdox|monitor "
            + b"assignment)",
            re.IGNORECASE,
        ),
    ),
    (
        "private key material",
        re.compile(PRIVATE_KEY_BINARY_PATTERN.encode("ascii")),
    ),
    (
        "credential-shaped token",
        re.compile(
            rb"(?:api[_-]?key|access[_-]?token|client[_-]?secret|password)"
            rb"\s*[:=]\s*[\"'][^\"']{8}",
            re.IGNORECASE,
        ),
    ),
    (
        "personal email address",
        re.compile(
            rb"(?![^@\s]+@example\.(?:com|org|net)(?:\b|$))"
            rb"[A-Za-z0-9._%+-]{1,64}@"
            rb"[A-Za-z0-9.-]{1,253}\.[A-Za-z]{2,63}",
        ),
    ),
)


def utf16le_literal(
    literal: bytes,
    *,
    ignore_case: bool = False,
) -> re.Pattern[bytes]:
    expression = b"".join(
        re.escape(bytes((character,))) + b"\x00"
        for character in literal
    )
    return re.compile(expression, re.IGNORECASE if ignore_case else 0)


PRIVACY_SENTINEL_LITERALS = {
    "absolute Linux home path": ((b"/home/", False),),
    "absolute macOS home path": ((b"/Users/", True),),
    "absolute Windows profile path": (
        (b"Users\\", True),
        (b"Users/", True),
    ),
    "host-assigned BSG address": ((b"/dev/bsg/", False),),
    "host-assigned runtime UID": ((b"/run/user/", False),),
    "desktop/monitor placement rule": (
        (b"monitor", True),
        (b"kwin-" + b"gdox", True),
    ),
    "private key material": ((b"-----BEGIN ", False),),
    "credential-shaped token": tuple(
        (literal, True)
        for literal in (
            b"api_key",
            b"api-key",
            b"apikey",
            b"access_token",
            b"access-token",
            b"accesstoken",
            b"client_secret",
            b"client-secret",
            b"clientsecret",
            b"password",
        )
    ),
    "personal email address": ((b"@", False),),
}
ASCII_PRIVACY_SENTINELS = {
    label: tuple(
        re.compile(re.escape(literal), re.IGNORECASE if ignore_case else 0)
        for literal, ignore_case in definitions
    )
    for label, definitions in PRIVACY_SENTINEL_LITERALS.items()
}
WIDE_PRIVACY_SENTINELS = {
    label: tuple(
        utf16le_literal(literal, ignore_case=ignore_case)
        for literal, ignore_case in definitions
    )
    for label, definitions in PRIVACY_SENTINEL_LITERALS.items()
}
BINARY_PRIVACY_RULES_BY_LABEL = dict(BINARY_PRIVACY_RULES)
TEXT_RULES_BY_LABEL = dict(TEXT_RULES)
BINARY_TEXT_RULES_BY_LABEL = dict(TEXT_RULES_BY_LABEL)
BINARY_TEXT_RULES_BY_LABEL["private key material"] = re.compile(
    PRIVATE_KEY_BINARY_PATTERN
)
ASCII_CONTEXT_BEFORE = {
    "absolute Windows profile path": 256,
    "desktop/monitor placement rule": 256,
    "personal email address": 64,
}
ASCII_CONTEXT_AFTER = {
    "desktop/monitor placement rule": 256,
    "private key material": 4096,
    "credential-shaped token": 128,
    "personal email address": 320,
}
WIDE_CONTEXT_BEFORE = {
    "absolute Windows profile path": 512,
    "desktop/monitor placement rule": 512,
    "personal email address": 128,
}
WIDE_CONTEXT_AFTER = {
    "desktop/monitor placement rule": 512,
    "private key material": 8192,
    "credential-shaped token": 256,
    "personal email address": 640,
}
DEFAULT_ASCII_CONTEXT_AFTER = 512
DEFAULT_WIDE_CONTEXT_AFTER = 1024
EMAIL_LOCAL_BYTES = frozenset(
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._%+-"
)
EMAIL_DOMAIN_BYTES = frozenset(
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-"
)
PUBLIC_PROJECT_CONTACTS = frozenset({"qemu-devel@nongnu.org"})


def is_public_project_contact(value: str | bytes) -> bool:
    if isinstance(value, bytes):
        value = value.decode("ascii", errors="strict")
    return value.casefold() in PUBLIC_PROJECT_CONTACTS


def has_disallowed_privacy_match(
    rule_label: str,
    matches: Iterable[re.Match[str] | re.Match[bytes]],
) -> bool:
    if rule_label != "personal email address":
        return next(iter(matches), None) is not None
    return any(
        not is_public_project_contact(match.group(0))
        for match in matches
    )


PUBLIC_HYGIENE_RULES = (
    (
        "automated authorship reference",
        re.compile(
            r"\b(?:"
            + "chat"
            + "gpt|open"
            + "ai|clau"
            + "de|co"
            + "pilot|gem"
            + "ini|anth"
            + "ropic|l"
            + r"lm)\b",
            re.IGNORECASE,
        ),
    ),
    (
        "automated-content attribution",
        re.compile(
            r"(?:\b"
            + "a"
            + r"i[\s_-]*(?:generated|authored|written|assisted|produced|created)\b|"
            + r"\b(?:generated|authored|written|assisted|produced|created)"
            + r"[\s_-]+(?:by|with)[\s_-]+"
            + "a"
            + r"i\b|\b(?:with|using)[\s_-]+"
            + "a"
            + r"i(?:[\s_-]+(?:assistance|support|help))?\b|"
            + r"\b"
            + "a"
            + r"i[\s_-]+(?:assistance|support|help)\b|"
            + r"\bas[\s_-]+an[\s_-]+"
            + "a"
            + r"i\b)",
            re.IGNORECASE,
        ),
    ),
    (
        "unfinished implementation marker",
        re.compile(
            r"\b(?:TO"
            + "DO|FIX"
            + "ME|HA"
            + "CK|X"
            + "XX|T"
            + "BD|PLACE"
            + r"HOLDER)\b"
        ),
    ),
    (
        "unprofessional language",
        re.compile(
            r"\b(?:bull"
            + "sh"
            + "it|sh"
            + r"it(?:s|ted|ting|ty)?|cr"
            + r"ap(?:ped|ping|py)?|motherfu"
            + r"ck(?:ed|er|ers|ing|s)?|fu"
            + r"ck(?:ed|er|ers|ing|s)?|ass"
            + r"hole(?:s)?|dumb"
            + r"ass(?:es)?|bi"
            + r"tch(?:ed|es|ing|y)?|da"
            + r"mn(?:ed)?)\b",
            re.IGNORECASE,
        ),
    ),
)


def runtime_markers() -> list[tuple[str, bytes]]:
    generic_values = {
        "/",
        "/tmp",
        "localhost",
        "nobody",
        "root",
        "runner",
        "ubuntu",
        "user",
    }
    values = {
        "current home directory": str(Path.home()),
        "current host name": socket.gethostname(),
    }
    explicit_markers = ",".join(
        (
            os.environ.get("GDOX_AUDIT_FORBID", ""),
        )
    )
    values.update(
        (f"GDOX_AUDIT_FORBID item {index}", value.strip())
        for index, value in enumerate(explicit_markers.split(","))
        if value.strip()
    )
    return [
        (label, value.encode())
        for label, value in values.items()
        if len(value) >= 4 and value.lower() not in generic_values
    ]


RUNTIME_MARKERS = runtime_markers()


def privacy_rule_is_exempt(
    rule_label: str,
    *,
    verified_runtime: bool,
    third_party_source: bool,
    third_party_notice: bool,
    verified_known_test_key: bool,
) -> bool:
    if third_party_source:
        return True
    if verified_known_test_key and rule_label == "private key material":
        return True
    if (
        (third_party_source or third_party_notice)
        and rule_label == "personal email address"
    ):
        return True
    return verified_runtime and rule_label in {
        "absolute Linux home path",
        "absolute macOS home path",
        "absolute Windows profile path",
        "personal email address",
    }


def inspect_binary_privacy(
    label: str,
    data: bytes,
    findings: list[str],
    *,
    verified_runtime: bool,
    third_party_source: bool,
    third_party_notice: bool,
    verified_known_test_key: bool,
) -> None:
    active_labels: list[str] = []
    for rule_label, _ in BINARY_PRIVACY_RULES:
        if privacy_rule_is_exempt(
            rule_label,
            verified_runtime=verified_runtime,
            third_party_source=third_party_source,
            third_party_notice=third_party_notice,
            verified_known_test_key=verified_known_test_key,
        ):
            continue
        active_labels.append(rule_label)

    for rule_label in active_labels:
        binary_rule = BINARY_PRIVACY_RULES_BY_LABEL[rule_label]
        before = ASCII_CONTEXT_BEFORE.get(rule_label, 0)
        after = ASCII_CONTEXT_AFTER.get(
            rule_label,
            DEFAULT_ASCII_CONTEXT_AFTER,
        )
        found = False
        for sentinel in ASCII_PRIVACY_SENTINELS[rule_label]:
            for match in sentinel.finditer(data):
                if rule_label == "personal email address" and (
                    match.start() == 0
                    or match.end() == len(data)
                    or data[match.start() - 1] not in EMAIL_LOCAL_BYTES
                    or data[match.end()] not in EMAIL_DOMAIN_BYTES
                ):
                    continue
                start = max(0, match.start() - before)
                end = min(len(data), match.end() + after)
                if has_disallowed_privacy_match(
                    rule_label,
                    binary_rule.finditer(data[start:end]),
                ):
                    found = True
                    break
            if found:
                break
        if found:
            findings.append(f"{label}: {rule_label}")
            continue

        text_rule = BINARY_TEXT_RULES_BY_LABEL[rule_label]
        before = WIDE_CONTEXT_BEFORE.get(rule_label, 0)
        after = WIDE_CONTEXT_AFTER.get(
            rule_label,
            DEFAULT_WIDE_CONTEXT_AFTER,
        )
        found_wide = False
        for sentinel in WIDE_PRIVACY_SENTINELS[rule_label]:
            for match in sentinel.finditer(data):
                if rule_label == "personal email address" and (
                    match.start() < 2
                    or match.end() + 1 >= len(data)
                    or data[match.start() - 2] not in EMAIL_LOCAL_BYTES
                    or data[match.start() - 1] != 0
                    or data[match.end()] not in EMAIL_DOMAIN_BYTES
                    or data[match.end() + 1] != 0
                ):
                    continue
                start = max(0, match.start() - before)
                start += (match.start() - start) % 2
                end = min(len(data), match.end() + after)
                end -= (end - start) % 2
                context = data[start:end].decode(
                    "utf-16le",
                    errors="ignore",
                )
                if has_disallowed_privacy_match(
                    rule_label,
                    text_rule.finditer(context),
                ):
                    found_wide = True
                    break
            if found_wide:
                break
        if found_wide:
            findings.append(f"{label}: {rule_label}")


def iter_files(root: Path):
    if root.is_file():
        yield root
        return
    if root.resolve() == ROOT and (ROOT / ".git").exists():
        result = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
            cwd=ROOT,
            check=True,
            capture_output=True,
        )
        for name in result.stdout.split(b"\0"):
            if name:
                path = ROOT / os.fsdecode(name)
                if path.is_symlink() or path.is_file():
                    yield path
        return
    excluded_directories = (
        EXCLUDED_DIRECTORIES if root.resolve() == ROOT else frozenset()
    )
    for directory, names, files in os.walk(root):
        base = Path(directory)
        retained_directories: list[str] = []
        for name in sorted(names):
            if name in excluded_directories:
                continue
            path = base / name
            if path.is_symlink():
                yield path
            else:
                retained_directories.append(name)
        names[:] = retained_directories
        for name in sorted(files):
            yield base / name


def inspect_filesystem_symlink(
    root: Path,
    path: Path,
    findings: list[str],
) -> None:
    try:
        target = os.readlink(path)
    except OSError as error:
        findings.append(f"{path}: could not read link target: {error}")
        return
    inspect_release_path(f"{path} link target", target, findings)
    inspect_bytes(f"{path} link target", os.fsencode(target), findings)
    try:
        if Path(target).is_absolute() or not path.resolve(
            strict=True
        ).is_relative_to(root.resolve(strict=True)):
            findings.append(f"{path}: unsafe filesystem link target")
    except OSError:
        findings.append(f"{path}: broken filesystem link target")


def inspect_bytes(label: str, data: bytes, findings: list[str]) -> None:
    normalized_label = label.replace("\\", "/")
    pinned_source_member = is_pinned_source_member(normalized_label)
    if pinned_source_member:
        for marker_label, marker in RUNTIME_MARKERS:
            wide_marker = marker.decode(errors="ignore").encode("utf-16le")
            if marker in data or wide_marker in data:
                findings.append(f"{label}: contains {marker_label}")
        return
    verified_xemu_runtime = is_verified_xemu_build_path_file(
        normalized_label,
        data,
    ) or is_verified_xemu_privacy_file(normalized_label, data)
    verified_known_test_key = is_verified_xemu_known_test_key_file(
        normalized_label,
        data,
    )
    verified_xenia_runtime = is_verified_xenia_runtime_file(
        normalized_label,
        data,
    )
    verified_runtime = verified_xemu_runtime or verified_xenia_runtime
    third_party_source = is_third_party_source(normalized_label, data)
    android_core_patch = is_android_core_patch(normalized_label)
    third_party_notice = is_verified_third_party_notice(
        normalized_label,
        data,
    )
    if not verified_runtime:
        for marker_label, marker in RUNTIME_MARKERS:
            wide_marker = marker.decode(errors="ignore").encode("utf-16le")
            if marker in data or wide_marker in data:
                findings.append(f"{label}: contains {marker_label}")

    if b"\0" in data:
        inspect_binary_privacy(
            label,
            data,
            findings,
            verified_runtime=verified_runtime,
            third_party_source=third_party_source,
            third_party_notice=third_party_notice,
            verified_known_test_key=verified_known_test_key,
        )
        return
    text = data.decode("utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), 1):
        if not third_party_source:
            for rule_label, rule in PUBLIC_HYGIENE_RULES:
                if (
                    (android_core_patch or third_party_notice)
                    and rule_label == "unfinished implementation marker"
                ):
                    continue
                if (
                    normalized_label.endswith(".patch")
                    and line.startswith((" ", "-"))
                    and rule_label == "unfinished implementation marker"
                ):
                    continue
                if rule.search(line):
                    findings.append(f"{label}:{line_number}: {rule_label}")
        for rule_label, rule in TEXT_RULES:
            if privacy_rule_is_exempt(
                rule_label,
                verified_runtime=verified_runtime,
                third_party_source=third_party_source,
                third_party_notice=third_party_notice,
                verified_known_test_key=verified_known_test_key,
            ):
                continue
            if has_disallowed_privacy_match(
                rule_label,
                rule.finditer(line),
            ):
                findings.append(f"{label}:{line_number}: {rule_label}")


def inspect_release_path(label: str, path: str, findings: list[str]) -> None:
    components = {
        component.lower()
        for component in path.replace("\\", "/").split("/")
        if component not in {"", "."}
    }
    if components & FORBIDDEN_RELEASE_COMPONENTS:
        findings.append(f"{label}: development-only path")
    if any(DEVELOPER_NO_RUNTIME_MARKER in component for component in components):
        findings.append(f"{label}: developer-only runtime-less package")


def inspect_archive(path: Path, findings: list[str]) -> None:
    if DEVELOPER_NO_RUNTIME_MARKER in path.name.casefold():
        findings.append(f"{path}: developer-only runtime-less package")
    if (
        "-android-corresponding-source" in path.name
        and not is_valid_android_source_archive(path)
    ):
        findings.append(
            f"{path}: invalid Android corresponding-source structure"
        )
    inspect_release_archive(
        path,
        findings,
        inspect_bytes=inspect_bytes,
        inspect_release_path=inspect_release_path,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--path",
        action="append",
        type=Path,
        help=(
            "source tree or file to inspect; defaults to the repository only "
            "when no artifacts are supplied"
        ),
    )
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        type=Path,
        help="binary or archive to inspect, including archive members",
    )
    parser.add_argument(
        "--target",
        help="release target for direct executable compatibility checks",
    )
    args = parser.parse_args()

    findings: list[str] = []
    configured_roots = args.path or ([] if args.artifact else [ROOT])
    for configured_root in configured_roots:
        root = configured_root.resolve()
        if not root.exists():
            findings.append(f"{root}: audit path does not exist")
            continue
        for path in iter_files(root):
            try:
                relative = (
                    path.relative_to(root) if root.is_dir() else Path(path.name)
                )
                inspect_release_path(str(path), str(relative), findings)
                inspect_bytes(
                    f"{path} file name",
                    os.fsencode(str(relative)),
                    findings,
                )
                if path.is_symlink():
                    inspect_filesystem_symlink(root, path, findings)
                    continue
                inspect_archive(path, findings)
            except (
                OSError,
                tarfile.TarError,
                zipfile.BadZipFile,
            ) as error:
                findings.append(f"{path}: could not read: {error}")
    for artifact in args.artifact:
        if not artifact.is_file():
            findings.append(f"{artifact}: release artifact does not exist")
            continue
        try:
            inspect_archive(artifact, findings)
        except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
            findings.append(f"{artifact}: could not inspect artifact: {error}")
    if args.target is not None:
        if len(args.artifact) != 1:
            findings.append(
                "target-aware audit requires exactly one direct artifact"
            )
        elif args.artifact[0].is_file():
            try:
                validate_release_artifact(args.target, args.artifact[0])
            except LinuxCompatibilityError as error:
                findings.append(str(error))

    if findings:
        print("Release audit failed:", file=sys.stderr)
        for finding in sorted(set(findings)):
            print(f"  {finding}", file=sys.stderr)
        return 1
    print("Release audit passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
