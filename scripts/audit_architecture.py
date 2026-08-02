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

    reject_includes(
        "src/core",
        ("app/", "ui/", "platform/", "android/"),
        failures,
    )
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
    if "gdox_usb_bot_identity_matches(candidate, &observed)" not in macos_transport:
        failures.append("macOS shared-USB-ID selection bypasses the exact matcher")
    if "GDOX_USB_BOT_ASUS_NR09" not in macos_transport:
        failures.append("macOS transport omits the ASUS A202 identity")

    windows_transport = (ROOT / "src/platform/usb_bot_windows.c").read_text(
        encoding="utf-8"
    )
    if "gdox_usb_bot_identity_matches(requested, &observed)" not in windows_transport:
        failures.append("Windows shared-USB-ID selection bypasses the exact matcher")

    optical_graph = (ROOT / "cmake/GdoxOptical.cmake").read_text(
        encoding="utf-8"
    )
    if "gdox::media" not in optical_graph or "gdox::core" in optical_graph:
        failures.append(
            "optical services must depend on the portable media core only"
        )
    for required in (
        "src/platform/asus_nr09_source.c",
        "src/platform/gp08_source.c",
        "src/platform/mmc_commands.c",
        "src/platform/mt1887_source.c",
    ):
        if required not in optical_graph:
            failures.append(f"desktop optical graph omits {required}")

    core_graph = (ROOT / "cmake/GdoxCore.cmake").read_text(encoding="utf-8")
    for required in (
        "add_library(gdox::media ALIAS gdox_media_core)",
        "add_library(gdox::platform ALIAS gdox_platform_services)",
        "add_library(gdox::services ALIAS gdox_desktop_services)",
    ):
        if required not in core_graph:
            failures.append(
                "desktop graph does not separate portable core and platform services"
            )
            break
    core_sources = re.search(
        r"add_library\(\s*gdox_core\s+STATIC(?P<sources>.*?)\n\)",
        core_graph,
        re.DOTALL,
    )
    if core_sources is None or "src/platform/" in core_sources.group("sources"):
        failures.append("gdox_core directly compiles platform implementation sources")
    core_links = re.search(
        r"target_link_libraries\(\s*gdox_core(?P<links>.*?)\n\)",
        core_graph,
        re.DOTALL,
    )
    if core_links is None or "gdox::platform" in core_links.group("links"):
        failures.append("gdox_core target depends outward on platform services")
    services_links = re.search(
        r"target_link_libraries\(\s*gdox_desktop_services(?P<links>.*?)\n\)",
        core_graph,
        re.DOTALL,
    )
    if services_links is None or not all(
        dependency in services_links.group("links")
        for dependency in ("gdox::core", "gdox::platform")
    ):
        failures.append("desktop services do not compose core and platform targets")

    runtime_graph = (ROOT / "cmake/GdoxRuntime.cmake").read_text(
        encoding="utf-8"
    )
    for required in (
        "add_library(gdox::runtime ALIAS gdox_desktop_runtime)",
        "src/app/runtime.c",
        "src/app/runtime_controls.c",
        "src/app/runtime_media.c",
        "src/app/runtime_preservation.c",
        "gdox::services",
        "gdox::optical",
    ):
        if required not in runtime_graph:
            failures.append("desktop runtime target omits production runtime code")
            break

    optical_registry = (ROOT / "src/platform/optical.c").read_text(
        encoding="utf-8"
    )
    for required in (
        "static const gdox_optical_driver drivers[]",
        "gdox_usb_bot_observe_all(",
        "gdox_usb_bot_present_all(",
        "gdox_optical_select_presence(",
        "GDOX_OPTICAL_DRIVE_ASUS_NR09",
    ):
        if required not in optical_registry:
            failures.append("desktop optical registry is not table-driven")
    legacy_optical_calls = (
        "gdox_optical_observe_gp63(",
        "gdox_optical_observe_gp65(",
        "gdox_optical_observe_gp08(",
        "gdox_optical_observe_asus_nr09(",
        "gdox_optical_gp63_connected(",
        "gdox_optical_gp65_connected(",
        "gdox_optical_gp08_connected(",
        "gdox_optical_asus_nr09_connected(",
    )
    for legacy in legacy_optical_calls:
        if legacy in optical_registry:
            failures.append(f"desktop optical registry retains legacy wrapper {legacy}")

    optical_header = (ROOT / "include/gdox/optical.h").read_text(encoding="utf-8")
    for legacy in legacy_optical_calls:
        if legacy in optical_header:
            failures.append(f"public optical API retains legacy wrapper {legacy}")

    for name, read_command in (
        ("asus_nr09_source.c", "gdox_mmc_read_10("),
        ("gp08_source.c", "gdox_mmc_read_10("),
        ("mt1887_source.c", "gdox_mmc_read_12("),
    ):
        source = (ROOT / "src/platform" / name).read_text(encoding="utf-8")
        for required in (
            "gdox_mmc_inquiry(",
            "gdox_mmc_read_capacity_10(",
            "gdox_mmc_read_dvd_structure(",
            read_command,
        ):
            if required not in source:
                failures.append(f"{name} bypasses shared MMC command {required}")
        if re.search(
            r"static\s+bool\s+(?:inquiry|read_capacity|read_dvd_structure)\s*\(",
            source,
        ):
            failures.append(f"{name} duplicates shared MMC command construction")

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
    if "src/platform/mmc_commands.c" not in android_graph:
        failures.append("Android optical stack omits shared MMC commands")

    if failures:
        for failure in failures:
            print(f"architecture audit: {failure}", file=sys.stderr)
        return 1
    print("Architecture boundaries are clean.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
