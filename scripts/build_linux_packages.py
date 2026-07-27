#!/usr/bin/env python3
"""Build Linux and Steam Deck packages in the pinned compatibility image."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from release_paths import cache_root, output_root


ROOT = Path(__file__).resolve().parent.parent
IMAGE = "localhost/gdox-linux-builder:24.04"
LINUX_TARGET = "x86_64-unknown-linux-gnu"
DECK_TARGET = "x86_64-steamdeck-linux-gnu"


def command(arguments: list[str]) -> None:
    print("+", " ".join(arguments), flush=True)
    subprocess.run(arguments, cwd=ROOT, check=True)


def container_runtime() -> str:
    for name in ("podman", "docker"):
        executable = shutil.which(name)
        if executable is not None:
            return executable
    raise SystemExit("Podman or Docker is required for portable Linux packages")


def run_in_builder(runtime: str, arguments: list[str]) -> None:
    output = output_root()
    cache = cache_root()
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    invocation = [
        runtime,
        "run",
        "--rm",
        "--volume",
        f"{ROOT}:/workspace",
        "--volume",
        f"{output}:/output",
        "--volume",
        f"{cache}:/cache",
        "--workdir",
        "/workspace",
        "--env",
        "HOME=/tmp",
        "--env",
        "GDOX_OUTPUT_ROOT=/output",
        "--env",
        "GDOX_CACHE_ROOT=/cache",
    ]
    if Path(runtime).name == "podman":
        invocation.append("--userns=keep-id")
    else:
        invocation.extend(["--user", f"{os.getuid()}:{os.getgid()}"])
    invocation.extend([IMAGE, *arguments])
    command(invocation)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument(
        "--linux-only",
        action="store_true",
        help="skip the Steam Deck archive",
    )
    arguments = parser.parse_args()
    runtime = container_runtime()

    command(
        [
            runtime,
            "build",
            "--file",
            str(ROOT / "packaging" / "linux" / "Containerfile"),
            "--tag",
            IMAGE,
            str(ROOT / "packaging" / "linux"),
        ]
    )
    run_in_builder(
        runtime,
        [
            "python3",
            "scripts/build_release.py",
            "--target",
            LINUX_TARGET,
        ],
    )
    artifact = f"/output/build/{LINUX_TARGET}/gdox"
    targets = [LINUX_TARGET]
    if not arguments.linux_only:
        targets.append(DECK_TARGET)
    for target in targets:
        run_in_builder(
            runtime,
            [
                "python3",
                "scripts/package_release.py",
                "--version",
                arguments.version,
                "--target",
                target,
                "--artifact",
                artifact,
            ],
        )


if __name__ == "__main__":
    main()
