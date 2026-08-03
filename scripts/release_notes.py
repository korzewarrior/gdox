#!/usr/bin/env python3
"""Extract one reviewed release section from the project changelog."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from project_version import ROOT, validated_project_version

HEADING = re.compile(
    r"^## (?P<version>[0-9]+(?:\.[0-9]+){2})(?P<unreleased> \(unreleased\))?$"
)


def release_notes(text: str, version: str, *, require_released: bool) -> str:
    lines = text.splitlines()
    matches = [
        (index, match)
        for index, line in enumerate(lines)
        if (match := HEADING.fullmatch(line)) and match.group("version") == version
    ]
    if len(matches) != 1:
        raise ValueError(
            f"CHANGELOG.md must contain exactly one section for {version}"
        )

    start, match = matches[0]
    if require_released and match.group("unreleased") is not None:
        raise ValueError(f"CHANGELOG.md still marks {version} as unreleased")

    end = next(
        (index for index in range(start + 1, len(lines)) if lines[index].startswith("## ")),
        len(lines),
    )
    body = "\n".join(lines[start + 1 : end]).strip()
    if not body:
        raise ValueError(f"CHANGELOG.md has no release notes for {version}")
    return body + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        help="release version, optionally prefixed by v; defaults to the project version",
    )
    parser.add_argument(
        "--require-released",
        action="store_true",
        help="reject a changelog section still marked unreleased",
    )
    parser.add_argument("--output", type=Path, help="write notes to this file")
    arguments = parser.parse_args()

    try:
        version = validated_project_version(arguments.version)
        notes = release_notes(
            (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"),
            version,
            require_released=arguments.require_released,
        )
        if arguments.output is None:
            sys.stdout.write(notes)
        else:
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            arguments.output.write_text(notes, encoding="utf-8")
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
