#!/usr/bin/env python3
"""Reject private runtime data, credentials, and unsafe release contents."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parent.parent
EXCLUDED_DIRECTORIES = {
    ".git",
    ".gdox-lab",
    "__pycache__",
    "dist",
    "target",
}
TEXT_LIMIT = 8 * 1024 * 1024

TEXT_RULES = (
    (
        "absolute Linux home path",
        re.compile(r"/home/(?!user(?:/|\b)|<)[A-Za-z0-9._-]+/"),
    ),
    (
        "absolute macOS home path",
        re.compile(r"/Users/(?!user(?:/|\b)|<)[A-Za-z0-9._-]+/"),
    ),
    (
        "absolute Windows profile path",
        re.compile(r"(?i)[A-Z]:[\\/]+Users[\\/]+(?!user(?:[\\/]|\b)|<)[^\\/\s]+[\\/]"),
    ),
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
        re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    ),
    (
        "credential-shaped token",
        re.compile(r"(?i)\b(?:api[_-]?key|access[_-]?token|client[_-]?secret|password)\s*[:=]\s*[\"'][^\"']{8,}"),
    ),
    (
        "personal email address",
        re.compile(r"\b(?![^@\s]+@example\.(?:com|org|net)\b)[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b"),
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


def is_third_party_source(label: str) -> bool:
    normalized = label.replace("\\", "/")
    if "/source/" in normalized and "/source/gdox/" not in normalized:
        return True
    if "!" not in normalized:
        return False
    member = normalized.split("!", 1)[1].lstrip("./")
    return member.startswith(("xemu-", "SDL2-"))


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
                if path.is_file():
                    yield path
        return
    for directory, names, files in os.walk(root):
        names[:] = sorted(name for name in names if name not in EXCLUDED_DIRECTORIES)
        base = Path(directory)
        for name in sorted(files):
            yield base / name


def inspect_bytes(label: str, data: bytes, findings: list[str]) -> None:
    normalized_label = label.replace("\\", "/")
    verified_xemu_runtime = "/runtime/xemu/" in normalized_label
    third_party_source = is_third_party_source(normalized_label)
    if not verified_xemu_runtime:
        for marker_label, marker in RUNTIME_MARKERS:
            if marker in data:
                findings.append(f"{label}: contains {marker_label}")

    if (
        third_party_source
        or len(data) > TEXT_LIMIT
        or b"\0" in data[:8192]
    ):
        return
    text = data.decode("utf-8", errors="replace")
    third_party_notice = any(
        marker in normalized_label
        for marker in (
            "/xemu/licenses/",
            "/xemu/LICENSE.txt",
            "/hdd/LICENSE.txt",
        )
    )
    for line_number, line in enumerate(text.splitlines(), 1):
        for rule_label, rule in TEXT_RULES:
            if (
                third_party_notice
                and rule_label == "personal email address"
                or verified_xemu_runtime
                and rule_label
                in {
                    "absolute Linux home path",
                    "absolute macOS home path",
                    "absolute Windows profile path",
                    "personal email address",
                }
            ):
                continue
            if rule.search(line):
                findings.append(f"{label}:{line_number}: {rule_label}")


def inspect_archive(path: Path, findings: list[str]) -> None:
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if not info.is_dir():
                    inspect_bytes(f"{path}!{info.filename}", archive.read(info), findings)
        return
    if tarfile.is_tarfile(path):
        with tarfile.open(path, "r:*") as archive:
            for member in archive.getmembers():
                if member.isfile():
                    source = archive.extractfile(member)
                    if source is not None:
                        inspect_bytes(f"{path}!{member.name}", source.read(), findings)
        return
    inspect_bytes(str(path), path.read_bytes(), findings)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--path",
        action="append",
        type=Path,
        help="source tree or file to inspect (defaults to the repository)",
    )
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        type=Path,
        help="binary or archive to inspect, including archive members",
    )
    args = parser.parse_args()

    findings: list[str] = []
    for root in args.path or [ROOT]:
        if not root.exists():
            findings.append(f"{root}: audit path does not exist")
            continue
        for path in iter_files(root):
            try:
                inspect_bytes(str(path), path.read_bytes(), findings)
            except OSError as error:
                findings.append(f"{path}: could not read: {error}")
    for artifact in args.artifact:
        if not artifact.is_file():
            findings.append(f"{artifact}: release artifact does not exist")
            continue
        try:
            inspect_archive(artifact, findings)
        except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
            findings.append(f"{artifact}: could not inspect artifact: {error}")

    if findings:
        print("Release privacy audit failed:", file=sys.stderr)
        for finding in sorted(set(findings)):
            print(f"  {finding}", file=sys.stderr)
        return 1
    print("Release privacy audit passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
