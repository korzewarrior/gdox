#!/usr/bin/env python3
"""Reject code and public content outside the Original Xbox release scope."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {
    "",
    ".c",
    ".cmake",
    ".cpp",
    ".css",
    ".h",
    ".html",
    ".in",
    ".java",
    ".json",
    ".kt",
    ".kts",
    ".md",
    ".patch",
    ".plist",
    ".pro",
    ".ps1",
    ".py",
    ".rules",
    ".sh",
    ".txt",
    ".xml",
    ".yml",
}
FORBIDDEN = (
    re.compile("xbox" + r"[\s_-]*3" + "60", re.IGNORECASE),
    re.compile("xe" + "nia", re.IGNORECASE),
    re.compile(r"xgd[23]", re.IGNORECASE),
    re.compile("game" + r"[\s_-]*" + "cube", re.IGNORECASE),
    re.compile("dol" + "phin", re.IGNORECASE),
    re.compile("dream" + r"[\s_-]*" + "cast", re.IGNORECASE),
    re.compile("red" + "ream", re.IGNORECASE),
    re.compile("fly" + "cast", re.IGNORECASE),
)


def files() -> list[Path]:
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return sorted(
        ROOT / name.decode()
        for name in result.stdout.split(b"\0")
        if name and (ROOT / name.decode()).is_file()
    )


def main() -> int:
    failures: list[str] = []

    for path in files():
        relative = path.relative_to(ROOT).as_posix()
        for rule in FORBIDDEN:
            if rule.search(relative):
                failures.append(f"{relative}: excluded feature path")
        if path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(text.splitlines(), 1):
            for rule in FORBIDDEN:
                if rule.search(line):
                    failures.append(
                        f"{relative}:{line_number}: excluded feature reference"
                    )

    if failures:
        for failure in failures:
            print(f"Original Xbox release audit: {failure}", file=sys.stderr)
        return 1
    print("Original Xbox release scope is clean.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
