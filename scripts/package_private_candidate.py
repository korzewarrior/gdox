#!/usr/bin/env python3
"""Create an audited private runtime-candidate archive for hardware tests."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from elf_compatibility import (
    LinuxCompatibilityError,
    validate_release_artifact,
)
from fetch_runtime import fetch as fetch_asset
from fetch_runtime import load_manifest
from linux_bridge_distribution import bundle as bundle_linux_bridge
from package_release import (
    PLATFORMS,
    ROOT,
    RUNTIME_TARGETS,
    add_platform_files,
    copy_documentation,
    payload_inventory,
    run,
    stage_application,
    validate_payload_paths,
    validate_runtime_components,
)
from private_candidate_runtime import (
    bundle_private_candidate_runtime,
    stage_private_xemu_candidate,
)
from project_version import validated_project_version
from release_archive import create_archive, file_sha256
from release_paths import cache_root, output_root

PRIVATE_CANDIDATE_TARGETS = tuple(
    target
    for target, platform_name in PLATFORMS.items()
    if platform_name in {"linux", "steamdeck", "windows"}
)


def candidate_package_name(version: str, target: str) -> str:
    return f"gdox-{version}-{target}-candidate"


def validate_private_candidate_stage(
    target: str,
    platform_name: str,
    stage: Path,
    manifest: dict,
) -> None:
    validate_runtime_components(target, platform_name, stage)
    exact, prefixes = payload_inventory(
        target,
        platform_name,
        manifest,
        runtime_included=True,
    )
    runtime_root = "runtime"
    exact.add(f"{runtime_root}/CANDIDATE.json")
    validate_payload_paths(stage, exact, prefixes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        help="candidate version; defaults to and must match the CMake project",
    )
    parser.add_argument(
        "--target",
        required=True,
        choices=sorted(PRIVATE_CANDIDATE_TARGETS),
    )
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument(
        "--output",
        default=output_root() / "release",
        type=Path,
    )
    parser.add_argument(
        "--candidate-runtime-directory",
        required=True,
        type=Path,
        help="directory containing only the reviewed private Xenia archives",
    )
    parser.add_argument(
        "--candidate-xemu-executable",
        required=True,
        type=Path,
        help="exact reviewed private xemu executable for the selected target",
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

    package_name = candidate_package_name(version, target)
    output_directory = arguments.output.resolve()
    stage = output_directory / package_name
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)

    binary = stage_application(platform_name, artifact, stage)
    copy_documentation(stage)
    runtime = stage / "runtime"
    manifest = load_manifest()
    bundle_private_candidate_runtime(
        RUNTIME_TARGETS.get(target, target),
        runtime,
        cache_root(),
        arguments.candidate_runtime_directory,
    )
    add_platform_files(platform_name, stage)
    stage_private_xemu_candidate(
        arguments.candidate_xemu_executable,
        runtime,
        target,
        manifest,
    )
    bundle_linux_bridge(
        target,
        stage,
        cache_root(),
        manifest,
        fetch_asset,
    )
    validate_private_candidate_stage(
        target,
        platform_name,
        stage,
        manifest,
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
    validate_private_candidate_stage(
        target,
        platform_name,
        stage,
        manifest,
    )

    archive = output_directory / (
        package_name + (".zip" if platform_name == "windows" else ".tar.gz")
    )
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
    checksum = archive.with_name(archive.name + ".sha256")
    checksum.write_text(
        f"{file_sha256(archive)}  {archive.name}\n",
        encoding="utf-8",
    )
    print(archive)
    print(checksum)


if __name__ == "__main__":
    main()
