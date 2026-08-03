#!/usr/bin/env python3
"""Create a self-contained, audited GDOX release archive."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from elf_compatibility import (
    LinuxCompatibilityError,
    validate_release_artifact,
)
from fetch_runtime import fetch as fetch_asset
from fetch_runtime import load_manifest
from linux_bridge_distribution import (
    bundle as bundle_linux_bridge,
)
from linux_bridge_distribution import (
    validate_stage as validate_linux_bridge_stage,
)
from project_version import validated_project_version
from release_archive import copy_file, create_archive, file_sha256
from release_paths import cache_root, output_root
from xenia_distribution import validate_stage as validate_xenia_stage

ROOT = Path(__file__).resolve().parent.parent
PLATFORMS = {
    "x86_64-unknown-linux-gnu": "linux",
    "x86_64-steamdeck-linux-gnu": "steamdeck",
    "x86_64-pc-windows-gnu": "windows",
    "x86_64-pc-windows-msvc": "windows",
    "x86_64-apple-darwin": "macos",
    "aarch64-apple-darwin": "macos",
}
RUNTIME_TARGETS = {
    "x86_64-steamdeck-linux-gnu": "x86_64-unknown-linux-gnu",
    "x86_64-pc-windows-gnu": "x86_64-pc-windows-msvc",
}
DOCUMENTS = (
    "ASUS_NR09.md",
    "PRESERVATION.md",
    "SAFETY.md",
    "TROUBLESHOOTING.md",
    "USER_GUIDE.md",
    "XBOX360.md",
)
DEPENDENCY_LICENSES = (
    "glfw-LICENSE.txt",
    "imgui-LICENSE.txt",
    "libusb-LGPL-2.1.txt",
    "nfd-LICENSE.txt",
    "raylib-LICENSE.txt",
    "rlimgui-LICENSE.txt",
)


def run(arguments: list[str]) -> None:
    print("+", " ".join(arguments), flush=True)
    subprocess.run(arguments, cwd=ROOT, check=True)


def is_link_or_reparse_point(path: Path) -> bool:
    try:
        if path.is_symlink():
            return True
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
        reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
        return bool(reparse_flag and attributes & reparse_flag)
    except OSError:
        return False


def is_contained_regular_file(application: Path, path: Path) -> bool:
    try:
        if is_link_or_reparse_point(application) or not application.is_dir():
            return False
        relative = path.relative_to(application)
        component = application
        for part in relative.parts:
            if not any(entry.name == part for entry in component.iterdir()):
                return False
            component /= part
            if is_link_or_reparse_point(component):
                return False
        return (
            path.is_file()
            and path.resolve(strict=True).is_relative_to(
                application.resolve(strict=True)
            )
        )
    except (OSError, ValueError):
        return False


def copy_documentation(stage: Path) -> None:
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md"):
        copy_file(ROOT / name, stage / name)
    for name in DOCUMENTS:
        copy_file(ROOT / "docs" / name, stage / "docs" / name)
    for name in DEPENDENCY_LICENSES:
        copy_file(
            ROOT / "packaging" / "licenses" / name,
            stage / "licenses" / name,
        )


def release_package_name(
    version: str,
    target: str,
    *,
    without_runtime: bool,
) -> str:
    name = f"gdox-{version}-{target}"
    if without_runtime:
        return name + "-developer-no-runtime"
    return name


def stage_application(platform_name: str, artifact: Path, stage: Path) -> Path:
    if platform_name == "macos":
        application = stage / "GDOX.app"
        if not artifact.is_dir():
            raise SystemExit(f"macOS application bundle is unavailable: {artifact}")
        shutil.copytree(artifact, application, symlinks=True)
        return application / "Contents" / "MacOS" / "gdox"

    if not artifact.is_file():
        raise SystemExit(f"release binary is unavailable: {artifact}")
    if platform_name in {"linux", "steamdeck"}:
        binary = stage / "libexec" / "gdox"
        copy_file(artifact, binary, executable=True)
        copy_file(ROOT / "packaging" / "linux" / "gdox", stage / "gdox", executable=True)
        _set_linux_host_profile(
            stage / "gdox",
            "handheld" if platform_name == "steamdeck" else "desktop",
        )
        _bundle_linux_libusb(artifact, stage)
        return binary
    binary = stage / "gdox.exe"
    copy_file(artifact, binary, executable=True)
    return binary


def _set_linux_host_profile(launcher: Path, profile: str) -> None:
    desktop = "export GDOX_HOST_PROFILE=desktop"
    replacement = f"export GDOX_HOST_PROFILE={profile}"
    if profile not in {"desktop", "handheld"}:
        raise SystemExit("Linux package host profile is invalid")
    source = launcher.read_text(encoding="utf-8")

    if source.count(desktop) != 1:
        raise SystemExit("Linux launcher host profile marker is invalid")
    launcher.write_text(source.replace(desktop, replacement), encoding="utf-8")


def _bundle_linux_libusb(artifact: Path, stage: Path) -> None:
    completed = subprocess.run(
        ["ldd", str(artifact)],
        check=True,
        capture_output=True,
        text=True,
    )
    for line in completed.stdout.splitlines():
        fields = line.strip().split()
        if len(fields) >= 3 and fields[:2] == ["libusb-1.0.so.0", "=>"]:
            library = Path(fields[2])
            if library.is_file():
                copy_file(library, stage / "lib" / "libusb-1.0.so.0")
                return
    raise SystemExit("Linux release binary does not link libusb-1.0.so.0")


def fetch_runtime(
    target: str,
    destination: Path,
) -> None:
    run(
        [
            sys.executable,
            str(ROOT / "scripts" / "fetch_runtime.py"),
            "bundle",
            "--target",
            RUNTIME_TARGETS.get(target, target),
            "--destination",
            str(destination),
        ]
    )


def add_platform_files(platform_name: str, stage: Path) -> None:
    readme_platform = "steamdeck" if platform_name == "steamdeck" else platform_name
    copy_file(
        ROOT / "packaging" / readme_platform / "README-FIRST.md",
        stage / "README-FIRST.md",
    )
    if platform_name in {"linux", "steamdeck"}:
        copy_file(
            ROOT / "packaging" / "install-device-access.sh",
            stage / "setup-device-access.sh",
            executable=True,
        )
        copy_file(
            ROOT / "packaging" / "60-gdox.rules",
            stage / "packaging" / "60-gdox.rules",
        )
        copy_file(
            ROOT / "packaging" / "org.gdox.gdox.desktop",
            stage / "packaging" / "org.gdox.gdox.desktop",
        )
        copy_file(
            ROOT / "packaging" / "steamdeck" / "artwork" / "icon.png",
            stage / "packaging" / "gdox.png",
        )
        if platform_name == "steamdeck":
            copy_file(
                ROOT / "packaging" / "steamdeck" / "install-artwork.py",
                stage / "packaging" / "steamdeck-artwork.py",
                executable=True,
            )
            artwork = ROOT / "packaging" / "steamdeck" / "artwork"
            for name in (
                "grid.png",
                "portrait.png",
                "hero.png",
                "logo.png",
                "icon.png",
            ):
                copy_file(
                    artwork / name,
                    stage / "packaging" / "steam-artwork" / name,
                )
        installer = ROOT / "packaging" / "linux" / "install.sh"
        copy_file(installer, stage / "install.sh", executable=True)
        copy_file(
            ROOT / "packaging" / "linux" / "xemu",
            stage / "runtime" / "xemu" / "xemu",
            executable=True,
        )
        if platform_name == "steamdeck":
            copy_file(
                ROOT / "packaging" / "linux" / "nbdfuse",
                stage / "libexec" / "nbdfuse",
                executable=True,
            )


def payload_inventory(
    target: str,
    platform_name: str,
    manifest: dict | None,
    *,
    runtime_included: bool,
) -> tuple[set[str], tuple[str, ...]]:
    exact = {
        "CHANGELOG.md",
        "LICENSE",
        "README-FIRST.md",
        "THIRD_PARTY_NOTICES.md",
        *(f"docs/{name}" for name in DOCUMENTS),
        *(f"licenses/{name}" for name in DEPENDENCY_LICENSES),
    }
    prefixes: list[str] = []
    if platform_name == "macos":
        exact.update(
            {
                "GDOX.app/Contents/Info.plist",
                "GDOX.app/Contents/MacOS/gdox",
                "GDOX.app/Contents/Resources/GDOX.icns",
                "GDOX.app/Contents/_CodeSignature/CodeResources",
            }
        )
        runtime_root = "GDOX.app/Contents/Resources/runtime"
    elif platform_name == "windows":
        exact.add("gdox.exe")
        runtime_root = "runtime"
    else:
        exact.update(
            {
                "gdox",
                "install.sh",
                "lib/libusb-1.0.so.0",
                "libexec/gdox",
                "packaging/60-gdox.rules",
                "packaging/gdox.png",
                "packaging/org.gdox.gdox.desktop",
                "setup-device-access.sh",
            }
        )
        runtime_root = "runtime"
        if platform_name == "steamdeck":
            exact.update(
                {
                    "libexec/nbdfuse",
                    "libexec/nbdfuse.bin",
                    "packaging/steamdeck-artwork.py",
                    *(
                        f"packaging/steam-artwork/{name}"
                        for name in (
                            "grid.png",
                            "portrait.png",
                            "hero.png",
                            "logo.png",
                            "icon.png",
                        )
                    ),
                }
            )
    if not runtime_included:
        return exact, tuple(prefixes)
    if manifest is None:
        raise SystemExit("release payload validation requires the runtime manifest")

    hdd = manifest["hdd"]
    exact.update(
        {
            f"{runtime_root}/SOURCE.md",
            f"{runtime_root}/VERSIONS.json",
            f"{runtime_root}/hdd/{hdd['name']}",
            f"{runtime_root}/hdd/{hdd['license']['name']}",
        }
    )
    prefixes.append(f"{runtime_root}/xemu/")
    runtime_target = RUNTIME_TARGETS.get(target, target)
    if any(
        definition["targets"].get(runtime_target) is not None
        for definition in manifest["xenia"]["revisions"].values()
    ):
        prefixes.append(f"{runtime_root}/xenia/")
    if platform_name == "steamdeck":
        prefixes.append(f"{runtime_root}/bridge/")
    return exact, tuple(prefixes)


def validate_payload_paths(
    stage: Path,
    exact: set[str],
    prefixes: tuple[str, ...],
) -> None:
    missing = [
        relative
        for relative in sorted(exact)
        if not is_contained_regular_file(stage, stage / relative)
    ]
    if missing:
        raise SystemExit(
            "release package is missing required files:\n  "
            + "\n  ".join(missing)
        )

    unexpected = []
    for path in stage.rglob("*"):
        if not (path.is_file() or is_link_or_reparse_point(path)):
            continue
        relative = path.relative_to(stage).as_posix()
        if relative not in exact and not any(
            relative.startswith(prefix) for prefix in prefixes
        ):
            unexpected.append(relative)
    if unexpected:
        raise SystemExit(
            "release package contains unexpected files:\n  "
            + "\n  ".join(sorted(unexpected))
        )


def validate_payload_inventory(
    target: str,
    platform_name: str,
    stage: Path,
    manifest: dict | None,
    *,
    runtime_included: bool,
) -> None:
    exact, prefixes = payload_inventory(
        target,
        platform_name,
        manifest,
        runtime_included=runtime_included,
    )
    validate_payload_paths(stage, exact, prefixes)


def validate_runtime_components(
    target: str,
    platform_name: str,
    stage: Path,
) -> None:
    runtime_root = (
        stage / "GDOX.app" / "Contents" / "Resources" / "runtime"
        if platform_name == "macos"
        else stage / "runtime"
    )
    required = [
        stage / "README-FIRST.md",
        stage / "LICENSE",
        runtime_root / "SOURCE.md",
        runtime_root / "VERSIONS.json",
    ]
    if platform_name == "macos":
        required.extend(
            (
                stage / "GDOX.app" / "Contents" / "MacOS" / "gdox",
                stage
                / "GDOX.app"
                / "Contents"
                / "Resources"
                / "runtime"
                / "xemu"
                / "xemu.app"
                / "Contents"
                / "MacOS"
                / "xemu",
                stage
                / "GDOX.app"
                / "Contents"
                / "Resources"
                / "runtime"
                / "hdd"
                / "xbox_hdd.qcow2",
            )
        )
    else:
        required.extend(
            (
                stage / ("gdox.exe" if platform_name == "windows" else "gdox"),
                stage
                / "runtime"
                / "xemu"
                / ("xemu.exe" if platform_name == "windows" else "AppDir/AppRun"),
                stage / "runtime" / "hdd" / "xbox_hdd.qcow2",
            )
        )
        if platform_name in {"linux", "steamdeck"}:
            required.extend(
                (
                    stage / "libexec" / "gdox",
                    stage / "lib" / "libusb-1.0.so.0",
                    stage / "install.sh",
                    stage / "runtime" / "xemu" / "xemu",
                )
            )
        if platform_name == "steamdeck":
            required.extend(
                (
                    stage / "packaging" / "steamdeck-artwork.py",
                    stage / "packaging" / "steam-artwork" / "hero.png",
                    stage / "packaging" / "steam-artwork" / "portrait.png",
                    stage / "libexec" / "nbdfuse",
                )
            )
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit("release package is incomplete:\n  " + "\n  ".join(missing))
    manifest = load_manifest()
    validate_xenia_stage(
        RUNTIME_TARGETS.get(target, target),
        runtime_root,
        manifest,
    )
    validate_linux_bridge_stage(target, stage, manifest)
    forbidden_roots = {"research", "site", "website", "workspace"}
    leaked = [
        str(path)
        for path in stage.rglob("*")
        if any(
            part.lower() in forbidden_roots
            for part in path.relative_to(stage).parts
        )
    ]
    if leaked:
        raise SystemExit(
            "development-only content entered the release package:\n  "
            + "\n  ".join(leaked)
        )


def validate_stage(target: str, platform_name: str, stage: Path) -> None:
    validate_runtime_components(target, platform_name, stage)
    validate_payload_inventory(
        target,
        platform_name,
        stage,
        load_manifest(),
        runtime_included=True,
    )


def verify_macos_embedded_runtime(
    application: Path,
    manifest: dict,
) -> tuple[tuple[Path, int, str], ...]:
    resources = application / "Contents" / "Resources"
    verified: list[tuple[Path, int, str]] = []
    nested_xemu_found = False
    for definition in manifest["xemu"]["embedded_build_path_files"]:
        path = resources / definition["member"]
        if not path.exists():
            continue
        if (
            not is_contained_regular_file(application, path)
            or path.stat().st_size != definition["size"]
            or file_sha256(path) != definition["sha256"]
        ):
            raise SystemExit(
                f"embedded runtime changed before macOS packaging: {path}"
            )
        verified.append((path, definition["size"], definition["sha256"]))
        nested_xemu_found = nested_xemu_found or definition["member"].endswith(
            "xemu.app/Contents/MacOS/xemu"
        )
    if not nested_xemu_found:
        raise SystemExit("the pinned macOS xemu executable is missing")
    return tuple(verified)


def macos_signing_commands(
    application: Path,
    identity: str,
) -> tuple[list[str], list[str]]:
    options = ["--options", "runtime", "--timestamp"] \
        if identity != "-" else []
    executable = application / "Contents" / "MacOS" / "gdox"
    if not is_contained_regular_file(application, executable):
        raise SystemExit(f"macOS GDOX executable is missing: {executable}")
    return (
        ["codesign", "--force", "--sign", identity, *options, str(executable)],
        ["codesign", "--force", "--sign", identity, *options, str(application)],
    )


def sign_macos(application: Path, manifest: dict | None) -> None:
    if sys.platform != "darwin":
        return
    expected_runtime = (
        verify_macos_embedded_runtime(application, manifest)
        if manifest is not None
        else None
    )
    identity = os.environ.get("GDOX_CODESIGN_IDENTITY", "-")
    for command in macos_signing_commands(application, identity):
        run(command)
    run(["codesign", "--verify", "--deep", "--strict", str(application)])
    if (
        manifest is not None
        and verify_macos_embedded_runtime(application, manifest)
        != expected_runtime
    ):
        raise SystemExit("macOS signing changed the pinned xemu runtime")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        help="release version; defaults to and must match the CMake project",
    )
    parser.add_argument("--target", required=True, choices=sorted(PLATFORMS))
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument(
        "--output",
        default=output_root() / "release",
        type=Path,
    )
    parser.add_argument(
        "--without-runtime",
        action="store_true",
        help="make a developer-only package without xemu or the blank HDD",
    )
    arguments = parser.parse_args()

    try:
        version = validated_project_version(arguments.version)
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    target = arguments.target
    platform_name = PLATFORMS[target]
    artifact = arguments.artifact.resolve()
    try:
        validate_release_artifact(target, artifact)
    except LinuxCompatibilityError as error:
        parser.error(str(error))
    package_name = release_package_name(
        version,
        target,
        without_runtime=arguments.without_runtime,
    )
    output_directory = arguments.output.resolve()
    stage = output_directory / package_name
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)

    binary = stage_application(
        platform_name,
        artifact,
        stage,
    )
    copy_documentation(stage)
    runtime = (
        stage / "GDOX.app" / "Contents" / "Resources" / "runtime"
        if platform_name == "macos"
        else stage / "runtime"
    )
    if not arguments.without_runtime:
        fetch_runtime(target, runtime)
    add_platform_files(platform_name, stage)
    if not arguments.without_runtime:
        manifest = load_manifest()
        bundle_linux_bridge(
            target,
            stage,
            cache_root(),
            manifest,
            fetch_asset,
        )
    if platform_name == "macos":
        sign_macos(
            stage / "GDOX.app",
            None if arguments.without_runtime else load_manifest(),
        )
    if not arguments.without_runtime:
        validate_stage(target, platform_name, stage)
    else:
        validate_payload_inventory(
            target,
            platform_name,
            stage,
            None,
            runtime_included=False,
        )
    run(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--path",
            str(stage),
            "--artifact",
            str(binary),
            "--target",
            target,
        ]
    )
    validate_payload_inventory(
        target,
        platform_name,
        stage,
        None if arguments.without_runtime else load_manifest(),
        runtime_included=not arguments.without_runtime,
    )

    suffix = ".zip" if platform_name == "windows" else ".tar.gz"
    archive = output_directory / f"{package_name}{suffix}"
    archive.unlink(missing_ok=True)
    create_archive(stage, archive, windows=platform_name == "windows")
    if not arguments.without_runtime:
        run(
            [
                sys.executable,
                str(ROOT / "scripts" / "audit_release.py"),
                "--artifact",
                str(archive),
            ]
        )
    digest = file_sha256(archive)
    checksum = archive.with_name(archive.name + ".sha256")
    checksum.write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    print(archive)
    print(checksum)


if __name__ == "__main__":
    main()
