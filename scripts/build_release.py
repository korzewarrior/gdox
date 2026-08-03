#!/usr/bin/env python3
"""Build, test, and audit a GDOX release candidate."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from elf_compatibility import (
    LinuxCompatibilityError,
    validate_release_artifact,
)
from release_paths import output_root

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT
ZIP_SAFE_SOURCE_DATE_EPOCH = "315532800"
TARGETS = {
    "x86_64-unknown-linux-gnu": "linux",
    "x86_64-pc-windows-gnu": "windows-gnu",
    "x86_64-pc-windows-msvc": "windows-msvc",
    "x86_64-apple-darwin": "macos-x86_64",
    "aarch64-apple-darwin": "macos-arm64",
}


def command(arguments: list[str], *, environment: dict[str, str]) -> None:
    print("+", " ".join(arguments), flush=True)
    subprocess.run(arguments, cwd=ROOT, env=environment, check=True)


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise SystemExit(f"required release tool is unavailable: {name}")
    return path


def cmake_tool(name: str) -> str:
    return require_tool(name).replace("\\", "/")


def compiler_flags(build: Path) -> str:
    root_text = ROOT.as_posix()
    build_text = build.as_posix()
    mappings = (
        f"-ffile-prefix-map={root_text}=.",
        f"-fdebug-prefix-map={root_text}=.",
        f"-fmacro-prefix-map={root_text}=.",
        f"-ffile-prefix-map={build_text}=build",
        f"-fdebug-prefix-map={build_text}=build",
        f"-fmacro-prefix-map={build_text}=build",
    )
    return "-O2 -DNDEBUG " + " ".join(mappings)


def configure_arguments(
    target: str,
    build: Path,
) -> tuple[list[str], Path]:
    kind = TARGETS[target]
    arguments = [
        "cmake",
        "-S",
        str(SOURCE),
        "-B",
        str(build),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=ON",
        "-DGDOX_BUILD_UI=ON",
        "-DGDOX_BUILD_OPTICAL=ON",
        "-DGDOX_REQUIRE_LIBUSB=ON",
        "-DGDOX_USE_SYSTEM_RAYLIB=OFF",
        "-DGDOX_WARNINGS_AS_ERRORS=ON",
    ]

    if kind == "linux":
        if sys.platform != "linux" or platform.machine() not in {"x86_64", "amd64"}:
            raise SystemExit(f"{target} must be built on x86-64 Linux")
        flags = compiler_flags(build)
        arguments.extend(
            [
                f"-DCMAKE_C_FLAGS_RELEASE={flags}",
                f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
            ]
        )
        artifact = build / "gdox"
    elif kind == "windows-gnu":
        if sys.platform == "linux":
            c_compiler = cmake_tool("x86_64-w64-mingw32-gcc")
            cxx_compiler = cmake_tool("x86_64-w64-mingw32-g++")
            rc_compiler = cmake_tool("x86_64-w64-mingw32-windres")
        elif os.name == "nt":
            c_compiler = cmake_tool("gcc")
            cxx_compiler = cmake_tool("g++")
            rc_compiler = cmake_tool("windres")
        else:
            raise SystemExit(f"{target} must be built on Linux or Windows")
        flags = compiler_flags(build)
        arguments.extend(
            [
                f"-DCMAKE_C_COMPILER={c_compiler}",
                f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
                f"-DCMAKE_RC_COMPILER={rc_compiler}",
                f"-DCMAKE_C_FLAGS_RELEASE={flags}",
                f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
            ]
        )
        if sys.platform == "linux":
            arguments.extend(
                [
                    "-DCMAKE_SYSTEM_NAME=Windows",
                    f"-DCMAKE_CROSSCOMPILING_EMULATOR={require_tool('wine')}",
                ]
            )
        artifact = build / "gdox.exe"
    elif kind == "windows-msvc":
        if os.name != "nt":
            raise SystemExit(f"{target} must be built in an MSVC developer environment")
        flags = (
            f"/O2 /DNDEBUG /Brepro /experimental:deterministic "
            f"/pathmap:{ROOT}=. /pathmap:{build}=build"
        )
        arguments.extend(
            [
                f"-DCMAKE_C_FLAGS_RELEASE={flags}",
                f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
                "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/Brepro",
            ]
        )
        artifact = build / "gdox.exe"
    else:
        if sys.platform != "darwin":
            raise SystemExit(f"{target} must be built on macOS")
        architecture = "x86_64" if kind == "macos-x86_64" else "arm64"
        flags = compiler_flags(build)
        arguments.extend(
            [
                f"-DCMAKE_OSX_ARCHITECTURES={architecture}",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0",
                f"-DCMAKE_C_FLAGS_RELEASE={flags}",
                f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
            ]
        )
        artifact = build / "gdox.app"
    return arguments, artifact


def test_command(
    build: Path,
    target: str,
    *,
    skip_host_neutral: bool,
) -> list[str]:
    # Serialize the GNU Windows process tests so their bounded probe timeouts
    # remain stable on both native Windows and Wine development hosts.
    parallelism = "1" if TARGETS[target] == "windows-gnu" else "4"
    arguments = [
        "ctest",
        "--test-dir",
        str(build),
        "--parallel",
        parallelism,
        "--output-on-failure",
    ]
    if skip_host_neutral:
        arguments.extend(["--label-exclude", "host-neutral"])
    return arguments


def run_tests(
    build: Path,
    target: str,
    *,
    skip_host_neutral: bool,
    environment: dict[str, str],
) -> None:
    arguments = test_command(
        build,
        target,
        skip_host_neutral=skip_host_neutral,
    )
    if TARGETS[target] != "windows-gnu" or os.name == "nt":
        command(arguments, environment=environment)
        return

    wineboot = require_tool("wineboot")
    wineserver = require_tool("wineserver")
    prefix = Path(environment["WINEPREFIX"])
    if prefix.exists() and (not prefix.is_dir() or prefix.is_symlink()):
        raise SystemExit(f"Wine prefix is not an ordinary directory: {prefix}")
    prefix.mkdir(parents=True, exist_ok=True)
    command([wineserver, "--persistent"], environment=environment)
    try:
        command([wineboot, "--init"], environment=environment)
        command(arguments, environment=environment)
    finally:
        print("+", wineserver, "--kill", flush=True)
        subprocess.run(
            [wineserver, "--kill"],
            cwd=ROOT,
            env=environment,
            check=False,
        )


def audited_binary(artifact: Path) -> Path:
    if artifact.is_dir():
        return artifact / "Contents" / "MacOS" / "gdox"
    return artifact


def cmake_tree_reusable(
    build: Path,
    source: Path = SOURCE,
    generator: str = "Ninja",
) -> bool:
    if not build.exists():
        return True
    if not build.is_dir() or build.is_symlink():
        return False
    try:
        populated = any(build.iterdir())
    except OSError:
        return False
    if not populated:
        return True
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return False
    values: dict[str, str] = {}
    try:
        for line in cache.read_text(encoding="utf-8", errors="strict").splitlines():
            key_and_type, separator, value = line.partition("=")
            if not separator:
                continue
            key, type_separator, _ = key_and_type.partition(":")
            if type_separator and key in {
                "CMAKE_CACHEFILE_DIR",
                "CMAKE_GENERATOR",
                "CMAKE_HOME_DIRECTORY",
            }:
                values[key] = value
        return (
            Path(values["CMAKE_HOME_DIRECTORY"]).resolve() == source.resolve()
            and Path(values["CMAKE_CACHEFILE_DIR"]).resolve() == build.resolve()
            and values["CMAKE_GENERATOR"] == generator
        )
    except (KeyError, OSError, UnicodeError):
        return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=sorted(TARGETS))
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="build directory (default: ../gdox-output/build/<target>)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="remove the target build tree before configuring",
    )
    parser.add_argument(
        "--skip-host-neutral-tests",
        action="store_true",
        help="skip packaging and policy tests already run by another CI job",
    )
    arguments = parser.parse_args()

    require_tool("cmake")
    require_tool("ninja")
    build = (
        arguments.build_dir
        if arguments.build_dir is not None
        else output_root() / "build" / arguments.target
    ).resolve()
    if build.exists() and (not build.is_dir() or build.is_symlink()):
        raise SystemExit(f"build path is not an ordinary directory: {build}")
    configure, artifact = configure_arguments(arguments.target, build)
    reuse = cmake_tree_reusable(build)
    if arguments.clean or not reuse:
        if not arguments.clean and build.exists():
            print(
                f"discarding incompatible CMake build tree: {build}",
                flush=True,
            )
        shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True, exist_ok=True)

    environment = os.environ.copy()
    environment["SOURCE_DATE_EPOCH"] = ZIP_SAFE_SOURCE_DATE_EPOCH
    if arguments.target == "x86_64-pc-windows-gnu" and sys.platform == "linux":
        environment.setdefault("WINEDEBUG", "-all")
        environment.setdefault("WINEARCH", "win64")
        environment.setdefault("WINEPREFIX", str(build / ".wine"))
        environment.setdefault("WINEDLLOVERRIDES", "mscoree,mshtml=")

    command(configure, environment=environment)
    command(
        ["cmake", "--build", str(build), "--config", "Release", "--parallel"],
        environment=environment,
    )
    run_tests(
        build,
        arguments.target,
        skip_host_neutral=arguments.skip_host_neutral_tests,
        environment=environment,
    )
    binary = audited_binary(artifact)
    if not binary.is_file():
        raise SystemExit(f"release artifact was not produced: {binary}")
    try:
        validate_release_artifact(arguments.target, binary)
    except LinuxCompatibilityError as error:
        raise SystemExit(str(error)) from error
    command(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--path",
            str(ROOT),
            "--artifact",
            str(binary),
            "--target",
            arguments.target,
        ],
        environment=environment,
    )
    print(artifact)


if __name__ == "__main__":
    main()
