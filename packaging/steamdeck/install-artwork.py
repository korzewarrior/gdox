#!/usr/bin/env python3
"""Install GDOX artwork for existing Steam shortcuts."""

from __future__ import annotations

import os
import shutil
import struct
import sys
from pathlib import Path


class VdfError(ValueError):
    pass


ARTWORK_NAMES = {
    "grid.png": "{app_id}.png",
    "portrait.png": "{app_id}p.png",
    "hero.png": "{app_id}_hero.png",
    "logo.png": "{app_id}_logo.png",
    "icon.png": "{app_id}_icon.png",
}


def read_string(data: bytes, offset: int) -> tuple[str, int]:
    end = data.find(b"\0", offset)
    if end < 0:
        raise VdfError("unterminated Steam shortcut string")
    return data[offset:end].decode("utf-8", errors="replace"), end + 1


def read_object(data: bytes, offset: int) -> tuple[dict[str, object], int]:
    result: dict[str, object] = {}
    while offset < len(data):
        value_type = data[offset]
        offset += 1
        if value_type == 8:
            return result, offset
        key, offset = read_string(data, offset)
        if value_type == 0:
            value, offset = read_object(data, offset)
        elif value_type == 1:
            value, offset = read_string(data, offset)
        elif value_type == 2:
            if offset + 4 > len(data):
                raise VdfError("truncated Steam shortcut integer")
            value = struct.unpack_from("<i", data, offset)[0]
            offset += 4
        else:
            raise VdfError(f"unsupported Steam shortcut value type {value_type}")
        result[key] = value
    raise VdfError("unterminated Steam shortcut object")


def gdox_app_ids(shortcuts_file: Path) -> list[int]:
    data = shortcuts_file.read_bytes()
    root, offset = read_object(data, 0)
    if offset != len(data):
        raise VdfError("unexpected data after Steam shortcuts")
    shortcuts = root.get("shortcuts")
    if not isinstance(shortcuts, dict):
        return []
    app_ids: list[int] = []
    for shortcut in shortcuts.values():
        if not isinstance(shortcut, dict):
            continue
        app_name = shortcut.get("AppName", shortcut.get("appname"))
        app_id = shortcut.get("appid")
        if app_name == "GDOX" and isinstance(app_id, int):
            app_ids.append(app_id & 0xFFFFFFFF)
    return app_ids


def install_for_user(user_directory: Path, artwork: Path) -> int:
    shortcuts = user_directory / "config" / "shortcuts.vdf"
    if not shortcuts.is_file():
        return 0
    try:
        app_ids = gdox_app_ids(shortcuts)
    except (OSError, VdfError) as error:
        print(f"Could not inspect {shortcuts}: {error}", file=sys.stderr)
        return 0
    missing = [name for name in ARTWORK_NAMES if not (artwork / name).is_file()]
    if missing:
        print(
            "Steam artwork is incomplete: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 0
    grid = user_directory / "config" / "grid"
    installed = 0
    for app_id in app_ids:
        grid.mkdir(parents=True, exist_ok=True)
        for source_name, destination_name in ARTWORK_NAMES.items():
            source = artwork / source_name
            shutil.copyfile(
                source,
                grid / destination_name.format(app_id=app_id),
            )
        installed += 1
    return installed


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: install-artwork.py ARTWORK_DIRECTORY", file=sys.stderr)
        return 2
    artwork = Path(sys.argv[1]).resolve()
    data_home = Path(
        os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")
    )
    userdata = data_home / "Steam" / "userdata"
    installed = sum(
        install_for_user(directory, artwork)
        for directory in userdata.iterdir()
        if directory.is_dir()
    ) if userdata.is_dir() else 0
    if installed:
        print(f"Installed Steam artwork for {installed} GDOX shortcut(s).")
        return 0
    print("Steam has not created the GDOX shortcut yet.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
