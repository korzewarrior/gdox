#!/usr/bin/env python3
"""Verify and stage private emulator candidates for hardware testing."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
from fetch_runtime import fetch, load_manifest, verify
from release_archive import copy_file, file_sha256
from release_paths import cache_root
from runtime_bundle_distribution import stage_runtime_bundle
from xenia_distribution import bundle as bundle_xenia

ROOT = Path(__file__).resolve().parent.parent


def bundle_private_candidate_runtime(
    target: str,
    destination: Path,
    cache: Path,
    candidate_directory: Path,
) -> None:
    manifest = load_manifest()
    candidate_directory = candidate_directory.resolve()
    expected_candidates = {
        asset["archive_name"]
        for definition in manifest["xenia"]["revisions"].values()
        if (asset := definition["targets"].get(target)) is not None
        and asset["release_state"] == "candidate-only"
    }
    if not expected_candidates:
        raise SystemExit(f"no candidate Xenia archives are defined for {target}")
    if not candidate_directory.is_dir() or {
        path.name for path in candidate_directory.iterdir()
    } != expected_candidates:
        raise SystemExit(
            "candidate directory must contain only the exact reviewed archives: "
            + ", ".join(sorted(expected_candidates))
        )

    def candidate_xenia_fetch(asset: dict, runtime_cache: Path) -> Path:
        if asset.get("release_state") != "candidate-only":
            return fetch(asset, runtime_cache)
        archive = candidate_directory / asset["archive_name"]
        if not verify(archive, asset):
            raise SystemExit(
                f"candidate archive failed verification: {asset['archive_name']}"
            )
        return archive

    def bundle_candidate_xenia(
        runtime_target: str,
        runtime_destination: Path,
        runtime_cache: Path,
        runtime_manifest: dict,
    ) -> int:
        return bundle_xenia(
            runtime_target,
            runtime_destination,
            runtime_cache,
            runtime_manifest,
            candidate_xenia_fetch,
            allow_candidate=True,
        )

    stage_runtime_bundle(
        target,
        destination,
        cache,
        manifest,
        fetch,
        bundle_candidate_xenia,
    )


def stage_private_xemu_candidate(
    executable: Path,
    runtime: Path,
    target: str,
    manifest: dict,
) -> None:
    linux_targets = {
        "x86_64-unknown-linux-gnu",
        "x86_64-steamdeck-linux-gnu",
    }
    windows_targets = {
        "x86_64-pc-windows-gnu",
        "x86_64-pc-windows-msvc",
    }
    if target in linux_targets:
        member = "runtime/xemu/AppDir/usr/bin/xemu"
        destination = runtime / "xemu" / "AppDir" / "usr" / "bin" / "xemu"
        launcher = runtime / "xemu" / "xemu"
    elif target in windows_targets:
        member = "runtime/xemu/xemu.exe"
        destination = runtime / "xemu" / "xemu.exe"
        launcher = destination
    else:
        raise SystemExit("private xemu candidate packaging is unsupported for target")
    if executable.is_symlink():
        raise SystemExit("the private xemu candidate must be a regular file")
    executable = executable.resolve()
    if not executable.is_file():
        raise SystemExit("the private xemu candidate must be a regular file")
    reviewed = next(
        (
            item
            for item in manifest["xemu"]["embedded_privacy_files"]
            if item["member"] == member
        ),
        None,
    )
    if reviewed is None or (
        executable.stat().st_size != reviewed["size"]
        or file_sha256(executable) != reviewed["sha256"]
    ):
        raise SystemExit("the private xemu candidate is not the reviewed binary")

    source = runtime / "SOURCE.md"
    if source.is_symlink() or not source.is_file():
        raise SystemExit("the runtime xemu source notice is invalid")
    source_text = source.read_text(encoding="utf-8")
    marker = "contains the reviewed GDOX-patched xemu"
    if source_text.count(marker) != 1:
        raise SystemExit("the runtime xemu source notice is invalid")

    if destination.is_symlink() or not destination.is_file():
        raise SystemExit("the bundled xemu executable is not a regular file")
    copy_file(executable, destination, executable=True)
    if (
        destination.stat().st_size != reviewed["size"]
        or file_sha256(destination) != reviewed["sha256"]
    ):
        raise SystemExit("the staged private xemu candidate changed during copy")
    integration = json.loads(
        (ROOT / "packaging" / "xemu" / "integration.json").read_text(
            encoding="utf-8"
        )
    )
    expected = integration["capability_query"]["response"]
    with tempfile.TemporaryDirectory(prefix="gdox-xemu-candidate-") as temporary:
        isolated = Path(temporary)
        observed = isolated
        if target in linux_targets:
            environment = {
                "HOME": str(isolated / "home"),
                "XDG_CACHE_HOME": str(isolated / "cache"),
                "XDG_CONFIG_HOME": str(isolated / "config"),
                "XDG_DATA_HOME": str(isolated / "data"),
                "XDG_STATE_HOME": str(isolated / "state"),
                "TMPDIR": str(isolated / "tmp"),
            }
            command = [
                str(launcher),
                integration["capability_query"]["argument"],
            ]
        else:
            observed = isolated / "observed"
            observed.mkdir()
            environment = os.environ.copy() if os.name == "nt" else {}
            environment.update(
                {
                    "APPDATA": str(observed / "appdata"),
                    "LOCALAPPDATA": str(observed / "localappdata"),
                    "USERPROFILE": str(observed / "profile"),
                    "TEMP": str(observed / "temp"),
                    "TMP": str(observed / "tmp"),
                }
            )
            if os.name == "nt":
                command = [
                    str(launcher),
                    integration["capability_query"]["argument"],
                ]
            else:
                wine = shutil.which("wine")
                wineboot = shutil.which("wineboot")
                if wine is None or wineboot is None:
                    raise SystemExit(
                        "Wine is required to capability-probe a Windows xemu candidate"
                    )
                wine_prefix = isolated / "wine"
                xdg_runtime = isolated / "xdg-runtime"
                xdg_runtime.mkdir(mode=0o700)
                environment.update(
                    {
                        "HOME": str(isolated / "wine-home"),
                        "WINEDEBUG": "-all",
                        "WINEPREFIX": str(wine_prefix),
                        "XDG_RUNTIME_DIR": str(xdg_runtime),
                    }
                )
                try:
                    prepared = subprocess.run(
                        [wineboot, "--init"],
                        cwd=isolated,
                        env=environment,
                        check=False,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        timeout=60,
                    )
                except (OSError, subprocess.TimeoutExpired) as error:
                    raise SystemExit(
                        "the Windows xemu probe environment could not be prepared"
                    ) from error
                if prepared.returncode != 0:
                    raise SystemExit(
                        "the Windows xemu probe environment could not be prepared"
                    )
                command = [
                    wine,
                    str(launcher),
                    integration["capability_query"]["argument"],
                ]
        try:
            completed = subprocess.run(
                command,
                cwd=observed,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
                timeout=15,
            )
        except (OSError, subprocess.TimeoutExpired, UnicodeError) as error:
            raise SystemExit(
                "the private xemu candidate could not be capability-probed"
            ) from error
        try:
            response = json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            raise SystemExit(
                "the private xemu candidate returned invalid capabilities"
            ) from error
        if (
            completed.returncode != 0
            or completed.stderr
            or response != expected
            or any(observed.iterdir())
        ):
            raise SystemExit(
                "the private xemu candidate failed its isolated capability probe"
            )

    metadata = {
        "schema": 1,
        "release_state": "candidate-only",
        "xemu": {
            "version": manifest["xemu"]["version"],
            "executable": member,
            "size": reviewed["size"],
            "sha256": reviewed["sha256"],
            "capabilities": expected,
            "integration_patches": integration["patches"],
        },
    }
    source.write_text(
        source_text.replace(
            marker,
            "contains a reviewed private GDOX-patched xemu",
            1,
        ),
        encoding="utf-8",
    )
    (runtime / "CANDIDATE.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cache",
        default=cache_root(),
        type=Path,
        help="verified download cache",
    )
    commands = parser.add_subparsers(dest="command", required=True)
    bundle_parser = commands.add_parser("bundle")
    bundle_parser.add_argument("--target", required=True)
    bundle_parser.add_argument("--destination", required=True, type=Path)
    bundle_parser.add_argument(
        "--candidate-directory",
        required=True,
        type=Path,
    )
    arguments = parser.parse_args()
    bundle_private_candidate_runtime(
        arguments.target,
        arguments.destination,
        arguments.cache.resolve(),
        arguments.candidate_directory,
    )


if __name__ == "__main__":
    main()
