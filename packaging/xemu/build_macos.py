#!/usr/bin/env python3
"""Build, test, and package the maintained universal macOS xemu runtime."""

from __future__ import annotations

import argparse
import importlib.metadata
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from package_xemu_macos_runtime import (
    ARCHITECTURES,
    executable_rpaths,
    expected_capability_bytes,
    file_sha256,
    macho_architectures,
    normalize_macho_uuid,
    package_runtime,
    probe_capability,
    remove_macho_signature,
    validate_thin_app,
)
from xemu_integration import validate as validate_integration


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    environment: dict[str, str] | None = None,
    capture: bool = True,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        check=True,
        capture_output=capture,
        text=True,
    )


def run_logged(
    command: list[str],
    log: Path,
    *,
    cwd: Path | None = None,
    environment: dict[str, str] | None = None,
    append: bool = False,
) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("ab" if append else "wb") as output:
        subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            check=True,
            stdout=output,
            stderr=subprocess.STDOUT,
        )


def run_logged_with_retries(
    command: list[str],
    log: Path,
    *,
    cwd: Path,
    environment: dict[str, str],
    attempts: int = 3,
) -> None:
    for attempt in range(1, attempts + 1):
        try:
            run_logged(
                command,
                log,
                cwd=cwd,
                environment=environment,
                append=attempt > 1,
            )
            return
        except subprocess.CalledProcessError:
            if attempt == attempts:
                raise
            with log.open("a", encoding="utf-8") as output:
                output.write(f"\nretrying dependency fetch ({attempt}/{attempts})\n")
            time.sleep(5)


def canonical_path(path: Path) -> Path:
    return path.expanduser().resolve(strict=False)


def require_not_root(path: Path, label: str) -> None:
    if path == Path(path.anchor):
        raise RuntimeError(f"{label} must not be a filesystem root: {path}")


def require_no_symlink_ancestors(path: Path, label: str) -> None:
    current = Path(path.anchor)
    for part in path.parts[1:]:
        current /= part
        if not current.exists() and not current.is_symlink():
            break
        if stat.S_ISLNK(current.lstat().st_mode):
            raise RuntimeError(f"{label} traverses a symlink: {current}")


def paths_overlap(first: Path, second: Path) -> bool:
    return first == second or first in second.parents or second in first.parents


def require_empty_directory(path: Path, label: str) -> None:
    if path.exists():
        if path.is_symlink() or not path.is_dir():
            raise RuntimeError(f"{label} is not a safe directory: {path}")
        if next(path.iterdir(), None) is not None:
            raise RuntimeError(f"{label} must not exist or must be empty: {path}")
    else:
        path.mkdir(parents=True)
    require_no_symlink_ancestors(path, label)


