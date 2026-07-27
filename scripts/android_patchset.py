#!/usr/bin/env python3
"""Apply or validate an ordered Android patch series."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

EXCLUDED_PATHS = {
    Path(".gdox-patch-set"),
    Path("android/key.properties"),
    Path("android/local.properties"),
}
EXCLUDED_PREFIXES = (
    Path("android/.gradle"),
    Path("android/app/.cxx"),
    Path("android/app/build"),
    Path("android/build"),
)


def git(
    repository: Path,
    *arguments: str,
    environment: dict[str, str] | None = None,
) -> bytes:
    return subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        capture_output=True,
        env=environment,
    ).stdout


def patch_files(patch_root: Path) -> list[Path]:
    series = patch_root / "series"
    if not series.is_file():
        raise RuntimeError(f"patch series is missing: {series}")

    patches: list[Path] = []
    for line in series.read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        patch = patch_root / name
        if not patch.is_file():
            raise RuntimeError(f"patch listed in series is missing: {patch}")
        patches.append(patch)
    if not patches:
        raise RuntimeError(f"patch series is empty: {series}")
    return patches


def index_environment(directory: Path) -> dict[str, str]:
    return os.environ | {"GIT_INDEX_FILE": str(directory / "index")}


def is_source_path(path: Path) -> bool:
    return path not in EXCLUDED_PATHS and not any(
        path == prefix or prefix in path.parents for prefix in EXCLUDED_PREFIXES
    )


def source_files(repository: Path) -> list[Path]:
    names = git(
        repository,
        "ls-files",
        "-z",
        "--cached",
        "--others",
        "--exclude-standard",
    )
    return sorted(
        path
        for name in names.split(b"\0")
        if name and is_source_path(path := Path(name.decode()))
    )


def expected_tree(repository: Path, patch_root: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="gdox-expected-index-") as name:
        environment = index_environment(Path(name))
        git(repository, "read-tree", "HEAD", environment=environment)
        for patch in patch_files(patch_root):
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repository),
                    "apply",
                    "--cached",
                    "--whitespace=error-all",
                    str(patch),
                ],
                check=True,
                env=environment,
                capture_output=True,
            )
        return git(repository, "write-tree", environment=environment).strip()


def working_tree(repository: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="gdox-working-index-") as name:
        environment = index_environment(Path(name))
        git(repository, "read-tree", "HEAD", environment=environment)
        git(repository, "add", "--update", environment=environment)
        untracked = git(
            repository,
            "ls-files",
            "-z",
            "--others",
            "--exclude-standard",
        )
        paths = [
            name
            for name in untracked.split(b"\0")
            if name and is_source_path(Path(name.decode()))
        ]
        if paths:
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repository),
                    "add",
                    "--pathspec-from-file=-",
                    "--pathspec-file-nul",
                ],
                check=True,
                input=b"\0".join(paths) + b"\0",
                capture_output=True,
                env=environment,
            )
        return git(repository, "write-tree", environment=environment).strip()


def validate_applied(repository: Path, patch_root: Path) -> None:
    if working_tree(repository) != expected_tree(repository, patch_root):
        raise RuntimeError(
            f"{repository} does not exactly match {patch_root / 'series'}"
        )


def ensure_applied(repository: Path, patch_root: Path) -> None:
    expected = expected_tree(repository, patch_root)
    if working_tree(repository) == expected:
        return
    status = git(repository, "status", "--porcelain")
    if status:
        raise RuntimeError(
            f"{repository} has changes that do not match the patch series"
        )
    for patch in patch_files(patch_root):
        subprocess.run(
            [
                "git",
                "-C",
                str(repository),
                "apply",
                "--whitespace=error-all",
                str(patch),
            ],
            check=True,
        )
    if working_tree(repository) != expected:
        raise RuntimeError("the applied patch series did not reproduce exactly")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path)
    parser.add_argument("patch_root", type=Path)
    parser.add_argument(
        "--validate",
        action="store_true",
        help="validate only; do not apply a missing series",
    )
    args = parser.parse_args()
    operation = validate_applied if args.validate else ensure_applied
    operation(args.repository.resolve(), args.patch_root.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Android patch validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
