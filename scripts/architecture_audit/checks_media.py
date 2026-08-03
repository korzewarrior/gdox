"""Live-media, optical registry, and session-policy architecture checks."""

from __future__ import annotations

import re

from .repository import Repository


def _normalized_sources(sources: tuple[str, ...]) -> tuple[str, ...]:
    prefixes = ("${GDOX_ROOT}/", "${CMAKE_CURRENT_SOURCE_DIR}/")
    normalized: list[str] = []
    for source in sources:
        for prefix in prefixes:
            if source.startswith(prefix):
                source = source[len(prefix) :]
                break
        normalized.append(source)
    return tuple(normalized)


def _check_optical_api(repository: Repository) -> list[str]:
    failures: list[str] = []
    public = repository.source("include/gdox/optical.h")
    registry = repository.source("src/platform/optical.c")
    runtime = repository.source("src/app/runtime_media.c")

    if not public.declares_c_function("gdox_optical_open_media"):
        failures.append("public optical API omits detected-media opening")
    obsolete = (
        "gdox_optical_open_xgd2",
        "gdox_optical_open_xgd3",
        "gdox_optical_open_xbox_360",
        "gdox_optical_xbox_360_profile",
        "gdox_optical_xbox_360_info",
        "gdox_optical_drive_capabilities",
        "gdox_optical_capabilities",
    )
    for symbol in obsolete:
        if public.has_identifier(symbol) or registry.has_identifier(symbol):
            failures.append(f"optical API restores obsolete parallel surface {symbol}")
    for callback in (
        "gdox_optical_open_gp63_media",
        "gdox_optical_open_asus_nr09_media",
    ):
        if not registry.has_identifier(callback):
            failures.append("optical registry bypasses table-driven media detection")
            break
    if any(runtime.has_identifier(symbol) for symbol in obsolete):
        failures.append("runtime media opening bypasses canonical optical detection")
    if "gdox_optical_open_media" not in runtime.calls:
        failures.append("runtime media omits canonical optical detection")

    implementations = (
        repository.source("src/platform/mt1887_source.c"),
        repository.source("src/platform/asus_nr09_source.c"),
    )
    for symbol in (
        "gdox_mt1887_xgd2_source_open",
        "gdox_mt1887_xgd3_source_open",
        "gdox_asus_nr09_source_open_xgd2",
    ):
        if any(source.has_identifier(symbol) for source in implementations):
            failures.append(
                f"optical implementation restores fixed-profile opener {symbol}"
            )
    return failures


def _check_live_media(repository: Repository) -> list[str]:
    failures: list[str] = []
    live_sources = _normalized_sources(
        repository.cmake.variable("GDOX_LIVE_DISC_SOURCES")
    )
    preservation_sources = _normalized_sources(
        repository.cmake.variable("GDOX_PRESERVATION_MEDIA_SOURCES")
    )
    for required in (
        "src/core/default_xbe_cache_source.c",
        "src/core/file_readahead_source.c",
        "src/core/xdvdfs_directory_cache.c",
        "src/core/xbe_patch_source.c",
    ):
        if required not in live_sources:
            failures.append(f"live-disc graph omits {required}")

    pipeline = repository.source("src/core/live.c")
    stages = (
        "gdox_source_make_partition",
        "gdox_source_make_xdvdfs_directory_cache",
        "gdox_source_make_default_xbe_cache",
        "gdox_source_make_file_readahead",
        "gdox_source_make_xbe_patch_source",
    )
    positions = pipeline.call_positions(stages)
    if any(position < 0 for position in positions) or positions != tuple(
        sorted(positions)
    ):
        failures.append("live-disc source adapters are missing or ordered incorrectly")
    if "src/core/compact.c" in live_sources:
        failures.append("live playback compiles the preservation-only compact builder")
    if "src/core/compact.c" not in preservation_sources:
        failures.append("preservation graph omits the compact-XISO builder")

    public_xdvdfs = repository.source("include/gdox/xdvdfs.h")
    compact = repository.source("src/core/compact.h")
    if public_xdvdfs.has_identifier("gdox_source_make_compact_xiso"):
        failures.append("public live-media API exposes the internal compact builder")
    if not compact.declares_c_function("gdox_source_make_compact_xiso"):
        failures.append("private preservation API omits the compact-XISO builder")

    mt1887 = _normalized_sources(
        repository.cmake.variable("GDOX_MT1887_OPTICAL_SOURCES")
    )
    for required in (
        "src/platform/mmc_commands.c",
        "src/platform/mt1887_media_profile.c",
        "src/platform/mt1887_source.c",
        "src/platform/mt1887_profile.c",
        "src/platform/scsi_transport.c",
        "src/platform/usb_bot_identity.c",
    ):
        if required not in mt1887:
            failures.append(f"shared MT1887 graph omits {required}")
    return failures


