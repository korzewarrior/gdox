#!/usr/bin/env python3
"""Build, test, and audit a GDOX release candidate."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from release_paths import output_root


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT
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


def compiler_flags(build: Path) -> str:
    mappings = (
        f"-ffile-prefix-map={ROOT}=.",
        f"-fdebug-prefix-map={ROOT}=.",
        f"-fmacro-prefix-map={ROOT}=.",
        f"-ffile-prefix-map={build}=build",
        f"-fdebug-prefix-map={build}=build",
        f"-fmacro-prefix-map={build}=build",
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
        if sys.platform != "linux":
            raise SystemExit(f"{target} is the supported Linux-hosted MinGW build")
        c_compiler = require_tool("x86_64-w64-mingw32-gcc")
        cxx_compiler = require_tool("x86_64-w64-mingw32-g++")
        rc_compiler = require_tool("x86_64-w64-mingw32-windres")
        flags = compiler_flags(build)
        arguments.extend(
            [
                "-DCMAKE_SYSTEM_NAME=Windows",
                f"-DCMAKE_C_COMPILER={c_compiler}",
                f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
                f"-DCMAKE_RC_COMPILER={rc_compiler}",
                f"-DCMAKE_C_FLAGS_RELEASE={flags}",
                f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
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


def test_command(target: str, build: Path) -> list[str]:
    if target == "x86_64-pc-windows-gnu" and sys.platform == "linux":
        return [require_tool("wine"), str(build / "gdox_tests.exe")]
    return ["ctest", "--test-dir", str(build), "--output-on-failure"]


def audited_binary(artifact: Path) -> Path:
    if artifact.is_dir():
        return artifact / "Contents" / "MacOS" / "gdox"
    return artifact


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=sorted(TARGETS))
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="build directory (default: ../gdox-output/build/<target>)",
    )
    parser.add_argument(
        "--reuse",
        action="store_true",
        help="reuse an existing CMake tree instead of configuring from empty",
    )
    arguments = parser.parse_args()

    require_tool("cmake")
    require_tool("ninja")
    build = (
        arguments.build_dir
        if arguments.build_dir is not None
        else output_root() / "build" / arguments.target
    ).resolve()
    configure, artifact = configure_arguments(arguments.target, build)
    if not arguments.reuse:
        shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True, exist_ok=True)

    environment = os.environ.copy()
    environment.setdefault("SOURCE_DATE_EPOCH", "0")
    if arguments.target == "x86_64-pc-windows-gnu":
        environment.setdefault("WINEDEBUG", "-all")

    command(configure, environment=environment)
    command(
        ["cmake", "--build", str(build), "--config", "Release", "--parallel"],
        environment=environment,
    )
    command(test_command(arguments.target, build), environment=environment)
    binary = audited_binary(artifact)
    if not binary.is_file():
        raise SystemExit(f"release artifact was not produced: {binary}")
    command(
        [
            sys.executable,
            str(ROOT / "scripts" / "audit_release.py"),
            "--path",
            str(ROOT),
            "--artifact",
            str(binary),
        ],
        environment=environment,
    )
    print(artifact)


if __name__ == "__main__":
    main()
