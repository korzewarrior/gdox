"""Desktop target composition and release-policy architecture checks."""

from __future__ import annotations

from pathlib import Path

from .repository import Repository


def _contains_path(values: tuple[str, ...], path: str) -> bool:
    return any(value == path or value.endswith(f"/{path}") for value in values)


def _check_core_targets(repository: Repository) -> list[str]:
    failures: list[str] = []
    aliases = repository.cmake.aliases()
    expected_aliases = {
        "gdox::media": "gdox_media_core",
        "gdox::platform": "gdox_platform_services",
        "gdox::services": "gdox_desktop_services",
    }
    if any(aliases.get(alias) != target for alias, target in expected_aliases.items()):
        failures.append(
            "desktop graph does not separate portable core and platform services"
        )

    core_sources = repository.cmake.target_sources("gdox_core")
    if not repository.cmake.has_target("gdox_core") or any(
        "src/platform/" in source for source in core_sources
    ):
        failures.append("gdox_core directly compiles platform implementation sources")
    core_links = repository.cmake.target_links("gdox_core")
    if not repository.cmake.has_target_command(
        "target_link_libraries", "gdox_core"
    ) or "gdox::platform" in core_links:
        failures.append("gdox_core target depends outward on platform services")
    services_links = repository.cmake.target_links("gdox_desktop_services")
    if not {"gdox::core", "gdox::platform"}.issubset(services_links):
        failures.append("desktop services do not compose core and platform targets")

    application_sources = repository.cmake.target_sources(
        "gdox_application_support"
    )
    if not application_sources:
        failures.append("desktop application support target is missing")
    elif _contains_path(application_sources, "src/app/runtime_bundle.c"):
        failures.append(
            "production and test runtime-bundle implementations share one object"
        )
    return failures


def _check_runtime_target(repository: Repository) -> list[str]:
    failures: list[str] = []
    aliases = repository.cmake.aliases()
    sources = repository.cmake.target_sources("gdox_desktop_runtime")
    links = repository.cmake.target_links("gdox_desktop_runtime")
    required_sources = (
        "src/app/runtime.c",
        "src/app/runtime_actions.c",
        "src/app/runtime_controls.c",
        "src/app/runtime_media.c",
        "src/app/runtime_playback.c",
        "src/app/runtime_physical.c",
        "src/app/runtime_preservation.c",
        "src/app/runtime_session.c",
        "src/app/runtime_state.c",
        "src/app/runtime_bundle.c",
        "src/app/runtime_xemu.c",
        "src/app/xemu_process_stop.c",
        "src/app/runtime_xenia.c",
    )
    if (
        aliases.get("gdox::runtime") != "gdox_desktop_runtime"
        or any(not _contains_path(sources, source) for source in required_sources)
        or not {"gdox::services", "gdox::optical", "gdox::xenia"}.issubset(links)
    ):
        failures.append("desktop runtime target omits production runtime code")

    modules = {
        name: repository.source(f"src/app/{name}")
        for name in (
            "runtime.c",
            "runtime_actions.c",
            "runtime_physical.c",
            "runtime_session.c",
        )
    }
    coordinator = modules["runtime.c"]
    for token in (
        "gdox_runtime_media_open_",
        "gdox_runtime_plan_request",
        "runtime->xemu",
        "runtime->xenia",
    ):
        found = (
            token in coordinator.text
            if token.endswith("_")
            else coordinator.has_identifier(token)
        )
        if found:
            failures.append(
                f"runtime coordinator bypasses a private module through {token}"
            )
    for name, limit in (
        ("runtime.c", 550),
        ("runtime_actions.c", 400),
        ("runtime_physical.c", 350),
        ("runtime_session.c", 500),
    ):
        if modules[name].line_count > limit:
            failures.append(f"{name} exceeds its focused module boundary")

    physical = modules["runtime_physical.c"]
    if "gdox_optical_connected" not in physical.calls:
        failures.append(
            "active playback presence must use non-commanding optical identity"
        )
    if "gdox_nbd_observe_media" not in physical.calls:
        failures.append(
            "active physical sessions must observe typed owned-source state"
        )
    if "gdox_nbd_media_present" in physical.calls:
        failures.append(
            "active physical sessions must not collapse media state to a boolean"
        )
    return failures


