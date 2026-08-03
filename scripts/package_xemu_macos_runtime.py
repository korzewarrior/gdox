#!/usr/bin/env python3
"""Validate and package the maintained universal macOS xemu runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import uuid
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INTEGRATION = ROOT / "packaging/xemu/integration.json"
ARCHITECTURES = ("arm64", "x86_64")
ARCHIVE_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
LIBRARY_NAMES = (
    "libSDL3.0.dylib",
    "libepoxy.0.dylib",
    "libglib-2.0.0.dylib",
    "libiconv.2.dylib",
    "libintl.8.dylib",
    "libpcap.A.dylib",
    "libpcre2-8.0.dylib",
    "libsamplerate.0.dylib",
    "libslirp.0.dylib",
    "libusb-1.0.0.dylib",
    "libz.1.dylib",
)
APP_FIXED_FILES = (
    "Contents/Info.plist",
    "Contents/MacOS/xemu",
    "Contents/Resources/xemu.icns",
    "Contents/_CodeSignature/CodeResources",
)
LC_UUID = 0x1B
MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
CPU_ARCHITECTURES = {
    0x0100000C: "arm64",
    0x01000007: "x86_64",
}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        check=True,
        capture_output=True,
        text=True,
    )


def _relative_files(root: Path) -> set[str]:
    files: set[str] = set()
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise RuntimeError(f"macOS runtime contains a symlink: {relative}")
        mode = path.lstat().st_mode
        if stat.S_ISREG(mode):
            files.add(relative)
        elif not stat.S_ISDIR(mode):
            raise RuntimeError(f"macOS runtime contains a special file: {relative}")
    return files


def expected_app_files() -> set[str]:
    files = set(APP_FIXED_FILES)
    for architecture in ARCHITECTURES:
        for name in LIBRARY_NAMES:
            files.add(f"Contents/Libraries/{architecture}/{name}")
    return files


def macho_architectures(path: Path) -> tuple[str, ...]:
    output = run(["xcrun", "lipo", "-archs", str(path)]).stdout.strip()
    if not output:
        raise RuntimeError(f"lipo returned no architecture for {path}")
    return tuple(output.split())


def parse_rpaths(output: str) -> tuple[str, ...]:
    rpaths: list[str] = []
    waiting_for_path = False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped == "cmd LC_RPATH":
            waiting_for_path = True
            continue
        if waiting_for_path and stripped.startswith("path "):
            fields = stripped.split()
            if len(fields) < 2:
                raise RuntimeError("otool returned an invalid LC_RPATH entry")
            rpaths.append(fields[1])
            waiting_for_path = False
    if waiting_for_path:
        raise RuntimeError("otool omitted an LC_RPATH path")
    return tuple(rpaths)


def executable_rpaths(executable: Path, architecture: str) -> tuple[str, ...]:
    output = run(["otool", "-arch", architecture, "-l", str(executable)]).stdout
    return parse_rpaths(output)


def parse_load_commands(output: str) -> tuple[str, ...]:
    return tuple(
        stripped.removeprefix("cmd ")
        for line in output.splitlines()
        if (stripped := line.strip()).startswith("cmd ")
    )


def executable_load_commands(executable: Path, architecture: str) -> tuple[str, ...]:
    output = run(["otool", "-arch", architecture, "-l", str(executable)]).stdout
    return parse_load_commands(output)


def remove_macho_signature(
    executable: Path,
    architectures: tuple[str, ...],
) -> None:
    """Remove inherited signature blobs before the deterministic final signing pass."""
    run(["codesign", "--remove-signature", str(executable)])
    for architecture in architectures:
        commands = executable_load_commands(executable, architecture)
        if "LC_CODE_SIGNATURE" in commands:
            raise RuntimeError(
                f"macOS xemu {architecture} retained an inherited code signature"
            )


def _macho_slices(data: bytes) -> tuple[tuple[str, int, int], ...]:
    if len(data) < 8:
        raise RuntimeError("macOS xemu Mach-O header is truncated")
    little_magic = struct.unpack_from("<I", data)[0]
    if little_magic == MH_MAGIC_64:
        cpu_type = struct.unpack_from("<I", data, 4)[0]
        architecture = CPU_ARCHITECTURES.get(cpu_type)
        if architecture is None:
            raise RuntimeError(f"macOS xemu Mach-O CPU type differs: {cpu_type:#x}")
        return ((architecture, 0, len(data)),)

    big_magic, count = struct.unpack_from(">II", data)
    if big_magic not in (FAT_MAGIC, FAT_MAGIC_64) or count < 1:
        raise RuntimeError("macOS xemu has an unsupported Mach-O header")
    entry_size = 20 if big_magic == FAT_MAGIC else 32
    table_end = 8 + count * entry_size
    if table_end > len(data):
        raise RuntimeError("macOS xemu universal header is truncated")
    slices: list[tuple[str, int, int]] = []
    for index in range(count):
        position = 8 + index * entry_size
        cpu_type = struct.unpack_from(">I", data, position)[0]
        architecture = CPU_ARCHITECTURES.get(cpu_type)
        if architecture is None:
            raise RuntimeError(f"macOS xemu Mach-O CPU type differs: {cpu_type:#x}")
        if big_magic == FAT_MAGIC:
            offset, size = struct.unpack_from(">II", data, position + 8)
        else:
            offset, size = struct.unpack_from(">QQ", data, position + 8)
        if offset < table_end or size < 32 or offset + size > len(data):
            raise RuntimeError(f"macOS xemu {architecture} slice is invalid")
        slices.append((architecture, offset, size))
    if len({architecture for architecture, _, _ in slices}) != len(slices):
        raise RuntimeError("macOS xemu has duplicate Mach-O architectures")
    return tuple(slices)


def _macho_uuid_offsets(data: bytes) -> dict[str, int]:
    offsets: dict[str, int] = {}
    for architecture, slice_offset, slice_size in _macho_slices(data):
        if struct.unpack_from("<I", data, slice_offset)[0] != MH_MAGIC_64:
            raise RuntimeError(f"macOS xemu {architecture} slice is not 64-bit Mach-O")
        command_count, command_bytes = struct.unpack_from(
            "<II", data, slice_offset + 16
        )
        command_offset = slice_offset + 32
        command_end = command_offset + command_bytes
        if command_end > slice_offset + slice_size:
            raise RuntimeError(f"macOS xemu {architecture} load commands are invalid")
        uuid_offsets: list[int] = []
        for _ in range(command_count):
            if command_offset + 8 > command_end:
                raise RuntimeError(
                    f"macOS xemu {architecture} load command is truncated"
                )
            command, command_size = struct.unpack_from("<II", data, command_offset)
            if command_size < 8 or command_offset + command_size > command_end:
                raise RuntimeError(f"macOS xemu {architecture} load command is invalid")
            if command == LC_UUID:
                if command_size != 24:
                    raise RuntimeError(f"macOS xemu {architecture} LC_UUID is invalid")
                uuid_offsets.append(command_offset + 8)
            command_offset += command_size
        if command_offset != command_end or len(uuid_offsets) != 1:
            raise RuntimeError(
                f"macOS xemu {architecture} must contain exactly one LC_UUID"
            )
        offsets[architecture] = uuid_offsets[0]
    return offsets


def macho_uuids(executable: Path) -> dict[str, str]:
    data = executable.read_bytes()
    return {
        architecture: str(uuid.UUID(bytes=data[offset : offset + 16]))
        for architecture, offset in _macho_uuid_offsets(data).items()
    }


def normalize_macho_uuid(
    executable: Path,
    architecture: str,
    expected_uuid: str,
) -> None:
    data = bytearray(executable.read_bytes())
    offsets = _macho_uuid_offsets(data)
    if tuple(offsets) != (architecture,):
        raise RuntimeError(
            f"macOS xemu UUID normalization expected only {architecture}"
        )
    expected = uuid.UUID(expected_uuid)
    offset = offsets[architecture]
    data[offset : offset + 16] = expected.bytes
    executable.write_bytes(data)
    if macho_uuids(executable) != {architecture: str(expected)}:
        raise RuntimeError(f"macOS xemu {architecture} UUID normalization failed")


def _dependencies(path: Path, architecture: str | None = None) -> tuple[str, ...]:
    command = ["otool"]
    if architecture is not None:
        command.extend(("-arch", architecture))
    command.extend(("-L", str(path)))
    lines = run(command).stdout.splitlines()
    dependencies: list[str] = []
    for line in lines[1:]:
        stripped = line.strip()
        if stripped:
            dependencies.append(stripped.split(" (", 1)[0])
    return tuple(dependencies)


def _validate_dependency(
    dependency: str,
    architecture: str,
    context: str,
) -> None:
    if dependency.startswith(("/System/Library/", "/usr/lib/")):
        return
    if dependency.startswith("@rpath/"):
        name = dependency.removeprefix("@rpath/")
        if name in LIBRARY_NAMES:
            return
    expected_prefix = f"@executable_path/../Libraries/{architecture}/"
    if (
        dependency.startswith(expected_prefix)
        and dependency.removeprefix(expected_prefix) in LIBRARY_NAMES
    ):
        return
    raise RuntimeError(f"{context} has an unbundled dependency: {dependency}")


def _minimum_macos(executable: Path, architecture: str) -> str:
    output = run(
        ["xcrun", "vtool", "-arch", architecture, "-show-build", str(executable)]
    ).stdout
    match = re.search(r"^\s*minos\s+(\S+)\s*$", output, re.MULTILINE)
    if match is None:
        raise RuntimeError(
            f"vtool did not report a minimum macOS version for {architecture}"
        )
    return match.group(1)


def _validate_signature(app: Path) -> None:
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    details = run(["codesign", "-dvv", str(app)]).stderr
    if "Signature=adhoc" not in details or "TeamIdentifier=not set" not in details:
        raise RuntimeError("macOS xemu must have one ad hoc bundle signature")


def validate_thin_app(app: Path, architecture: str, definition: dict) -> None:
    if architecture not in ARCHITECTURES:
        raise RuntimeError(f"unsupported macOS xemu architecture: {architecture}")
    if not app.is_dir() or app.is_symlink():
        raise RuntimeError(f"macOS xemu app is missing or unsafe: {app}")
    executable = app / "Contents/MacOS/xemu"
    if macho_architectures(executable) != (architecture,):
        raise RuntimeError(
            f"macOS xemu thin executable is not {architecture}: {executable}"
        )
    actual_uuids = macho_uuids(executable)
    if actual_uuids != {architecture: definition["uuid"]}:
        raise RuntimeError(f"macOS xemu {architecture} UUID differs: {actual_uuids}")
    rpaths = executable_rpaths(executable, architecture)
    if rpaths != (definition["rpath"],):
        raise RuntimeError(f"macOS xemu {architecture} rpaths differ: {rpaths}")
    minimum = _minimum_macos(executable, architecture)
    if minimum != definition["minimum_macos"]:
        raise RuntimeError(
            f"macOS xemu {architecture} minimum is {minimum}, not "
            f"{definition['minimum_macos']}"
        )
    for dependency in _dependencies(executable, architecture):
        _validate_dependency(dependency, architecture, "xemu")

    libraries = app / "Contents/Libraries"
    roots = tuple(sorted(path.name for path in libraries.iterdir()))
    if roots != (architecture,):
        raise RuntimeError(f"macOS xemu thin library roots differ: {roots}")
    library_root = libraries / architecture
    names = tuple(sorted(path.name for path in library_root.iterdir()))
    if names != tuple(sorted(LIBRARY_NAMES)):
        raise RuntimeError(f"macOS xemu {architecture} library inventory differs")
    for name in LIBRARY_NAMES:
        library = library_root / name
        if library.is_symlink() or not library.is_file():
            raise RuntimeError(f"macOS xemu library is unsafe: {library}")
        if macho_architectures(library) != (architecture,):
            raise RuntimeError(
                f"macOS xemu library has a wrong architecture: {library}"
            )
        for dependency in _dependencies(library):
            _validate_dependency(dependency, architecture, name)
    _validate_signature(app)


def validate_app(app: Path, macos_recipe: dict) -> None:
    if not app.is_dir() or app.is_symlink():
        raise RuntimeError(f"macOS xemu app is missing or unsafe: {app}")
    actual_files = _relative_files(app)
    expected_files = expected_app_files()
    if actual_files != expected_files:
        missing = sorted(expected_files - actual_files)
        extra = sorted(actual_files - expected_files)
        raise RuntimeError(
            f"macOS xemu app inventory differs; missing={missing}, extra={extra}"
        )

    plist_path = app / "Contents/Info.plist"
    with plist_path.open("rb") as source:
        plist = plistlib.load(source)
    expected_plist = {
        "CFBundleExecutable": "xemu",
        "CFBundleShortVersionString": macos_recipe["xemu_version"],
        "CFBundleVersion": macos_recipe["xemu_version"],
    }
    for key, expected in expected_plist.items():
        if plist.get(key) != expected:
            raise RuntimeError(
                f"macOS xemu Info.plist {key} differs: {plist.get(key)!r}"
            )

    executable = app / "Contents/MacOS/xemu"
    architectures = macho_architectures(executable)
    if set(architectures) != set(ARCHITECTURES) or len(architectures) != 2:
        raise RuntimeError(
            f"macOS xemu executable architectures differ: {architectures}"
        )

    definitions = macos_recipe["architectures"]
    expected_uuids = {
        architecture: definitions[architecture]["uuid"]
        for architecture in ARCHITECTURES
    }
    actual_uuids = macho_uuids(executable)
    if actual_uuids != expected_uuids:
        raise RuntimeError(f"macOS xemu UUIDs differ: {actual_uuids}")
    for architecture in ARCHITECTURES:
        definition = definitions[architecture]
        expected_rpath = definition["rpath"]
        actual_rpaths = executable_rpaths(executable, architecture)
        if actual_rpaths != (expected_rpath,):
            raise RuntimeError(
                f"macOS xemu {architecture} rpaths differ: {actual_rpaths}"
            )
        minimum = _minimum_macos(executable, architecture)
        if minimum != definition["minimum_macos"]:
            raise RuntimeError(
                f"macOS xemu {architecture} minimum is {minimum}, not "
                f"{definition['minimum_macos']}"
            )
        for dependency in _dependencies(executable, architecture):
            _validate_dependency(dependency, architecture, "xemu")

        library_root = app / "Contents/Libraries" / architecture
        actual_names = tuple(sorted(path.name for path in library_root.iterdir()))
        if actual_names != tuple(sorted(LIBRARY_NAMES)):
            raise RuntimeError(f"macOS xemu {architecture} library inventory differs")
        for name in LIBRARY_NAMES:
            library = library_root / name
            if macho_architectures(library) != (architecture,):
                raise RuntimeError(
                    f"macOS xemu library has a wrong architecture: {library}"
                )
            for dependency in _dependencies(library):
                _validate_dependency(dependency, architecture, name)

    _validate_signature(app)


def expected_capability_bytes(integration: dict) -> bytes:
    response = integration["capability_query"]["response"]
    return (
        json.dumps(response, ensure_ascii=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")


def probe_capability(
    executable: Path,
    architecture: str,
    expected: bytes,
    temporary_root: Path,
) -> None:
    probe_root = temporary_root / architecture
    home = probe_root / "home"
    working = probe_root / "working"
    scratch = probe_root / "scratch"
    for directory in (home, working, scratch):
        directory.mkdir(parents=True, exist_ok=False)
    command = [str(executable), "--gdox-capabilities"]
    if architecture == "x86_64":
        command = ["/usr/bin/arch", "-x86_64", *command]
    environment = os.environ | {
        "HOME": str(home),
        "TMPDIR": str(scratch),
        "XDG_CACHE_HOME": str(home / "cache"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_DATA_HOME": str(home / "data"),
    }
    result = subprocess.run(
        command,
        cwd=working,
        env=environment,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"macOS xemu {architecture} capability query exited {result.returncode}"
        )
    if result.stderr:
        raise RuntimeError(
            f"macOS xemu {architecture} capability query wrote diagnostics"
        )
    if result.stdout != expected:
        raise RuntimeError(f"macOS xemu {architecture} capability response differs")
    side_effects = [
        path.relative_to(probe_root).as_posix()
        for path in probe_root.rglob("*")
        if path not in {home, working, scratch}
    ]
    if side_effects:
        raise RuntimeError(
            f"macOS xemu {architecture} capability query wrote files: {side_effects}"
        )


def validate_archive(archive_path: Path, expected_files: set[str]) -> None:
    expected_members = {"LICENSE.txt", "xemu.app/"}
    for relative in expected_files:
        path = Path("xemu.app") / relative
        expected_members.add(path.as_posix())
        for parent in path.parents:
            if parent == Path("."):
                break
            expected_members.add(parent.as_posix().rstrip("/") + "/")
    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(names) != len(set(names)):
            raise RuntimeError("macOS xemu archive has duplicate members")
        if len({name.casefold() for name in names}) != len(names):
            raise RuntimeError("macOS xemu archive has case-colliding members")
        if names != sorted(names):
            raise RuntimeError("macOS xemu archive members are not sorted")
        if set(names) != expected_members:
            missing = sorted(expected_members - set(names))
            extra = sorted(set(names) - expected_members)
            raise RuntimeError(
                f"macOS xemu archive inventory differs; "
                f"missing={missing}, extra={extra}"
            )
        for info in infos:
            path = Path(info.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in info.filename:
                raise RuntimeError(
                    f"macOS xemu archive has an unsafe member: {info.filename}"
                )
            if info.date_time != ARCHIVE_TIMESTAMP:
                raise RuntimeError(
                    f"macOS xemu archive timestamp differs: {info.filename}"
                )
            mode = info.external_attr >> 16
            if mode:
                if stat.S_ISLNK(mode):
                    raise RuntimeError(
                        f"macOS xemu archive has a symlink: {info.filename}"
                    )
                expected_type = stat.S_ISDIR if info.is_dir() else stat.S_ISREG
                if not expected_type(mode):
                    raise RuntimeError(
                        f"macOS xemu archive has a special file: {info.filename}"
                    )
        corrupt = archive.testzip()
        if corrupt is not None:
            raise RuntimeError(f"macOS xemu archive is corrupt: {corrupt}")


def _normalize_timestamps(root: Path) -> None:
    paths = [root, *root.rglob("*")]
    run(["/usr/bin/touch", "-h", "-t", "198001010000", *map(str, paths)])


def write_archive(
    app: Path,
    license_file: Path,
    output: Path,
    staging_directory: Path,
) -> None:
    if output.exists():
        raise RuntimeError(f"macOS xemu output already exists: {output}")
    staging_directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="gdox-xemu-macos-stage-", dir=staging_directory
    ) as temporary:
        stage = Path(temporary)
        shutil.copytree(app, stage / "xemu.app", symlinks=True)
        shutil.copy2(license_file, stage / "LICENSE.txt")
        _normalize_timestamps(stage)
        members = ["."] + [
            f"./{path.relative_to(stage).as_posix()}"
            for path in sorted(stage.rglob("*"))
        ]
        environment = os.environ | {"COPYFILE_DISABLE": "1"}
        subprocess.run(
            ["/usr/bin/zip", "-X", "-y", "-q", str(output), "-@"],
            cwd=stage,
            env=environment,
            input="\n".join(members) + "\n",
            check=True,
            text=True,
        )


def load_integration(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TypeError("xemu integration metadata must be an object")
    return value


def package_runtime(
    app: Path,
    license_file: Path,
    output: Path,
    staging_directory: Path,
    integration: dict,
) -> dict[str, str | int]:
    macos_recipe = integration["build_recipe"]["macos_universal"]
    if output.name != macos_recipe["archive_name"]:
        raise RuntimeError(f"output must be named {macos_recipe['archive_name']}")
    if not license_file.is_file() or license_file.is_symlink():
        raise RuntimeError(f"license is missing or unsafe: {license_file}")

    validate_app(app, macos_recipe)
    expected = expected_capability_bytes(integration)
    staging_directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="gdox-xemu-macos-probe-", dir=staging_directory
    ) as temporary:
        probe_root = Path(temporary)
        for architecture in ARCHITECTURES:
            probe_capability(
                app / "Contents/MacOS/xemu",
                architecture,
                expected,
                probe_root,
            )

    write_archive(app, license_file, output, staging_directory)
    validate_archive(output, expected_app_files())
    identity: dict[str, str | int] = {
        "archive_name": output.name,
        "executable_sha256": file_sha256(app / "Contents/MacOS/xemu"),
        "executable_size": (app / "Contents/MacOS/xemu").stat().st_size,
        "sha256": file_sha256(output),
        "size": output.stat().st_size,
    }
    expected_identity = macos_recipe["deterministic_output"]
    if identity != expected_identity:
        raise RuntimeError(
            "macOS xemu output identity differs: "
            + json.dumps(identity, sort_keys=True)
        )
    return identity


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--license", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--staging-directory", required=True, type=Path)
    parser.add_argument("--integration", type=Path, default=DEFAULT_INTEGRATION)
    args = parser.parse_args()

    integration = load_integration(args.integration)
    try:
        identity = package_runtime(
            args.app,
            args.license,
            args.output,
            args.staging_directory,
            integration,
        )
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        parser.error(str(exc))
    print(json.dumps(identity, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
