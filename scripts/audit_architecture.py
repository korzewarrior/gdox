#!/usr/bin/env python3
"""Enforce GDOX architecture boundaries."""

from __future__ import annotations

import sys
from pathlib import Path

sys.dont_write_bytecode = True

from architecture_audit import audit_repository

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    failures = audit_repository(ROOT)
    if failures:
        for failure in failures:
            print(f"architecture audit: {failure}", file=sys.stderr)
        return 1
    print("Architecture boundaries are clean.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
