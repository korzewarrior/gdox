#!/usr/bin/env python3
"""Enforce CMake as the single source of the GDOX release version."""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from project_version import project_version, validated_project_version

ROOT = Path(__file__).resolve().parents[1]
DERIVED_VERSION_FILES = (
    "scripts/build_linux_packages.py",
    "scripts/package_android_source.py",
    "scripts/package_private_candidate.py",
    "scripts/package_release.py",
    "scripts/package_source.py",
    "scripts/release_notes.py",
)
CURRENT_DOCUMENTS = (
    "README.md",
    "docs/ARCHITECTURE.md",
    "docs/DEVELOPMENT.md",
    "docs/RELEASING.md",
    "docs/STATUS.md",
    "docs/TROUBLESHOOTING.md",
    "docs/USER_GUIDE.md",
)


def main() -> int:
    failures: list[str] = []
    version = project_version()

    if validated_project_version(f"v{version}") != version:
        failures.append("tag-prefixed project version validation is inconsistent")
    try:
        validated_project_version("999.999.999")
    except ValueError:
        pass
    else:
        failures.append("mismatched release versions are not rejected")

    for name in DERIVED_VERSION_FILES:
        text = (ROOT / name).read_text(encoding="utf-8")
        if "validated_project_version" not in text:
            failures.append(f"{name} does not validate against the CMake version")

    for name in CURRENT_DOCUMENTS:
        if version in (ROOT / name).read_text(encoding="utf-8"):
            failures.append(f"{name} hard-codes the current release version")

    android_patch = (
        ROOT / "android/emulator/patches/0002-gdox-android-platform.patch"
    ).read_text(encoding="utf-8")
    for required in (
        'providers.gradleProperty("gdoxVersion")',
        "versionCode = gdoxVersionCode",
        "versionName = gdoxVersionName",
    ):
        if required not in android_patch:
            failures.append("Android metadata does not derive from gdoxVersion")
            break
    if re.search(r'versionName\s*=\s*"[0-9]', android_patch):
        failures.append("Android versionName is hard-coded")
    if re.search(r"versionCode\s*=\s*[0-9]", android_patch):
        failures.append("Android versionCode is hard-coded")

    if failures:
        for failure in failures:
            print(f"version audit: {failure}", file=sys.stderr)
        return 1
    print(f"Release version {version} is derived from CMake.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