def _check_test_graph(repository: Repository) -> list[str]:
    failures: list[str] = []
    includes = repository.cmake.includes("cmake/GdoxTests.cmake")
    for module in (
        "GdoxTestsCoreUi",
        "GdoxTestsPlatform",
        "GdoxTestsRuntimeOptical",
        "GdoxTestsPackagingPolicy",
    ):
        if module not in includes:
            failures.append(f"desktop test router omits {module}")
    test_sources = repository.cmake.target_sources("gdox_tests")
    test_definitions = repository.cmake.target_compile_definitions("gdox_tests")
    if not _contains_path(
        test_sources, "src/app/runtime_bundle.c"
    ) or "GDOX_RUNTIME_BUNDLE_TESTING=1" not in test_definitions:
        failures.append("runtime-bundle fixture implementation is not test-only")

    helper_sources = repository.cmake.target_sources("gdox_test_xemu_helper")
    required_helper_sources = (
        "tests/test_xemu_helper_capabilities.c",
        "tests/test_xemu_helper_gameplay.c",
        "tests/test_xemu_helper_main.c",
        "tests/test_xemu_helper_save.c",
    )
    if (
        not repository.cmake.has_target("gdox_test_xemu_helper")
        or any(
            not _contains_path(helper_sources, source)
            for source in required_helper_sources
        )
    ):
        failures.append("fake xemu test behavior is not isolated in its helper target")
    if any(_contains_path(test_sources, source) for source in required_helper_sources):
        failures.append("test group runner directly compiles fake xemu behavior")
    if not _contains_path(test_sources, "tests/test_process_support.c"):
        failures.append("test group runner omits shared child-process setup")

    test_main = repository.source("tests/test_main.c")
    if test_main.line_count > 120:
        failures.append("test_main.c exceeds its dispatcher-only boundary")
    for forbidden in (
        "GDOX_XEMU_CAPABILITIES_ARGUMENT",
        "GDOX_TEST_XEMU_CAPABILITY_MODE",
        "GDOX_TEST_XEMU_SAVE_MODE",
        "--gdox-migrate-hdd",
        "--gdox-runtime",
    ):
        if forbidden in test_main.text:
            failures.append("test_main.c owns child-process helper behavior")
            break
    return failures


def _check_xenia_target(repository: Repository) -> list[str]:
    failures: list[str] = []
    aliases = repository.cmake.aliases()
    sources = repository.cmake.target_sources("gdox_xenia_runtime")
    links = repository.cmake.target_links("gdox_xenia_runtime")
    required_sources = (
        "src/core/xenia_launch.c",
        "src/platform/xenia_process_windows.c",
        "src/platform/xenia_runtime_windows.c",
        "src/platform/xenia_process_posix.c",
        "src/platform/xenia_runtime_posix.c",
        "src/platform/xenia_bridge_linux.c",
        "src/platform/xenia_bridge_tools_linux.c",
        "src/platform/xenia_unsupported.c",
    )
    if (
        aliases.get("gdox::xenia") != "gdox_xenia_runtime"
        or any(not _contains_path(sources, source) for source in required_sources)
        or "gdox::platform" not in links
    ):
        failures.append("Xenia runtime is not isolated behind gdox::xenia")
    platform_sources = repository.cmake.target_sources("gdox_platform_services")
    if any("src/platform/xenia_" in source for source in platform_sources):
        failures.append(
            "generic platform services directly compile Xenia runtime sources"
        )
    windows_command = "src/platform/windows_command.c"
    if not _contains_path(platform_sources, windows_command):
        failures.append("Windows process launchers do not share command construction")
    if _contains_path(sources, windows_command):
        failures.append("Xenia target privately duplicates Windows command construction")
    return failures


def _check_elf_policy(repository: Repository) -> list[str]:
    failures: list[str] = []
    policy = repository.source("scripts/elf_compatibility.py").text
    for required in (
        '"x86_64-unknown-linux-gnu"',
        '"x86_64-steamdeck-linux-gnu"',
        "maximum_glibc=(2, 38, 0)",
        "DT_VERNEED",
        "DT_VERNEEDNUM",
    ):
        if required not in policy:
            failures.append("Linux release compatibility policy is incomplete")
            break
    for path in (
        "scripts/build_release.py",
        "scripts/package_release.py",
        "scripts/audit_release.py",
    ):
        if not repository.python(path).calls("validate_release_artifact"):
            failures.append(f"{path} bypasses the Linux ELF compatibility gate")
    return failures


def _check_audit_modules(repository: Repository) -> list[str]:
    failures: list[str] = []
    entrypoint = repository.source("scripts/audit_architecture.py")
    if entrypoint.line_count > 50:
        failures.append("architecture audit CLI exceeds its thin entrypoint boundary")
    audit_root = repository.root / "scripts/architecture_audit"
    for path in sorted(audit_root.glob("*.py")):
        if path.name == "repository.py":
            limit = 400
        else:
            limit = 300
        line_count = len(path.read_text(encoding="utf-8").splitlines())
        if line_count > limit:
            failures.append(
                f"{Path(path).name} exceeds its focused audit module boundary"
            )
    return failures


def check_build(repository: Repository) -> list[str]:
    failures: list[str] = []
    for check in (
        _check_core_targets,
        _check_runtime_target,
        _check_test_graph,
        _check_xenia_target,
        _check_elf_policy,
        _check_audit_modules,
    ):
        failures.extend(check(repository))
    return failures
