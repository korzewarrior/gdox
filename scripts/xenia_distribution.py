"""Validate and stage the pinned Xenia runtime distribution."""

from __future__ import annotations

import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile
from collections.abc import Callable
from pathlib import Path

sys.dont_write_bytecode = True

from xenia_compatibility import CompatibilityError, load_compatibility_manifest

ROOT = Path(__file__).resolve().parent.parent
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
REVISION_PATTERN = re.compile(r"[0-9a-f]{7,40}")
LINUX_TARGET = "x86_64-unknown-linux-gnu"
WINDOWS_TARGET = "x86_64-pc-windows-msvc"
TARGET_FORMATS = {
    LINUX_TARGET: {"windows-zip-proton", "linux-appimage"},
    WINDOWS_TARGET: {"windows-zip", "windows-7z"},
}
NO_XENIA_TARGETS = {
    "x86_64-apple-darwin",
    "aarch64-apple-darwin",
}
RUNTIME_TARGETS = set(TARGET_FORMATS) | NO_XENIA_TARGETS
ASSET_FIELDS = {
    "archive_name",
    "url",
    "sha256",
    "size",
    "format",
    "executable",
    "executable_sha256",
    "executable_size",
    "disc_transport",
    "origin",
    "release_state",
    "managed_options",
}
MANAGED_OPTIONS = {
    "apu_max_queued_frames",
    "async_shader_compilation",
    "cache_root",
    "content_root",
    "custom_internal_display_resolution",
    "d3d12_pipeline_creation_threads",
    "disable_instruction_infocache",
    "flush_log",
    "framerate_limit",
    "gdox_disclaimer_acknowledged",
    "gdox_persistent_content_saves_only",
    "host_present_from_non_ui_thread",
    "ignore_thread_affinities",
    "log_level",
    "log_file",
    "mount_cache",
    "mount_scratch",
    "storage_root",
    "store_shaders",
    "vulkan_allow_present_mode_fifo_relaxed",
    "vulkan_pipeline_creation_threads",
}
DISC_TRANSPORTS = {
    "image-path-v1",
    "gdox-private-nbd-v1",
}
ASSET_ORIGINS = {
    "upstream-release",
    "gdox-patched",
}
RELEASE_STATES = {
    "published",
    "candidate-only",
}
# This is deliberately separate from the editable runtime manifest. These
# digests passed the save-only storage contract. The managed-session allowlist
# below remains separate so a manifest edit cannot grant either capability.
REVIEWED_SAVE_ONLY_EXECUTABLES: dict[tuple[str, str], str] = {
    (
        "72ce13097",
        LINUX_TARGET,
    ): "cbaeb15d890ba7bcc6d8140041bb29c6df3a47f339729ab975bcb3fd633930a5",
    (
        "72ce13097",
        WINDOWS_TARGET,
    ): "cbaeb15d890ba7bcc6d8140041bb29c6df3a47f339729ab975bcb3fd633930a5",
    (
        "7d8be7f17",
        LINUX_TARGET,
    ): "cd0a9c6bddf0d0b960cb24f63efa5d56ecd34fdf20a838c0b567aacbb6f638cf",
    (
        "7d8be7f17",
        WINDOWS_TARGET,
    ): "cd0a9c6bddf0d0b960cb24f63efa5d56ecd34fdf20a838c0b567aacbb6f638cf",
}
# Populated only after an executable built from the current patch series has
# passed the exact, side-effect-free capability probe. Manifest claims alone
# cannot enable the managed acknowledgement path.
REVIEWED_MANAGED_SESSION_EXECUTABLES: dict[tuple[str, str], str] = {
    (
        "72ce13097",
        LINUX_TARGET,
    ): "cbaeb15d890ba7bcc6d8140041bb29c6df3a47f339729ab975bcb3fd633930a5",
    (
        "72ce13097",
        WINDOWS_TARGET,
    ): "cbaeb15d890ba7bcc6d8140041bb29c6df3a47f339729ab975bcb3fd633930a5",
    (
        "7d8be7f17",
        LINUX_TARGET,
    ): "cd0a9c6bddf0d0b960cb24f63efa5d56ecd34fdf20a838c0b567aacbb6f638cf",
    (
        "7d8be7f17",
        WINDOWS_TARGET,
    ): "cd0a9c6bddf0d0b960cb24f63efa5d56ecd34fdf20a838c0b567aacbb6f638cf",
}
STORAGE_CAPABILITY_RESPONSE = (
    b'{"schema":1,"runtime":"xenia","storage":{'
    b'"persistent":"saves-profiles-only",'
    b'"game_content":"ephemeral"},"session":{'
    b'"disclaimer":"acknowledged-no-external-link"}}\n'
)
INJECTION_ENVIRONMENT_PATH = (
    ROOT / "src" / "platform" / "xenia_injection_environment.def"
)
INJECTION_ENVIRONMENT_LINE = re.compile(
    r"GDOX_XENIA_INJECTION_(UNSET|SET)\("
    r"([A-Z_][A-Z0-9_]*)"
    r"(?:, ([A-Za-z0-9_~*.+-]+))?\)\Z"
)
FetchAsset = Callable[[dict, Path], Path]


