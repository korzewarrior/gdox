"""Shared locations for generated release data."""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def output_root() -> Path:
    configured = os.environ.get("GDOX_OUTPUT_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    return (ROOT.parent / "gdox-output").resolve()


def cache_root() -> Path:
    configured = os.environ.get("GDOX_CACHE_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Caches"
    elif os.name == "nt":
        default = Path.home() / "AppData" / "Local"
        base = Path(os.environ.get("LOCALAPPDATA", default))
    else:
        base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return (base / "gdox" / "release").resolve()
