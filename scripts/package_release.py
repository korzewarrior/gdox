#!/usr/bin/env python3
"""Create a self-contained, audited GDOX release archive."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from release_archive import copy_file, create_archive
from release_paths import output_root


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
    "CATALOG.md",
    "PRESERVATION.md",
    "SAFETY.md",
    "TROUBLESHOOTING.md",
    "USER_GUIDE.md",
)
SCHEMAS = (
    "preservation-manifest-v2.json",
    "security-map-v1.json",
    "xgd1-catalog-v1.json",
)
DEPENDENCY_LICENSES = (
    "glfw-LICENSE.txt",
    "imgui-LICENSE.txt",
    "nfd-LICENSE.txt",
    "raylib-LICENSE.txt",
    "rlimgui-LICENSE.txt",
)


def run(arguments: list[str]) -> None:
    print("+", " ".join(arguments), flush=True)
    subprocess.run(arguments, cwd=ROOT, check=True)


def copy_documentation(stage: Path) -> None:
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "CHANGELOG.md"):
        copy_file(ROOT / name, stage / name)
    for name in DOCUMENTS:
        copy_file(ROOT / "docs" / name, stage / "docs" / name)
    for name in SCHEMAS:
        copy_file(
            ROOT / "docs" / "schemas" / name,
            stage / "docs" / "schemas" / name,
        )
    for name in DEPENDENCY_LICENSES:
        copy_file(
            ROOT / "packaging" / "licenses" / name,
            stage / "licenses" / name,
        )
    copy_file(ROOT / "catalog" / "xgd1.json", stage / "catalog" / "xgd1.json")


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
        _bundle_linux_libusb(artifact, stage)
        return binary
    binary = stage / "gdox.exe"
    copy_file(artifact, binary, executable=True)
    return binary


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


def fetch_runtime(target: str, destination: Path) -> None:
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
            ROOT / "packaging" / "gdox.svg",
            stage / "packaging" / "gdox.svg",
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
        libusb_license = stage / "runtime" / "xemu" / "licenses" / "COPYING.LIB"
        if libusb_license.is_file():
            copy_file(
                libusb_license,
                stage / "licenses" / "libusb-LGPL-2.1.txt",
            )


def validate_stage(platform_name: str, stage: Path) -> None:
    required = [stage / "README-FIRST.md", stage / "LICENSE"]
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
                )
            )
        if platform_name == "steamdeck":
            required.extend(
                (
                    stage / "packaging" / "steamdeck-artwork.py",
                    stage / "packaging" / "steam-artwork" / "hero.png",
                    stage / "packaging" / "steam-artwork" / "portrait.png",
                )
            )
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit("release package is incomplete:\n  " + "\n  ".join(missing))


def sign_macos(application: Path) -> None:
    if sys.platform != "darwin":
        return
    identity = os.environ.get("GDOX_CODESIGN_IDENTITY", "-")
    signing = ["codesign", "--force", "--deep", "--sign", identity]
    if identity != "-":
        signing.extend(["--options", "runtime", "--timestamp"])
    signing.append(str(application))
    run(signing)
    run(["codesign", "--verify", "--deep", "--strict", str(application)])


def semantic_version(value: str) -> str:
    version = value.removeprefix("v")
    if re.fullmatch(
        r"[0-9]+(?:\.[0-9]+){2}(?:[-+][0-9A-Za-z.-]+)?",
        version,
    ) is None:
        raise SystemExit("version must resemble 0.1.0 or 0.1.0-rc.1")
    return version


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
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

    version = semantic_version(arguments.version)
    target = arguments.target
    platform_name = PLATFORMS[target]
    package_name = f"gdox-{version}-{target}"
    output_directory = arguments.output.resolve()
    stage = output_directory / package_name
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)

    binary = stage_application(
        platform_name,
        arguments.artifact.resolve(),
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
        validate_stage(platform_name, stage)
    run(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--path",
            str(stage),
            "--artifact",
            str(binary),
        ]
    )
    if platform_name == "macos":
        sign_macos(stage / "GDOX.app")

    suffix = ".zip" if platform_name == "windows" else ".tar.gz"
    archive = output_directory / f"{package_name}{suffix}"
    archive.unlink(missing_ok=True)
    create_archive(stage, archive, windows=platform_name == "windows")
    run(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--artifact",
            str(archive),
        ]
    )
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = archive.with_name(archive.name + ".sha256")
    checksum.write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    print(archive)
    print(checksum)


if __name__ == "__main__":
    main()
