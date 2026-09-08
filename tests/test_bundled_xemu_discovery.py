from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def executable_fixture(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"discovery fixture; never executed\n")
    path.chmod(0o700)


def runtime_fixture(root: Path, windows: bool) -> Path:
    suffix = (
        "xemu/xemu.exe" if windows else
        "xemu/xemu.app/Contents/MacOS/xemu" if sys.platform == "darwin" else
        "xemu/xemu"
    )
    executable = root / suffix
    executable_fixture(executable)
    if not windows and sys.platform != "darwin":
        executable_fixture(root / "xemu/AppDir/AppRun")
    return executable


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--scratch-root", required=True, type=Path)
    parser.add_argument("--runner", default="")
    args = parser.parse_args()
    windows = args.probe.suffix.lower() == ".exe"
    with tempfile.TemporaryDirectory(
        prefix="gdox-bundled-discovery-", dir=args.scratch_root
    ) as temporary:
        root = Path(temporary).resolve()
        original = root / "original"
        module_directory = original
        if sys.platform == "darwin" and not windows:
            module_directory = original / "GDOX.app/Contents/MacOS"
        module_directory.mkdir(parents=True)
        probe = module_directory / args.probe.name
        shutil.copy2(args.probe, probe)
        external = root / "external" / ("xemu.exe" if windows else "xemu")
        executable_fixture(external)
        external_runtime = root / "external-runtime"
        runtime_fixture(external_runtime, windows)
        env = os.environ.copy()
        env["GDOX_XEMU"] = str(external)
        env["GDOX_RUNTIME_DIR"] = str(external_runtime)
        env["PATH"] = str(external.parent) + os.pathsep + env.get("PATH", "")

        def run(executable: Path, automatic: bool = False) -> subprocess.CompletedProcess[str]:
            command = ([args.runner] if args.runner else []) + [str(executable)]
            if automatic:
                command.append("--automatic")
            return subprocess.run(
                command, capture_output=True, text=True, env=env,
                cwd=root, timeout=30, check=False,
            )

        def matches(result: subprocess.CompletedProcess[str], expected: Path) -> bool:
            actual = result.stdout.strip().replace("\\", "/")
            wanted = str(expected).replace("\\", "/")
            if args.runner and actual.lower().startswith("z:"):
                actual = actual[2:]
            return result.returncode == 0 and (
                os.path.normcase(os.path.normpath(actual))
                == os.path.normcase(os.path.normpath(wanted))
            )

        # Existing automatic selection keeps honoring explicit external xemu.
        result = run(probe, automatic=True)
        assert matches(result, external), (result, external)
        # Explicit included mode must not fall back to any external candidate.
        result = run(probe)
        assert result.returncode != 0 and "included xemu was not found" in result.stderr, result
        if windows:
            assert "expected=" in result.stderr and "runtime\\xemu\\xemu.exe" in result.stderr, result
        runtime = module_directory / "runtime"
        if sys.platform == "darwin" and not windows:
            runtime = module_directory.parent / "Resources/runtime"
        included = runtime_fixture(runtime, windows)
        result = run(probe)
        assert matches(result, included), (result, included)
        # Moving the complete app must discover the new location immediately.
        relocated = root / "relocated"
        original.rename(relocated)
        probe = relocated / probe.relative_to(original)
        included = relocated / included.relative_to(original)
        result = run(probe)
        assert matches(result, included), (result, included)
        included.unlink()
        result = run(probe)
        assert result.returncode != 0 and "included xemu was not found" in result.stderr, result
        if windows:
            assert "expected=" in result.stderr and "relocated" in result.stderr, result
    print("Bundled xemu discovery survives relocation and refuses external fallback")


if __name__ == "__main__":
    main()