def _check_session_policy(repository: Repository) -> list[str]:
    failures: list[str] = []
    policy = repository.source("src/platform/session_storage_policy.c")
    policy_header = repository.source("src/platform/session_storage_policy.h")
    platform_sources = _normalized_sources(
        repository.cmake.target_sources("gdox_platform_services")
    )
    if "src/platform/session_storage_policy.c" not in platform_sources:
        failures.append("platform graph omits shared session-storage policy")
    for function in (
        "gdox_session_relative_path_is_safe",
        "gdox_session_owner_marker_format",
        "gdox_session_recovery_decide",
    ):
        if not policy.defines_c_function(function):
            failures.append("shared session-storage policy is incomplete")
            break
    for name in ("session_storage_posix.c", "session_storage_windows.c"):
        source = repository.source(f"src/platform/{name}")
        if "platform/session_storage_policy.h" not in source.includes:
            failures.append(f"{name} bypasses shared session-storage policy")
        for duplicate in (
            "static bool safe_relative(",
            "GDOX_SESSION_MARKER_PREFIX",
            "typedef enum session_lock_state",
        ):
            if duplicate in source.text:
                failures.append(f"{name} duplicates neutral session-storage policy")
    for forbidden in (
        "windows.h",
        "openat(",
        "CreateFileW(",
        "FILE_ATTRIBUTE_REPARSE_POINT",
        "O_NOFOLLOW",
    ):
        if forbidden in policy.text:
            failures.append(
                "shared session-storage policy contains OS filesystem mechanics"
            )
            break
    for source, limit in ((policy, 120), (policy_header, 60)):
        if source.line_count > limit:
            failures.append(
                f"{source.path.name} exceeds its focused module boundary"
            )
    return failures


def _check_optical_registry(repository: Repository) -> list[str]:
    failures: list[str] = []
    registry = repository.source("src/platform/optical.c")
    header = repository.source("include/gdox/optical.h")
    for required in (
        "static const gdox_optical_driver drivers[]",
        "gdox_usb_bot_observe_all(",
        "gdox_usb_bot_present_all(",
        "gdox_optical_select_presence(",
        "GDOX_OPTICAL_DRIVE_ASUS_NR09",
    ):
        if required not in registry.text:
            failures.append("desktop optical registry is not table-driven")
    legacy = (
        "gdox_optical_observe_gp63",
        "gdox_optical_observe_gp65",
        "gdox_optical_observe_gp08",
        "gdox_optical_observe_asus_nr09",
        "gdox_optical_gp63_connected",
        "gdox_optical_gp65_connected",
        "gdox_optical_gp08_connected",
        "gdox_optical_asus_nr09_connected",
    )
    for function in legacy:
        if function in registry.calls:
            failures.append(
                f"desktop optical registry retains legacy wrapper {function}("
            )
        if header.declares_c_function(function):
            failures.append(f"public optical API retains legacy wrapper {function}(")
    if registry.line_count > 450:
        failures.append("optical.c exceeds its focused module boundary")
    return failures


def _check_mmc_clients(repository: Repository) -> list[str]:
    failures: list[str] = []
    for name, read_command in (
        ("asus_nr09_source.c", "gdox_mmc_read_10"),
        ("gp08_source.c", "gdox_mmc_read_10"),
        ("mt1887_source.c", "gdox_mmc_read_12"),
    ):
        source = repository.source(f"src/platform/{name}")
        for required in (
            "gdox_mmc_inquiry",
            "gdox_mmc_read_capacity_10",
            "gdox_mmc_read_dvd_structure",
            read_command,
        ):
            if required not in source.calls:
                failures.append(f"{name} bypasses shared MMC command {required}(")
        if re.search(
            r"static\s+bool\s+(?:inquiry|read_capacity|read_dvd_structure)\s*\(",
            source.text,
        ):
            failures.append(f"{name} duplicates shared MMC command construction")
    return failures


def check_media(repository: Repository) -> list[str]:
    failures: list[str] = []
    optical_sources = _normalized_sources(
        repository.cmake.target_sources("gdox_optical")
    )
    optical_links = repository.cmake.target_links("gdox_optical")
    if "gdox::media" not in optical_links or "gdox::core" in optical_links:
        failures.append("optical services must depend on the portable media core only")
    for required in (
        "src/platform/asus_nr09_source.c",
        "src/platform/gp08_source.c",
    ):
        if required not in optical_sources:
            failures.append(f"desktop optical graph omits {required}")
    if not repository.cmake.variable("GDOX_MT1887_OPTICAL_SOURCES"):
        failures.append("desktop optical graph omits GDOX_MT1887_OPTICAL_SOURCES")
    for check in (
        _check_optical_api,
        _check_live_media,
        _check_session_policy,
        _check_optical_registry,
        _check_mmc_clients,
    ):
        failures.extend(check(repository))
    return failures