def verify_file(path: Path, expected: dict, label: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise RuntimeError(f"{label} is missing or unsafe: {path}")
    if path.stat().st_size != expected["size"]:
        raise RuntimeError(f"{label} size differs: {path}")
    if file_sha256(path) != expected["sha256"]:
        raise RuntimeError(f"{label} digest differs: {path}")


def _archive_members(archive: Path, compression: str) -> tuple[str, ...]:
    option = "--zstd" if compression == "zstd" else "-z"
    output = run(["tar", option, "-tf", str(archive)]).stdout
    return tuple(line for line in output.splitlines() if line)


def validate_archive_members(
    archive: Path,
    compression: str,
    expected_root: str,
) -> None:
    members = _archive_members(archive, compression)
    if not members:
        raise RuntimeError(f"source archive is empty: {archive}")
    for member in members:
        normalized = member.removeprefix("./")
        parts = Path(normalized).parts
        if (
            not parts
            or parts[0] != expected_root
            or Path(normalized).is_absolute()
            or ".." in parts
            or "\\" in normalized
        ):
            raise RuntimeError(f"source archive has an unsafe member: {member}")


def extract_archive(
    archive: Path,
    destination: Path,
    *,
    compression: str,
    expected_root: str,
) -> None:
    if destination.exists():
        raise RuntimeError(f"source destination already exists: {destination}")
    validate_archive_members(archive, compression, expected_root)
    extraction = destination.parent / f".{destination.name}.extract"
    extraction.mkdir(parents=True, exist_ok=False)
    option = "--zstd" if compression == "zstd" else "-z"
    run(["tar", option, "-xf", str(archive), "-C", str(extraction)])
    children = tuple(extraction.iterdir())
    if len(children) != 1 or children[0].name != expected_root:
        raise RuntimeError(f"source archive root differs: {archive}")
    children[0].rename(destination)
    extraction.rmdir()


def require_exact_output(command: list[str], expected: str, label: str) -> None:
    actual = run(command).stdout.strip()
    if actual != expected:
        raise RuntimeError(f"{label} must report {expected!r}; received {actual!r}")


def require_output_line(
    command: list[str], expected: str, label: str, line: int = 0
) -> None:
    lines = run(command).stdout.strip().splitlines()
    actual = lines[line] if len(lines) > line else ""
    if actual != expected:
        raise RuntimeError(f"{label} must report {expected!r}; received {actual!r}")


def require_toolchain(recipe: dict) -> None:
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise RuntimeError("the reviewed macOS xemu build host is Apple Silicon")
    tools = recipe["toolchain"]
    require_exact_output(["sw_vers", "-productVersion"], tools["macos"], "macOS")
    require_exact_output(["xcodebuild", "-version"], tools["xcode"], "Xcode")
    require_exact_output(
        ["xcrun", "--sdk", "macosx", "--show-sdk-version"],
        tools["macos_sdk"],
        "macOS SDK",
    )
    require_output_line(["xcrun", "clang", "--version"], tools["clang"], "Clang")
    require_exact_output(["git", "--version"], tools["git"], "Git")
    require_output_line(["cmake", "--version"], tools["cmake"], "CMake")
    require_exact_output(["ninja", "--version"], tools["ninja"], "Ninja")
    require_output_line(["/usr/bin/zip", "-v"], tools["zip"], "Zip", line=1)
    actual_python = platform.python_version()
    if actual_python != tools["python"]:
        raise RuntimeError(
            f"Python must be {tools['python']}; received {actual_python}"
        )
    for distribution, expected in tools["python_packages"].items():
        actual = importlib.metadata.version(distribution)
        if actual != expected:
            raise RuntimeError(f"{distribution} must be {expected}; received {actual}")
    rosetta = subprocess.run(
        ["/usr/bin/arch", "-x86_64", "/usr/bin/true"],
        check=False,
        capture_output=True,
    )
    if rosetta.returncode != 0:
        raise RuntimeError("Rosetta is required to audit the Intel runtime")


def build_dylibbundler(
    archive: Path,
    work_root: Path,
    recipe: dict,
    jobs: int,
) -> Path:
    definition = recipe["dylibbundler"]
    verify_file(archive, definition["source"], "dylibbundler source")
    source = work_root / "tools/dylibbundler-source"
    extract_archive(
        archive,
        source,
        compression="gzip",
        expected_root=definition["source"]["root"],
    )
    log = work_root / "logs/dylibbundler-build.log"
    run_logged(["make", f"-j{jobs}"], log, cwd=source)
    executable = source / "dylibbundler"
    verify_file(executable, definition["executable"], "dylibbundler")
    tools = work_root / "tools/bin"
    tools.mkdir(parents=True)
    destination = tools / "dylibbundler"
    shutil.copy2(executable, destination)
    destination.chmod(0o755)
    require_output_line(
        [str(destination), "-h"],
        definition["version_output"],
        "dylibbundler",
    )
    return destination


def build_environment(tool: Path, recipe: dict, source: Path) -> dict[str, str]:
    canonical = recipe["canonical_source_root"]
    physical = source.resolve(strict=True)
    flags = [
        "-Wno-error=redundant-decls",
        f"-ffile-prefix-map={physical}={canonical}",
        f"-fdebug-prefix-map={physical}={canonical}",
    ]
    path = os.pathsep.join(
        (str(Path(sys.executable).parent), str(tool.parent), os.environ["PATH"])
    )
    return os.environ | {
        "CFLAGS": " ".join(flags),
        "PATH": path,
        "PYTHONDONTWRITEBYTECODE": "1",
        "SOURCE_DATE_EPOCH": str(recipe["source_date_epoch"]),
    }


def prepare_patched_source(
    source_archive: Path,
    destination: Path,
    patches: list[Path],
    source_definition: dict,
) -> None:
    extract_archive(
        source_archive,
        destination,
        compression="zstd",
        expected_root=source_definition["root"],
    )
    for patch in patches:
        run(
            [
                "git",
                "-C",
                str(destination),
                "apply",
                "--check",
                "--whitespace=error-all",
                str(patch),
            ]
        )
        run(
            [
                "git",
                "-C",
                str(destination),
                "apply",
                "--whitespace=error-all",
                str(patch),
            ]
        )


def build_slice(
    source: Path,
    architecture: str,
    tool: Path,
    recipe: dict,
    work_root: Path,
    jobs: int,
) -> Path:
    definition = recipe["macos_universal"]["architectures"][architecture]
    environment = build_environment(tool, recipe, source)
    run_logged_with_retries(
        [sys.executable, "scripts/download-macos-libs.py", architecture],
        work_root / f"logs/xemu-{architecture}-dependencies.log",
        cwd=source,
        environment=environment,
    )
    command = [
        "bash",
        "./build.sh",
        f"-j{jobs}",
        "-a",
        architecture,
        *recipe["configure_arguments"][1:],
        f"-Dc_link_args={' '.join(definition['link_arguments'])}",
        f"-Dcpp_link_args={' '.join(definition['link_arguments'])}",
    ]
    run_logged(
        command,
        work_root / f"logs/xemu-{architecture}-build.log",
        cwd=source,
        environment=environment,
    )
    validate_build_arguments(source, architecture, definition)
    test_targets = [
        "tests/unit/test-xemu-gdox-runtime",
        "tests/unit/test-xbox-volatile-hdd",
        "tests/unit/test-xbox-save-vault",
    ]
    run_logged(
        ["ninja", "-C", "build", *test_targets],
        work_root / f"logs/xemu-{architecture}-tests-build.log",
        cwd=source,
        environment=environment,
    )
    run_logged(
        ["bash", "tests/unit/test-xemu-version.sh", str(source)],
        work_root / f"logs/{architecture}-test-xemu-version.log",
        cwd=source,
        environment=environment,
    )
    app = source / "dist/xemu.app"
    executable = app / "Contents/MacOS/xemu"
    run(["xcrun", "strip", "-S", "-x", str(executable)])
    if str(source.resolve(strict=True)).encode("utf-8") in executable.read_bytes():
        raise RuntimeError(
            f"xemu {architecture} executable contains its generated build path"
        )
    expected_rpath = definition["rpath"]
    before = executable_rpaths(executable, architecture)
    expected_before = (expected_rpath,) * definition["pre_normalization_count"]
    if before != expected_before:
        raise RuntimeError(
            f"xemu {architecture} pre-normalization rpaths differ: {before}"
        )
    if len(before) == 2:
        run(["install_name_tool", "-delete_rpath", expected_rpath, str(executable)])
    normalize_macho_uuid(executable, architecture, definition["uuid"])
    run(["codesign", "--force", "--deep", "--sign", "-", str(app)])
    validate_thin_app(app, architecture, definition)
    return app


def validate_build_arguments(
    source: Path,
    architecture: str,
    definition: dict,
) -> None:
    options_path = source / "build/meson-info/intro-buildoptions.json"
    options = {
        entry["name"]: entry["value"]
        for entry in json.loads(options_path.read_text(encoding="utf-8"))
    }
    minimum = definition["minimum_macos"]
    link_arguments = definition["link_arguments"]
    try:
        sdk_path = link_arguments[link_arguments.index("-isysroot") + 1]
    except (ValueError, IndexError) as error:
        raise RuntimeError(
            f"xemu {architecture} linker SDK argument is missing"
        ) from error
    required_compile_arguments = (
        "-arch",
        architecture,
        "-target",
        f"{architecture}-apple-macos{minimum}",
        "-isysroot",
        sdk_path,
        f"-mmacosx-version-min={minimum}",
    )
    for language in ("c", "cpp"):
        compile_arguments = options.get(f"{language}_args")
        if not isinstance(compile_arguments, list) or not all(
            argument in compile_arguments for argument in required_compile_arguments
        ):
            raise RuntimeError(
                f"xemu {architecture} {language} deployment arguments differ"
            )
        actual_link_arguments = options.get(f"{language}_link_args")
        if actual_link_arguments != link_arguments:
            raise RuntimeError(
                f"xemu {architecture} {language} linker arguments differ: "
                f"{actual_link_arguments}"
            )


def build_plain_qemu_img(
    source_archive: Path,
    destination: Path,
    tool: Path,
    recipe: dict,
    source_definition: dict,
    work_root: Path,
    jobs: int,
) -> tuple[Path, Path]:
    extract_archive(
        source_archive,
        destination,
        compression="zstd",
        expected_root=source_definition["root"],
    )
    environment = build_environment(tool, recipe, destination)
    run_logged_with_retries(
        [sys.executable, "scripts/download-macos-libs.py", "arm64"],
        work_root / "logs/qemu-img-dependencies.log",
        cwd=destination,
        environment=environment,
    )
    prefix = destination / "macos-libs/arm64/opt/local"
    build = destination / "qemu-img-build"
    build.mkdir()
    physical = destination.resolve(strict=True)
    canonical = recipe["canonical_source_root"]
    arm64_definition = recipe["macos_universal"]["architectures"]["arm64"]
    minimum = arm64_definition["minimum_macos"]
    link_arguments = arm64_definition["link_arguments"]
    sdk = link_arguments[link_arguments.index("-isysroot") + 1]
    cflags = " ".join(
        (
            "-Wno-error=redundant-decls",
            "-arch arm64",
            f"-target arm64-apple-macos{minimum}",
            f"-isysroot {sdk}",
            f"-I{prefix}/include",
            f"-mmacosx-version-min={minimum}",
            f"-ffile-prefix-map={physical}={canonical}",
            f"-fdebug-prefix-map={physical}={canonical}",
        )
    )
    configure = [
        str(destination / "configure"),
        "--target-list=i386-softmmu",
        "--enable-tools",
        "--disable-werror",
        "--disable-cocoa",
        "--cross-prefix=",
        "-Db_lto=true",
        "-Dx86_version=3",
        f"-Dc_link_args={' '.join(arm64_definition['link_arguments'])}",
        f"-Dcpp_link_args={' '.join(arm64_definition['link_arguments'])}",
        f"--extra-cflags={cflags}",
        f"--extra-ldflags=-headerpad_max_install_names -arch arm64 -isysroot {sdk}",
    ]
    environment |= {"PKG_CONFIG_LIBDIR": str(prefix / "lib/pkgconfig")}
    run_logged(
        configure,
        work_root / "logs/qemu-img-configure.log",
        cwd=build,
        environment=environment,
    )
    run_logged(
        ["ninja", f"-j{jobs}", "qemu-img"],
        work_root / "logs/qemu-img-build.log",
        cwd=build,
        environment=environment,
    )
    executable = build / "qemu-img"
    if macho_architectures(executable) != ("arm64",):
        raise RuntimeError("plain qemu-img is not Apple Silicon")
    probe = work_root / "qemu-img-probe"
    probe.mkdir()
    raw = probe / "input.raw"
    qcow = probe / "output.qcow2"
    with raw.open("wb") as output:
        output.truncate(16 * 1024 * 1024)
    probe_environment = environment | {"DYLD_LIBRARY_PATH": str(prefix / "lib")}
    run(
        [str(executable), "convert", "-f", "raw", "-O", "qcow2", str(raw), str(qcow)],
        environment=probe_environment,
    )
    run(
        [str(executable), "check", "-f", "qcow2", str(qcow)],
        environment=probe_environment,
    )
    return executable, prefix / "lib"


def run_storage_tests(
    sources: dict[str, Path],
    qemu_img: Path,
    qemu_img_libraries: Path,
    work_root: Path,
) -> None:
    for architecture in ARCHITECTURES:
        source = sources[architecture]
        architecture_libraries = source / (f"macos-libs/{architecture}/opt/local/lib")
        environment = os.environ | {
            "DYLD_LIBRARY_PATH": os.pathsep.join(
                (str(qemu_img_libraries), str(architecture_libraries))
            ),
            "PATH": os.pathsep.join((str(qemu_img.parent), os.environ["PATH"])),
        }
        prefix: list[str] = []
        if architecture == "x86_64":
            prefix = [
                "/usr/bin/arch",
                "-x86_64",
                "/usr/bin/env",
                f"DYLD_LIBRARY_PATH={environment['DYLD_LIBRARY_PATH']}",
                f"PATH={environment['PATH']}",
            ]
            environment = os.environ.copy()
        tests = (
            ("test-xemu-gdox-runtime", ["--tap"]),
            ("test-xbox-volatile-hdd", ["--tap"]),
            ("test-xbox-save-vault", ["--tap", "-m", "slow"]),
        )
        for name, arguments in tests:
            executable = source / f"build/tests/unit/{name}"
            log = work_root / f"logs/{architecture}-{name}.tap"
            run_logged(
                [*prefix, str(executable), *arguments],
                log,
                environment=environment,
            )
            text = log.read_text(encoding="utf-8")
            if "not ok " in text or "Bail out!" in text:
                raise RuntimeError(f"xemu {architecture} {name} failed")
            if name == "test-xbox-save-vault" and (
                "ok 15 /xbox-save-vault/real-raw-and-qcow2-migration" not in text
                or "real-raw-and-qcow2-migration # SKIP" in text
            ):
                raise RuntimeError(
                    f"xemu {architecture} raw/QCOW2 migration did not run"
                )


def assemble_universal(
    apps: dict[str, Path],
    work_root: Path,
) -> tuple[Path, Path]:
    arm_app = apps["arm64"]
    intel_app = apps["x86_64"]
    for relative in (
        "Contents/Info.plist",
        "Contents/Resources/xemu.icns",
    ):
        if (arm_app / relative).read_bytes() != (intel_app / relative).read_bytes():
            raise RuntimeError(f"macOS xemu slice resources differ: {relative}")
    arm_license = arm_app.parent / "LICENSE.txt"
    intel_license = intel_app.parent / "LICENSE.txt"
    if arm_license.read_bytes() != intel_license.read_bytes():
        raise RuntimeError("macOS xemu slice license texts differ")

    universal = work_root / "universal"
    app = universal / "xemu.app"
    universal.mkdir()
    shutil.copytree(arm_app, app)
    signature = app / "Contents/_CodeSignature"
    shutil.rmtree(signature)
    shutil.copytree(
        intel_app / "Contents/Libraries/x86_64",
        app / "Contents/Libraries/x86_64",
    )
    executable = app / "Contents/MacOS/xemu"
    arm_executable = arm_app / "Contents/MacOS/xemu"
    intel_executable = intel_app / "Contents/MacOS/xemu"
    run(
        [
            "xcrun",
            "lipo",
            "-create",
            str(arm_executable),
            str(intel_executable),
            "-output",
            str(executable),
        ]
    )
    remove_macho_signature(executable, ARCHITECTURES)
    run(["codesign", "--force", "--deep", "--sign", "-", str(app)])
    license_file = universal / "LICENSE.txt"
    shutil.copy2(arm_license, license_file)
    return app, license_file


def load_integration(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TypeError("xemu integration metadata must be an object")
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-archive", required=True, type=Path)
    parser.add_argument("--dylibbundler-source-archive", required=True, type=Path)
    parser.add_argument("--work-root", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    os.environ["PATH"] = os.pathsep.join(
        (str(Path(sys.executable).parent), os.environ["PATH"])
    )

    source_archive = canonical_path(args.source_archive)
    dylibbundler_archive = canonical_path(args.dylibbundler_source_archive)
    work_root = canonical_path(args.work_root)
    output_directory = canonical_path(args.output_directory)
    repository = canonical_path(ROOT)
    for path, label in (
        (repository, "repository"),
        (work_root, "work root"),
        (output_directory, "output directory"),
    ):
        require_not_root(path, label)
        require_no_symlink_ancestors(path, label)
    roots = (repository, work_root, output_directory)
    if any(
        paths_overlap(first, second)
        for index, first in enumerate(roots)
        for second in roots[index + 1 :]
    ):
        parser.error("repository, work root, and output directory must differ")
    if source_archive == dylibbundler_archive:
        parser.error("source archives must be separate files")
    if (
        repository in source_archive.parents
        or repository in dylibbundler_archive.parents
    ):
        parser.error("source archives must be outside the repository")
    if any(
        path == source_archive
        or path == dylibbundler_archive
        or path in source_archive.parents
        or path in dylibbundler_archive.parents
        for path in (work_root, output_directory)
    ):
        parser.error("input archives must be outside generated output trees")
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    require_empty_directory(work_root, "work root")
    require_empty_directory(output_directory, "output directory")
    integration_path = ROOT / "packaging/xemu/integration.json"
    integration = load_integration(integration_path)
    recipe = integration["build_recipe"]
    source_definition = recipe["macos_universal"]["source_archive"]
    verify_file(source_archive, source_definition, "xemu source")
    require_toolchain(recipe["macos_universal"])
    patches = validate_integration()
    tool = build_dylibbundler(
        dylibbundler_archive, work_root, recipe["macos_universal"], args.jobs
    )

    sources: dict[str, Path] = {}
    apps: dict[str, Path] = {}
    for architecture in ARCHITECTURES:
        source = work_root / f"xemu-{architecture}"
        prepare_patched_source(source_archive, source, patches, source_definition)
        sources[architecture] = source
        apps[architecture] = build_slice(
            source,
            architecture,
            tool,
            recipe,
            work_root,
            args.jobs,
        )

    qemu_source = work_root / "xemu-qemu-img"
    qemu_img, qemu_img_libraries = build_plain_qemu_img(
        source_archive,
        qemu_source,
        tool,
        recipe,
        source_definition,
        work_root,
        args.jobs,
    )
    run_storage_tests(sources, qemu_img, qemu_img_libraries, work_root)

    expected = expected_capability_bytes(integration)
    probes = work_root / "thin-probes"
    probes.mkdir()
    for architecture in ARCHITECTURES:
        probe_capability(
            apps[architecture] / "Contents/MacOS/xemu",
            architecture,
            expected,
            probes,
        )
    app, license_file = assemble_universal(apps, work_root)
    archive = output_directory / recipe["macos_universal"]["archive_name"]
    identity = package_runtime(
        app,
        license_file,
        archive,
        work_root / "package-temporary",
        integration,
    )
    print(json.dumps(identity, indent=2, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(f"macOS xemu build failed: {error}") from error
