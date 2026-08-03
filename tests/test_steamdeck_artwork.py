"""Validate canonical artwork and each platform's derived application icon."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARTWORK = ROOT / "packaging" / "steamdeck" / "artwork"
INSTALLER = ROOT / "packaging" / "steamdeck" / "install-artwork.py"
WINDOWS_ICON = ROOT / "packaging" / "windows" / "GDOX.ico"
MACOS_ICON = ROOT / "packaging" / "macos" / "GDOX.icns"
ANDROID_RESOURCES = ROOT / "android" / "app" / "src" / "main" / "res"
ANDROID_ICON = ANDROID_RESOURCES / "drawable-nodpi" / "gdox_icon.png"
sys.path.insert(0, str(ROOT / "scripts"))
from package_release import payload_inventory, validate_payload_paths

EXPECTED = {
    "grid.png": (920, 430),
    "portrait.png": (600, 900),
    "hero.png": (1920, 620),
    "logo.png": (1280, 720),
    "icon.png": (512, 512),
}
CANONICAL_ICON_SHA256 = (
    "02b755e7bd43675311e62276a3cd3ef3083359e61383fff3359de02120eb929c"
)
WINDOWS_ICON_SHA256 = (
    "e46aab2b76a7dda12b3f5a7ba1ba7227551d31d72c093c1f88e6de64d7d2e745"
)
WINDOWS_ICON_GEOMETRIES = (256, 128, 64, 48, 32, 24, 16)
MACOS_ICON_SHA256 = (
    "fac28d1139f565ad7cddaf8182b9e77f7edeb925437ce234b68ed038f666b7ea"
)
MACOS_ICON_GEOMETRIES = (
    ("icp4", 16),
    ("ic11", 32),
    ("icp5", 32),
    ("ic12", 64),
    ("icp6", 64),
    ("ic07", 128),
    ("ic13", 256),
    ("ic08", 256),
    ("ic14", 512),
    ("ic09", 512),
    ("ic10", 1024),
)


def load_installer():
    specification = importlib.util.spec_from_file_location(
        "gdox_steamdeck_artwork", INSTALLER
    )
    if specification is None or specification.loader is None:
        raise RuntimeError("could not load the Steam Deck artwork installer")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def png_geometry(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError(f"{path.name} is not a canonical PNG")
    width, height, depth, color, compression, filtering, interlace = (
        struct.unpack(">IIBBBBB", data[16:29])
    )
    if depth != 8 or color not in {2, 6}:
        raise ValueError(f"{path.name} must use 8-bit RGB or RGBA")
    if compression != 0 or filtering != 0 or interlace != 0:
        raise ValueError(f"{path.name} uses unsupported PNG encoding")
    return width, height


def ico_geometries(path: Path) -> tuple[tuple[int, int, int], ...]:
    data = path.read_bytes()
    if len(data) < 6:
        raise ValueError(f"{path.name} has a truncated ICO header")
    reserved, image_type, count = struct.unpack_from("<HHH", data)
    if reserved != 0 or image_type != 1 or count == 0:
        raise ValueError(f"{path.name} is not a Windows icon")
    if len(data) < 6 + count * 16:
        raise ValueError(f"{path.name} has a truncated ICO directory")
    geometries = []
    for index in range(count):
        width, height, _colors, _, _, depth, size, offset = struct.unpack_from(
            "<BBBBHHII", data, 6 + index * 16
        )
        width = width or 256
        height = height or 256
        if size == 0 or offset < 6 + count * 16 or offset + size > len(data):
            raise ValueError(f"{path.name} has an invalid ICO image entry")
        geometries.append((width, height, depth))
    return tuple(geometries)


def icns_geometries(path: Path) -> tuple[tuple[str, int, int], ...]:
    data = path.read_bytes()
    if len(data) < 8 or data[:4] != b"icns":
        raise ValueError(f"{path.name} is not a macOS icon")
    declared_size = struct.unpack_from(">I", data, 4)[0]
    if declared_size != len(data):
        raise ValueError(f"{path.name} has an invalid ICNS container size")
    offset = 8
    geometries = []
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError(f"{path.name} has a truncated ICNS entry")
        kind = data[offset : offset + 4].decode("ascii")
        size = struct.unpack_from(">I", data, offset + 4)[0]
        if size < 8 or offset + size > len(data):
            raise ValueError(f"{path.name} has an invalid ICNS entry")
        png = data[offset + 8 : offset + size]
        if png[:8] != b"\x89PNG\r\n\x1a\n" or png[12:16] != b"IHDR":
            raise ValueError(f"{path.name} contains a non-PNG icon entry")
        width, height = struct.unpack_from(">II", png, 16)
        geometries.append((kind, width, height))
        offset += size
    return tuple(geometries)


def shortcut_file(app_id: int) -> bytes:
    return b"".join(
        (
            b"\x00shortcuts\0",
            b"\x000\0",
            b"\x01AppName\0GDOX\0",
            b"\x02appid\0",
            struct.pack("<i", app_id),
            b"\x08\x08\x08",
        )
    )


class PlatformArtworkTest(unittest.TestCase):
    def test_source_inventory_and_png_geometry_are_exact(self) -> None:
        files = {
            path.name for path in ARTWORK.iterdir() if path.is_file()
        }
        self.assertEqual(files, set(EXPECTED))
        for name, geometry in EXPECTED.items():
            self.assertEqual(png_geometry(ARTWORK / name), geometry, name)

    def test_windows_icon_matches_the_canonical_artwork_revision(self) -> None:
        self.assertEqual(
            hashlib.sha256((ARTWORK / "icon.png").read_bytes()).hexdigest(),
            CANONICAL_ICON_SHA256,
        )
        self.assertEqual(
            hashlib.sha256(WINDOWS_ICON.read_bytes()).hexdigest(),
            WINDOWS_ICON_SHA256,
        )
        self.assertEqual(
            ico_geometries(WINDOWS_ICON),
            tuple((size, size, 32) for size in WINDOWS_ICON_GEOMETRIES),
        )

    def test_macos_icon_matches_the_canonical_artwork_revision(self) -> None:
        self.assertEqual(
            hashlib.sha256(MACOS_ICON.read_bytes()).hexdigest(),
            MACOS_ICON_SHA256,
        )
        self.assertEqual(
            icns_geometries(MACOS_ICON),
            tuple(
                (kind, size, size)
                for kind, size in MACOS_ICON_GEOMETRIES
            ),
        )

    def test_android_icon_matches_the_canonical_artwork_revision(self) -> None:
        self.assertEqual(
            ANDROID_ICON.read_bytes(),
            (ARTWORK / "icon.png").read_bytes(),
        )
        for name in ("gdox_launcher.xml", "gdox_launcher_round.xml"):
            launcher = (
                ANDROID_RESOURCES / "mipmap-anydpi-v26" / name
            ).read_text(encoding="utf-8")
            self.assertIn('@drawable/gdox_icon"', launcher)
            self.assertIn('@android:color/transparent"', launcher)
        self.assertFalse(
            (ANDROID_RESOURCES / "drawable" / "gdox_icon_foreground.xml")
            .exists()
        )
        self.assertFalse(
            (ANDROID_RESOURCES / "values" / "gdox_colors.xml").exists()
        )

    def test_installer_maps_every_asset_to_the_steam_shortcut(self) -> None:
        installer = load_installer()
        self.assertEqual(set(installer.ARTWORK_NAMES), set(EXPECTED))
        with tempfile.TemporaryDirectory(
            prefix="gdox-steamdeck-artwork-"
        ) as directory:
            user = Path(directory) / "1234"
            shortcuts = user / "config" / "shortcuts.vdf"
            shortcuts.parent.mkdir(parents=True)
            shortcuts.write_bytes(shortcut_file(123456789))

            self.assertEqual(installer.install_for_user(user, ARTWORK), 1)
            grid = user / "config" / "grid"
            for source_name, destination in installer.ARTWORK_NAMES.items():
                installed = grid / destination.format(app_id=123456789)
                self.assertEqual(
                    installed.read_bytes(),
                    (ARTWORK / source_name).read_bytes(),
                    source_name,
                )

    def test_release_inventory_includes_only_canonical_artwork(self) -> None:
        exact, prefixes = payload_inventory(
            "x86_64-steamdeck-linux-gnu",
            "steamdeck",
            None,
            runtime_included=False,
        )
        artwork_prefix = "packaging/steam-artwork/"
        packaged_artwork = {
            name.removeprefix(artwork_prefix)
            for name in exact
            if name.startswith(artwork_prefix)
        }
        self.assertEqual(packaged_artwork, set(EXPECTED))

        with tempfile.TemporaryDirectory(
            prefix="gdox-steamdeck-inventory-"
        ) as directory:
            stage = Path(directory)
            for relative in exact:
                path = stage / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            legacy = stage / artwork_prefix / "grid-old.png"
            legacy.write_bytes(b"legacy")
            with self.assertRaisesRegex(SystemExit, "unexpected files"):
                validate_payload_paths(stage, exact, prefixes)

    def test_installer_refuses_an_incomplete_artwork_set(self) -> None:
        installer = load_installer()
        with tempfile.TemporaryDirectory(
            prefix="gdox-steamdeck-artwork-"
        ) as directory:
            root = Path(directory)
            user = root / "1234"
            shortcuts = user / "config" / "shortcuts.vdf"
            shortcuts.parent.mkdir(parents=True)
            shortcuts.write_bytes(shortcut_file(123456789))
            incomplete = root / "artwork"
            incomplete.mkdir()
            (incomplete / "grid.png").write_bytes(b"fixture")

            diagnostics = io.StringIO()
            with redirect_stderr(diagnostics):
                self.assertEqual(
                    installer.install_for_user(user, incomplete), 0
                )
            self.assertIn("Steam artwork is incomplete", diagnostics.getvalue())
            self.assertFalse((user / "config" / "grid").exists())


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    unittest.main()
