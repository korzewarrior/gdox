#!/usr/bin/env python3
"""Read and validate the release version declared by the CMake project."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
PROJECT_DECLARATION = re.compile(
    r"\bproject\s*\(\s*gdox\s+VERSION\s+([0-9]+(?:\.[0-9]+){2})\b",
    re.DOTALL,
)


def project_version() -> str:
    match = PROJECT_DECLARATION.search(
        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    )
    if match is None:
        raise RuntimeError("CMakeLists.txt does not declare the gdox project version")
    return match.group(1)


def validated_project_version(requested: str | None) -> str:
    expected = project_version()
    if requested is None:
        return expected
    actual = requested.removeprefix("v")
    if actual != expected:
        raise ValueError(
            f"release version {actual!r} does not match CMake project version {expected!r}"
        )
    return expected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--validate",
        metavar="VERSION",
        help="require VERSION (optionally prefixed by v) to match CMake",
    )
    arguments = parser.parse_args()
    try:
        print(validated_project_version(arguments.validate))
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
