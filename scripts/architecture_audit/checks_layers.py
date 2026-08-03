"""Layer direction and host transport architecture checks."""

from __future__ import annotations

from .repository import Repository


def _reject_includes(
    repository: Repository,
    directory: str,
    forbidden_prefixes: tuple[str, ...],
) -> list[str]:
    failures: list[str] = []
    for source in repository.source_files(directory):
        for included in source.includes:
            if included.startswith(forbidden_prefixes):
                failures.append(
                    f"{repository.relative(source.path)} crosses its layer boundary "
                    f"through {included}"
                )
    return failures


def _reject_tokens(
    repository: Repository,
    directory: str,
    forbidden_tokens: tuple[str, ...],
) -> list[str]:
    failures: list[str] = []
    for source in repository.source_files(directory):
        for token in forbidden_tokens:
            if token in source.text:
                failures.append(
                    f"{repository.relative(source.path)} contains platform-specific "
                    f"token {token}"
                )
    return failures


def check_layers(repository: Repository) -> list[str]:
    failures: list[str] = []
    failures.extend(
        _reject_includes(
            repository,
            "src/core",
            ("app/", "ui/", "platform/", "android/"),
        )
    )
    failures.extend(_reject_includes(repository, "src/app", ("ui/", "android/")))
    failures.extend(
        _reject_includes(repository, "src/ui", ("platform/", "android/"))
    )
    failures.extend(
        _reject_includes(repository, "android/native", ("app/", "ui/"))
    )
    failures.extend(
        _reject_includes(
            repository,
            "include/gdox",
            ("app/", "ui/", "platform/", "android/"),
        )
    )
    failures.extend(
        _reject_tokens(
            repository,
            "include/gdox",
            ("__ANDROID__", "ANDROID", "__APPLE__", "_WIN32"),
        )
    )
    failures.extend(
        _reject_tokens(
            repository,
            "src/core",
            ("__ANDROID__", "org.korze.gdox"),
        )
    )
    failures.extend(
        _reject_tokens(
            repository,
            "src/app",
            ("__ANDROID__", "org.korze.gdox"),
        )
    )
    failures.extend(
        _reject_tokens(
            repository,
            "src/ui",
            ("__ANDROID__", "org.korze.gdox"),
        )
    )

    if any("android/" in source for source in repository.cmake.all_source_paths()):
        failures.append("desktop CMake graph directly references Android sources")

    desktop_xemu = repository.source("src/app/runtime_xemu.c")
    if (
        "core/hdd_cache.h" in desktop_xemu.includes
        or "gdox_hdd_reset_cache_partitions" in desktop_xemu.calls
    ):
        failures.append("desktop xemu launch erases guest-owned scratch partitions")

    linux = repository.source("src/platform/usb_bot_libusb.c").text
    for required in (
        "gdox_usb_bot_candidate_matches(",
        "usb->location_valid",
        "gdox_usb_bot_recovery_identity(",
        "observe_unbound_usb_candidates(",
        "usb->handoff.reattach_required = true;",
    ):
        if required not in linux:
            failures.append(
                "Linux shared-USB-ID recovery is missing exact identity selection"
            )

    macos = repository.source("src/platform/macos_scsi.c").text
    if "gdox_usb_bot_identity_matches(candidate, &observed)" not in macos:
        failures.append("macOS shared-USB-ID selection bypasses the exact matcher")
    if "GDOX_USB_BOT_ASUS_NR09" not in macos:
        failures.append("macOS transport omits the ASUS A202 identity")

    windows = repository.source("src/platform/usb_bot_windows.c").text
    if "gdox_usb_bot_identity_matches(requested, &observed)" not in windows:
        failures.append("Windows shared-USB-ID selection bypasses the exact matcher")
    return failures