def _require_target(target: str) -> None:
    if target not in RUNTIME_TARGETS:
        raise SystemExit(f"unsupported Xenia runtime target: {target}")


def load_injection_environment_policy() -> tuple[
    tuple[str, ...], tuple[tuple[str, str], ...]
]:
    removed: list[str] = []
    fixed: list[tuple[str, str]] = []
    names: set[str] = set()
    try:
        lines = INJECTION_ENVIRONMENT_PATH.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise SystemExit(f"could not read Xenia injection policy: {error}") from None
    in_comment = False
    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.strip()
        if in_comment:
            if line.endswith("*/"):
                in_comment = False
            continue
        if line.startswith("/*"):
            in_comment = not line.endswith("*/")
            continue
        if not line or line.startswith("//"):
            continue
        match = INJECTION_ENVIRONMENT_LINE.fullmatch(line)
        if match is None:
            raise SystemExit(
                "Xenia injection policy has invalid syntax at line "
                f"{line_number}"
            )
        action, name, value = match.groups()
        if name in names:
            raise SystemExit(f"Xenia injection policy repeats {name}")
        names.add(name)
        if action == "UNSET":
            if value is not None:
                raise SystemExit(f"Xenia injection unset {name} has a value")
            removed.append(name)
        else:
            if value is None:
                raise SystemExit(f"Xenia injection setting {name} has no value")
            fixed.append((name, value))
    if in_comment or not removed or not fixed:
        raise SystemExit("Xenia injection policy is incomplete")
    return tuple(removed), tuple(fixed)


def render_injection_environment_policy() -> str:
    removed, fixed = load_injection_environment_policy()
    lines = ["unset " + " ".join(removed)]
    lines.extend(f"{name}={shlex.quote(value)}" for name, value in fixed)
    lines.append("export " + " ".join(name for name, unused in fixed))
    return "\n".join(lines)


def has_reviewed_save_only_capability(
    revision: str,
    target: str,
    asset: dict,
) -> bool:
    return (
        asset["origin"] == "gdox-patched"
        and "gdox_persistent_content_saves_only"
        in asset["managed_options"]
        and REVIEWED_SAVE_ONLY_EXECUTABLES.get((revision, target))
        == asset["executable_sha256"]
    )


def has_reviewed_managed_session_capability(
    revision: str,
    target: str,
    asset: dict,
) -> bool:
    return (
        asset["origin"] == "gdox-patched"
        and "gdox_disclaimer_acknowledged" in asset["managed_options"]
        and REVIEWED_MANAGED_SESSION_EXECUTABLES.get((revision, target))
        == asset["executable_sha256"]
    )


