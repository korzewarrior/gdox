#!/usr/bin/env python3
"""Enforce dependency direction between GDOX product layers."""

from __future__ import annotations

from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
INCLUDE = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]', re.MULTILINE)


def source_files(directory: str) -> list[Path]:
    return sorted(
        path
        for path in (ROOT / directory).rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def reject_includes(
    directory: str,
    forbidden_prefixes: tuple[str, ...],
    failures: list[str],
) -> None:
    for path in source_files(directory):
        text = path.read_text(encoding="utf-8")
        for included in INCLUDE.findall(text):
            if included.startswith(forbidden_prefixes):
                failures.append(
                    f"{relative(path)} crosses its layer boundary through {included}"
                )


def reject_tokens(
    directory: str,
    forbidden_tokens: tuple[str, ...],
    failures: list[str],
) -> None:
    for path in source_files(directory):
        text = path.read_text(encoding="utf-8")
        for token in forbidden_tokens:
            if token in text:
                failures.append(
                    f"{relative(path)} contains platform-specific token {token}"
                )


def main() -> int:
    failures: list[str] = []

    reject_includes("src/core", ("app/", "ui/", "android/"), failures)
    reject_includes("src/app", ("ui/", "android/"), failures)
    reject_includes("src/ui", ("platform/", "android/"), failures)
    reject_includes("android/native", ("app/", "ui/"), failures)
    reject_includes(
        "include/gdox",
        ("app/", "ui/", "platform/", "android/"),
        failures,
    )
    reject_tokens(
        "include/gdox",
        ("__ANDROID__", "ANDROID", "__APPLE__", "_WIN32"),
        failures,
    )
    reject_tokens("src/core", ("__ANDROID__", "org.korze.gdox"), failures)
    reject_tokens("src/app", ("__ANDROID__", "org.korze.gdox"), failures)
    reject_tokens("src/ui", ("__ANDROID__", "org.korze.gdox"), failures)

    desktop_graph = "\n".join(
        path.read_text(encoding="utf-8")
        for path in [ROOT / "CMakeLists.txt", *sorted((ROOT / "cmake").glob("*.cmake"))]
    )
    if re.search(r"\bandroid/", desktop_graph):
        failures.append("desktop CMake graph directly references Android sources")

    libusb_transport = (ROOT / "src/platform/usb_bot_libusb.c").read_text(
        encoding="utf-8"
    )
    for required in (
        "gdox_usb_bot_candidate_matches(",
        "usb->location_valid",
        "claim_unbound_bot_interface(",
        "restore_exact_linux_candidate(",
    ):
        if required not in libusb_transport:
            failures.append(
                "Linux shared-USB-ID recovery is missing exact identity selection"
            )

    macos_transport = (ROOT / "src/platform/macos_scsi.c").read_text(
        encoding="utf-8"
    )
    if "gdox_usb_bot_identity_matches(requested, &observed)" not in macos_transport:
        failures.append("macOS shared-USB-ID selection bypasses the exact matcher")

    windows_transport = (ROOT / "src/platform/usb_bot_windows.c").read_text(
        encoding="utf-8"
    )
    if "gdox_usb_bot_identity_matches(requested, &observed)" not in windows_transport:
        failures.append("Windows shared-USB-ID selection bypasses the exact matcher")

    android_graph = (ROOT / "android/native/CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    for source_set in ("GDOX_LIVE_DISC_SOURCES", "GDOX_HDD_CACHE_SOURCES"):
        if source_set not in android_graph:
            failures.append(
                f"Android build does not consume the shared {source_set} set"
            )
    if "src/platform/mt1887_profile.c" not in android_graph:
        failures.append("Android MT1887 source omits the identity profile")
    if "src/platform/usb_bot_identity.c" not in android_graph:
        failures.append("Android USB transport omits the shared identity matcher")

    if failures:
        for failure in failures:
            print(f"architecture audit: {failure}", file=sys.stderr)
        return 1
    print("Architecture boundaries are clean.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