def verify_storage_capability(executable: Path, label: str) -> None:
    if not executable.is_file():
        raise SystemExit(f"{label} executable does not exist")
    executable = executable.resolve()
    with tempfile.TemporaryDirectory(prefix="gdox-xenia-capability-") as root:
        isolated_root = Path(root)
        environment = os.environ.copy()
        for variable in (
            "APPDATA",
            "HOME",
            "LOCALAPPDATA",
            "TEMP",
            "TMP",
            "TMPDIR",
            "USERPROFILE",
            "XDG_CACHE_HOME",
            "XDG_CONFIG_HOME",
            "XDG_DATA_HOME",
            "XDG_STATE_HOME",
        ):
            environment[variable] = root
        try:
            result = subprocess.run(
                [str(executable), "--gdox-storage-capabilities"],
                cwd=isolated_root,
                env=environment,
                stdin=subprocess.DEVNULL,
                capture_output=True,
                check=False,
                timeout=5,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise SystemExit(
                f"{label} storage capability query failed: {error}"
            ) from None
        created_entries = tuple(isolated_root.iterdir())
    if (
        result.returncode != 0
        or result.stdout != STORAGE_CAPABILITY_RESPONSE
        or result.stderr != b""
        or created_entries
    ):
        raise SystemExit(
            f"{label} failed exact managed storage/session capability "
            "verification"
        )


def require_publishable(target: str, manifest: dict) -> None:
    _require_target(target)
    publishable_assets: list[tuple[str, dict]] = []
    for revision, definition in manifest["xenia"]["revisions"].items():
        asset = definition["targets"].get(target)
        if asset is not None:
            publishable_assets.append((revision, asset))
        if asset is not None and asset["release_state"] != "published":
            raise SystemExit(
                f"Xenia {revision} for {target} is candidate-only; publish the "
                "reviewed downstream archive and record its HTTPS URL before "
                "creating a release package"
            )
        if asset is not None and (
            "gdox_persistent_content_saves_only"
            not in asset["managed_options"]
        ):
            raise SystemExit(
                f"Xenia {revision} for {target} lacks the required save-only "
                "persistent-content contract"
            )
        if asset is not None and (
            "gdox_disclaimer_acknowledged" not in asset["managed_options"]
        ):
            raise SystemExit(
                f"Xenia {revision} for {target} lacks managed disclaimer "
                "acknowledgement"
            )
    content_patch = (
        "packaging/xenia/patches/"
        "0004-gdox-ephemeral-game-content.patch"
    )
    if content_patch not in {
        patch["path"]
        for patch in manifest["xenia"]["integration"]["patches"]
    }:
        raise SystemExit(
            "Xenia release integration does not include the reviewed "
            "save-only content patch"
        )
    disclaimer_patch = (
        "packaging/xenia/patches/"
        "0005-gdox-managed-disclaimer.patch"
    )
    if disclaimer_patch not in {
        patch["path"]
        for patch in manifest["xenia"]["integration"]["patches"]
    }:
        raise SystemExit(
            "Xenia release integration does not include the reviewed "
            "managed disclaimer patch"
        )
    for revision, asset in publishable_assets:
        if asset["origin"] != "gdox-patched":
            raise SystemExit(
                f"Xenia {revision} for {target} is an unchanged upstream "
                "binary; the save-only storage contract requires a reviewed "
                "downstream build"
            )
        if not has_reviewed_save_only_capability(revision, target, asset):
            raise SystemExit(
                f"Xenia {revision} for {target} has not passed executable "
                "save-only storage capability verification"
            )
        if not has_reviewed_managed_session_capability(
            revision, target, asset
        ):
            raise SystemExit(
                f"Xenia {revision} for {target} has not passed executable "
                "managed-session capability verification"
            )


def _require_digest(value: object, label: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise SystemExit(f"{label} must be a lowercase SHA-256 digest")
    return value


def _require_size(value: object, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise SystemExit(f"{label} must be a positive integer")
    return value


def _validate_asset(asset: object, label: str, target: str) -> dict:
    if not isinstance(asset, dict) or set(asset) != ASSET_FIELDS:
        raise SystemExit(f"{label} has missing or unknown fields")
    origin = asset["origin"]
    release_state = asset["release_state"]
    if origin not in ASSET_ORIGINS:
        raise SystemExit(f"{label}.origin is invalid")
    if release_state not in RELEASE_STATES:
        raise SystemExit(f"{label}.release_state is invalid")
    url = asset["url"]
    if release_state == "candidate-only":
        if origin != "gdox-patched" or url is not None:
            raise SystemExit(
                f"{label} candidate-only assets must be unpublished GDOX builds"
            )
    elif not isinstance(url, str) or not url.startswith("https://"):
        raise SystemExit(f"{label}.url must be a published HTTPS URL")
    if origin == "upstream-release" and release_state != "published":
        raise SystemExit(f"{label} upstream assets must be published")
    archive_name = asset["archive_name"]
    if (
        not isinstance(archive_name, str)
        or not archive_name
        or archive_name != Path(archive_name).name
    ):
        raise SystemExit(f"{label}.archive_name is invalid")
    if isinstance(url, str) and url.rsplit("/", 1)[-1] != archive_name:
        raise SystemExit(f"{label}.url does not match archive_name")
    if origin == "gdox-patched" and isinstance(url, str) and url.startswith(
        (
            "https://github.com/xenia-canary/",
            "https://raw.githubusercontent.com/xenia-canary/",
        )
    ):
        raise SystemExit(f"{label}.url must identify the downstream archive")
    _require_digest(asset["sha256"], f"{label}.sha256")
    _require_size(asset["size"], f"{label}.size")
    if asset["format"] not in TARGET_FORMATS[target]:
        raise SystemExit(f"{label} has an unsupported format")
    expected_executable = (
        "xenia_canary_linux.AppImage"
        if asset["format"] == "linux-appimage"
        else "xenia_canary.exe"
    )
    if asset["executable"] != expected_executable:
        raise SystemExit(f"{label} has an unsupported executable")
    disc_transport = asset["disc_transport"]
    if disc_transport not in DISC_TRANSPORTS:
        raise SystemExit(f"{label}.disc_transport is invalid")
    if target != WINDOWS_TARGET and disc_transport != "image-path-v1":
        raise SystemExit(
            f"{label}.disc_transport is unsupported for this target"
        )
    if disc_transport == "gdox-private-nbd-v1" and (
        origin != "gdox-patched" or target != WINDOWS_TARGET
    ):
        raise SystemExit(
            f"{label}.disc_transport requires a patched Windows runtime"
        )
    if origin == "upstream-release" and disc_transport != "image-path-v1":
        raise SystemExit(f"{label} upstream runtimes require image-path-v1")
    if (
        origin == "gdox-patched"
        and target == WINDOWS_TARGET
        and asset["format"] != "windows-zip"
    ):
        raise SystemExit(f"{label} patched archives must use deterministic zip")
    managed_options = asset["managed_options"]
    if (
        not isinstance(managed_options, list)
        or any(not isinstance(option, str) for option in managed_options)
        or len(managed_options) != len(set(managed_options))
        or managed_options != sorted(managed_options)
        or not set(managed_options).issubset(MANAGED_OPTIONS)
    ):
        raise SystemExit(f"{label}.managed_options is invalid")
    _require_digest(
        asset["executable_sha256"], f"{label}.executable_sha256"
    )
    _require_size(asset["executable_size"], f"{label}.executable_size")
    if asset["format"] == "linux-appimage" and (
        asset["sha256"] != asset["executable_sha256"]
        or asset["size"] != asset["executable_size"]
    ):
        raise SystemExit(f"{label} AppImage payload must be the release asset")
    return asset


def _validate_local_input(asset: object, label: str, prefix: Path) -> dict:
    expected_fields = {"path", "sha256", "size"}
    if not isinstance(asset, dict) or set(asset) != expected_fields:
        raise SystemExit(f"{label} has missing or unknown fields")
    path_value = asset["path"]
    if not isinstance(path_value, str) or not path_value:
        raise SystemExit(f"{label}.path is invalid")
    relative = Path(path_value)
    if (
        relative.is_absolute()
        or ".." in relative.parts
        or prefix not in relative.parents
    ):
        raise SystemExit(f"{label}.path is outside {prefix}")
    _require_digest(asset["sha256"], f"{label}.sha256")
    _require_size(asset["size"], f"{label}.size")
    path = ROOT / relative
    if (
        not path.is_file()
        or path.stat().st_size != asset["size"]
        or file_sha256(path) != asset["sha256"]
    ):
        raise SystemExit(f"{label} failed local integrity validation")
    return asset


def _validate_integration(integration: object) -> None:
    expected_fields = {
        "patches",
        "upstream_repository",
        "windows_build",
    }
    if not isinstance(integration, dict) or set(integration) != expected_fields:
        raise SystemExit("Xenia integration has missing or unknown fields")
    if integration["upstream_repository"] != (
        "https://github.com/xenia-canary/xenia-canary.git"
    ):
        raise SystemExit("Xenia integration repository is invalid")
    patches = integration["patches"]
    if not isinstance(patches, list) or not patches:
        raise SystemExit(
            "Xenia integration must identify at least one reviewed patch"
        )
    for index, patch in enumerate(patches):
        _validate_local_input(
            patch,
            f"xenia.integration.patches[{index}]",
            Path("packaging/xenia/patches"),
        )
    if len({patch["path"] for patch in patches}) != len(patches):
        raise SystemExit("Xenia integration patches must be unique")

    build = integration["windows_build"]
    build_fields = {
        "cmake",
        "git",
        "msvc_compiler",
        "ninja",
        "packager",
        "python",
        "recipe",
        "vc_tools",
        "visual_studio",
        "vulkan_sdk",
        "windows_sdk",
    }
    if not isinstance(build, dict) or set(build) != build_fields:
        raise SystemExit("Xenia Windows build has missing or unknown fields")
    for field in (
        "cmake",
        "git",
        "msvc_compiler",
        "ninja",
        "python",
        "vc_tools",
        "visual_studio",
        "windows_sdk",
    ):
        if not isinstance(build[field], str) or not build[field]:
            raise SystemExit(f"Xenia Windows build {field} is invalid")
    _validate_local_input(
        build["recipe"],
        "xenia.integration.windows_build.recipe",
        Path("packaging/xenia"),
    )
    _validate_local_input(
        build["packager"],
        "xenia.integration.windows_build.packager",
        Path("scripts"),
    )
    vulkan = build["vulkan_sdk"]
    if not isinstance(vulkan, dict) or set(vulkan) != {
        "url",
        "sha256",
        "size",
        "version",
    }:
        raise SystemExit("Xenia Windows Vulkan SDK has missing or unknown fields")
    if (
        vulkan["url"] !=
        f"https://sdk.lunarg.com/sdk/download/{vulkan['version']}/windows/"
        f"vulkansdk-windows-X64-{vulkan['version']}.exe"
    ):
        raise SystemExit("Xenia Windows Vulkan SDK URL is invalid")
    _require_digest(
        vulkan["sha256"],
        "xenia.integration.windows_build.vulkan_sdk.sha256",
    )
    _require_size(
        vulkan["size"],
        "xenia.integration.windows_build.vulkan_sdk.size",
    )


def _validate_license(asset: object, label: str, commit: str) -> dict:
    expected_fields = {"url", "sha256", "size", "name"}
    if not isinstance(asset, dict) or set(asset) != expected_fields:
        raise SystemExit(f"{label} has missing or unknown fields")
    if asset["url"] != (
        f"https://raw.githubusercontent.com/xenia-canary/xenia-canary/{commit}/LICENSE"
    ):
        raise SystemExit(f"{label}.url must identify the exact source license")
    if asset["name"] != "LICENSE":
        raise SystemExit(f"{label}.name must be LICENSE")
    _require_digest(asset["sha256"], f"{label}.sha256")
    _require_size(asset["size"], f"{label}.size")
    return asset


def validate_runtime_manifest(manifest: dict) -> None:
    xenia = manifest.get("xenia")
    if not isinstance(xenia, dict):
        raise SystemExit("runtime manifest has no Xenia definition")
    if set(xenia) != {
        "default_revision",
        "integration",
        "license",
        "revisions",
    }:
        raise SystemExit("Xenia runtime definition has missing or unknown fields")
    if xenia["license"] != "BSD-3-Clause":
        raise SystemExit("Xenia runtime license must be BSD-3-Clause")
    _validate_integration(xenia["integration"])
    revisions = xenia["revisions"]
    if not isinstance(revisions, dict) or not revisions:
        raise SystemExit("runtime manifest has no Xenia revisions")
    if xenia["default_revision"] not in revisions:
        raise SystemExit("Xenia default revision is not bundled")

    for revision, definition in revisions.items():
        label = f"xenia.revisions.{revision}"
        if not isinstance(revision, str) or REVISION_PATTERN.fullmatch(revision) is None:
            raise SystemExit(f"invalid Xenia revision name: {revision}")
        if not isinstance(definition, dict) or set(definition) != {
            "commit",
            "license",
            "source",
            "release",
            "targets",
        }:
            raise SystemExit(f"{label} has missing or unknown fields")
        commit = definition["commit"]
        if (
            not isinstance(commit, str)
            or len(commit) != 40
            or REVISION_PATTERN.fullmatch(commit) is None
            or not commit.startswith(revision)
        ):
            raise SystemExit(f"{label}.commit must extend the revision to 40 digits")
        expected_source = (
            "https://github.com/xenia-canary/xenia-canary/tree/" + commit
        )
        if definition["source"] != expected_source:
            raise SystemExit(f"{label}.source must identify the exact commit")
        _validate_license(definition["license"], f"{label}.license", commit)
        release = definition["release"]
        release_marker = "/releases/tag/"
        if not isinstance(release, str) or release.count(release_marker) != 1:
            raise SystemExit(f"{label}.release must identify one GitHub release tag")
        release_repository, release_tag = release.split(release_marker, 1)
        if release_repository not in {
            "https://github.com/xenia-canary/xenia-canary",
            "https://github.com/xenia-canary/xenia-canary-releases",
        } or not release_tag or not revision.startswith(release_tag):
            raise SystemExit(f"{label}.release does not match the revision")
        targets = definition["targets"]
        if not isinstance(targets, dict) or set(targets) != set(TARGET_FORMATS):
            raise SystemExit(
                f"{label}.targets must define the supported Linux and Windows targets"
            )
        for target, asset in targets.items():
            _validate_asset(asset, f"{label}.targets.{target}", target)
            upstream_filename = {
                "windows-zip-proton": "xenia_canary_windows.zip",
                "windows-zip": "xenia_canary_windows.zip",
                "windows-7z": "xenia_canary_windows.7z",
                "linux-appimage": "xenia_canary_linux.AppImage",
            }[asset["format"]]
            expected_download = (
                f"{release_repository}/releases/download/{release_tag}/"
                f"{upstream_filename}"
            )
            if asset["origin"] == "upstream-release" and (
                asset["url"] != expected_download
                or asset["archive_name"] != upstream_filename
            ):
                raise SystemExit(
                    f"{label}.targets.{target}.url does not match the release"
                )

    try:
        compatibility = load_compatibility_manifest()
    except CompatibilityError as error:
        raise SystemExit(str(error)) from None
    if {runtime.revision for runtime in compatibility.runtimes} != set(revisions):
        raise SystemExit(
            "Xenia runtime and compatibility manifests declare different revisions"
        )
    if compatibility.default.revision != xenia["default_revision"]:
        raise SystemExit(
            "Xenia runtime and compatibility manifests have different defaults"
        )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _seven_zip() -> str:
    executable = shutil.which("7z") or shutil.which("7zz")
    if executable is None:
        raise SystemExit("7z is required to validate the Xenia Windows runtime")
    return executable


def _validate_zip(archive_path: Path, asset: dict, label: str) -> None:
    expected_members = {"LICENSE", asset["executable"]}
    try:
        with zipfile.ZipFile(archive_path) as archive:
            infos = archive.infolist()
            members = [info.filename for info in infos]
            if (
                len(infos) != len(expected_members)
                or any(info.is_dir() for info in infos)
                or len(members) != len(set(members))
                or set(members) != expected_members
            ):
                raise SystemExit(
                    f"{label} must contain only LICENSE and {asset['executable']}"
                )
            info = archive.getinfo(asset["executable"])
            if info.file_size != asset["executable_size"]:
                raise SystemExit(f"{label} executable has the wrong size")
            digest = hashlib.sha256()
            with archive.open(info) as executable:
                for chunk in iter(lambda: executable.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest() != asset["executable_sha256"]:
                raise SystemExit(f"{label} executable failed its integrity check")
    except zipfile.BadZipFile as error:
        raise SystemExit(f"{label} is not a valid zip archive: {error}") from None


def _seven_zip_entries(archive_path: Path, label: str) -> dict[str, int]:
    result = subprocess.run(
        [_seven_zip(), "l", "-ba", "-slt", str(archive_path)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"{label} is not a valid 7z archive")
    entries: dict[str, int] = {}
    current_path: str | None = None
    for line in result.stdout.splitlines():
        if line.startswith("Path = "):
            current_path = line.removeprefix("Path = ")
        elif line.startswith("Size = ") and current_path is not None:
            try:
                size = int(line.removeprefix("Size = "))
            except ValueError:
                raise SystemExit(f"{label} has invalid member metadata") from None
            if current_path in entries:
                raise SystemExit(f"{label} contains a duplicate member")
            entries[current_path] = size
            current_path = None
    return entries


def _seven_zip_member_sha256(
    archive_path: Path, member: str, label: str
) -> str:
    result = subprocess.run(
        [_seven_zip(), "x", "-so", str(archive_path), member],
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"could not read {member} from {label}: {detail}")
    return hashlib.sha256(result.stdout).hexdigest()


def _validate_7z(archive_path: Path, asset: dict, label: str) -> None:
    expected_members = {"LICENSE", asset["executable"]}
    entries = _seven_zip_entries(archive_path, label)
    if set(entries) != expected_members:
        raise SystemExit(
            f"{label} must contain only LICENSE and {asset['executable']}"
        )
    if entries[asset["executable"]] != asset["executable_size"]:
        raise SystemExit(f"{label} executable has the wrong size")
    if _seven_zip_member_sha256(
        archive_path, asset["executable"], label
    ) != asset["executable_sha256"]:
        raise SystemExit(f"{label} executable failed its integrity check")


def _validate_appimage(archive_path: Path, asset: dict, label: str) -> None:
    with archive_path.open("rb") as source:
        header = source.read(12)
    if (
        len(header) != 12
        or header[:4] != b"\x7fELF"
        or header[8:11] != b"AI\x02"
        or archive_path.stat().st_size != asset["executable_size"]
        or file_sha256(archive_path) != asset["executable_sha256"]
    ):
        raise SystemExit(f"{label} is not the reviewed type-2 AppImage")


def validate_archive(
    archive_path: Path,
    asset: dict,
    label: str,
) -> None:
    if (
        not archive_path.is_file()
        or archive_path.stat().st_size != asset["size"]
        or file_sha256(archive_path) != asset["sha256"]
    ):
        raise SystemExit(f"{label} archive failed its integrity check")
    match asset["format"]:
        case "windows-zip" | "windows-zip-proton":
            _validate_zip(archive_path, asset, label)
        case "windows-7z":
            _validate_7z(archive_path, asset, label)
        case "linux-appimage":
            _validate_appimage(archive_path, asset, label)
        case other:
            raise SystemExit(f"unsupported Xenia archive format: {other}")


def _extract_archive(archive_path: Path, destination: Path, asset: dict) -> None:
    if asset["format"] in {"windows-zip", "windows-zip-proton"}:
        with (
            zipfile.ZipFile(archive_path) as archive,
            archive.open(asset["executable"]) as source,
            (
                destination / asset["executable"]
            ).open("wb") as output,
        ):
            shutil.copyfileobj(source, output)
        return
    if asset["format"] == "windows-7z":
        output = destination / asset["executable"]
        with output.open("wb") as target:
            result = subprocess.run(
                [
                    _seven_zip(),
                    "x",
                    "-so",
                    str(archive_path),
                    asset["executable"],
                ],
                check=False,
                stdout=target,
                stderr=subprocess.PIPE,
            )
        if result.returncode != 0:
            output.unlink(missing_ok=True)
            raise SystemExit("could not extract the Xenia Windows executable")
        return
    if asset["format"] == "linux-appimage":
        shutil.copy2(archive_path, destination / asset["executable"])
        (destination / asset["executable"]).chmod(0o755)
        return
    raise SystemExit(f"unsupported Xenia archive format: {asset['format']}")


def _render_launcher(destination: Path, asset: dict) -> None:
    template_name = (
        "xenia-native"
        if asset["format"] == "linux-appimage"
        else "xenia-proton"
    )
    template = (ROOT / "packaging" / "linux" / template_name).read_text(
        encoding="utf-8"
    )
    replacements = {
        "@GDOX_XENIA_EXECUTABLE@": asset["executable"],
        "@GDOX_XENIA_EXECUTABLE_SIZE@": str(asset["executable_size"]),
        "@GDOX_XENIA_EXECUTABLE_SHA256@": asset["executable_sha256"],
        "@GDOX_XENIA_INJECTION_ENVIRONMENT@": (
            render_injection_environment_policy()
        ),
    }
    for marker, value in replacements.items():
        if template.count(marker) != 1:
            raise SystemExit(f"Xenia launcher template must contain one {marker}")
        template = template.replace(marker, value)
    destination.write_text(template, encoding="utf-8")
    destination.chmod(0o755)


def bundle(
    target: str,
    destination: Path,
    cache: Path,
    manifest: dict,
    fetch_asset: FetchAsset,
    *,
    allow_candidate: bool = False,
) -> int:
    _require_target(target)
    if not allow_candidate:
        require_publishable(target, manifest)
    bundled = 0
    for revision, definition in manifest["xenia"]["revisions"].items():
        asset = definition["targets"].get(target)
        if asset is None:
            continue
        if asset["release_state"] != "published" and not allow_candidate:
            raise SystemExit(f"Xenia {revision} is not publishable")
        archive = fetch_asset(asset, cache)
        label = f"Xenia {revision} for {target}"
        validate_archive(archive, asset, label)
        revision_directory = destination / "xenia" / revision
        revision_directory.mkdir(parents=True)
        _extract_archive(archive, revision_directory, asset)
        license_asset = definition["license"]
        shutil.copy2(
            fetch_asset(license_asset, cache), revision_directory / "LICENSE"
        )
        executable = revision_directory / asset["executable"]
        if (
            executable.stat().st_size != asset["executable_size"]
            or file_sha256(executable) != asset["executable_sha256"]
        ):
            raise SystemExit(f"verification failed for extracted {label}")
        if asset["format"] in {"windows-zip-proton", "linux-appimage"}:
            _render_launcher(revision_directory / "xenia", asset)
        bundled += 1
    return bundled


def bundle_publishable(
    target: str,
    destination: Path,
    cache: Path,
    manifest: dict,
    fetch_asset: FetchAsset,
) -> int:
    return bundle(
        target,
        destination,
        cache,
        manifest,
        fetch_asset,
        allow_candidate=False,
    )


def validate_stage(target: str, runtime_root: Path, manifest: dict) -> None:
    _require_target(target)
    expected_revisions = set()
    for revision, definition in manifest["xenia"]["revisions"].items():
        asset = definition["targets"].get(target)
        if asset is None:
            continue
        expected_revisions.add(revision)
        directory = runtime_root / "xenia" / revision
        executable = directory / asset["executable"]
        license_file = directory / "LICENSE"
        allowed_entries = {"LICENSE", asset["executable"]}
        if asset["format"] in {"windows-zip-proton", "linux-appimage"}:
            allowed_entries.add("xenia")
        if not directory.is_dir() or {
            path.name for path in directory.iterdir()
        } != allowed_entries:
            raise SystemExit(
                f"staged Xenia runtime has missing or unexpected contents: {revision}"
            )
        if not license_file.is_file():
            raise SystemExit(f"staged Xenia license is missing: {revision}")
        license_asset = definition["license"]
        if (
            license_file.stat().st_size != license_asset["size"]
            or file_sha256(license_file) != license_asset["sha256"]
        ):
            raise SystemExit(f"staged Xenia license failed verification: {revision}")
        if (
            not executable.is_file()
            or executable.stat().st_size != asset["executable_size"]
            or file_sha256(executable) != asset["executable_sha256"]
        ):
            raise SystemExit(
                f"staged Xenia executable failed verification: {revision}"
            )
        launcher = directory / "xenia"
        if asset["format"] in {"windows-zip-proton", "linux-appimage"} and not (
            launcher.is_file() and launcher.stat().st_mode & 0o111
        ):
            raise SystemExit(f"staged Xenia launcher is not executable: {revision}")

    xenia_root = runtime_root / "xenia"
    if not expected_revisions:
        if xenia_root.exists():
            raise SystemExit(f"Xenia is not supported for runtime target {target}")
        return
    if not xenia_root.is_dir():
        raise SystemExit(f"Xenia runtime is missing for runtime target {target}")
    runtime_entries = list(xenia_root.iterdir())
    actual_revisions = {path.name for path in runtime_entries}
    if (
        any(not path.is_dir() for path in runtime_entries)
        or actual_revisions != expected_revisions
    ):
        raise SystemExit("staged Xenia revisions differ from the runtime manifest")
