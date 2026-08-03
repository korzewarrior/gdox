"""Verify Linux launcher session selection without starting GDOX."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from package_release import _set_linux_host_profile

SESSION_VARIABLES = (
    "DESKTOP_SESSION",
    "GDOX_GAMING_MODE",
    "GDOX_HOST_PROFILE",
    "LD_LIBRARY_PATH",
    "SteamGamepadUI",
    "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_DESKTOP",
)


class LinuxLauncherTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="gdox-linux-launcher-"
        )
        self.root = Path(self.temporary.name)
        launcher = self.root / "gdox"
        shutil.copy2(ROOT / "packaging" / "linux" / "gdox", launcher)
        launcher.chmod(0o755)
        executable = self.root / "libexec" / "gdox"
        executable.parent.mkdir()
        executable.write_text(
            "#!/bin/sh\n"
            "printf 'gaming=%s\\n' \"${GDOX_GAMING_MODE-unset}\"\n"
            "printf 'host=%s\\n' \"${GDOX_HOST_PROFILE-unset}\"\n"
            "printf 'libraries=%s\\n' \"${LD_LIBRARY_PATH-unset}\"\n",
            encoding="utf-8",
        )
        executable.chmod(0o755)
        bridge_launcher = self.root / "libexec" / "nbdfuse"
        shutil.copy2(
            ROOT / "packaging" / "linux" / "nbdfuse",
            bridge_launcher,
        )
        bridge_launcher.chmod(0o755)
        bridge = self.root / "libexec" / "nbdfuse.bin"
        bridge.write_text(
            "#!/bin/sh\n"
            "printf 'bridge-libraries=%s\\n' \"${LD_LIBRARY_PATH-unset}\"\n",
            encoding="utf-8",
        )
        bridge.chmod(0o755)
        xemu_root = self.root / "runtime" / "xemu"
        xemu_root.mkdir(parents=True)
        xemu_launcher = xemu_root / "xemu"
        shutil.copy2(
            ROOT / "packaging" / "linux" / "xemu",
            xemu_launcher,
        )
        xemu_launcher.chmod(0o755)
        app_run = xemu_root / "AppDir" / "AppRun"
        app_run.parent.mkdir()
        app_run.write_text(
            "#!/bin/sh\n"
            "printf 'xemu-libraries=%s\\n' \"${LD_LIBRARY_PATH-unset}\"\n",
            encoding="utf-8",
        )
        app_run.chmod(0o755)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def launch(
        self,
        values: dict[str, str],
        launcher: Path | None = None,
    ) -> list[str]:
        environment = os.environ.copy()
        for name in SESSION_VARIABLES:
            environment.pop(name, None)
        environment.update(values)
        completed = subprocess.run(
            [str(launcher or self.root / "gdox")],
            check=True,
            capture_output=True,
            env=environment,
            text=True,
        )
        return completed.stdout.splitlines()

    def test_launcher_resolves_install_root_through_symlink(self) -> None:
        bin_directory = self.root / "bin"
        bin_directory.mkdir()
        launcher = bin_directory / "gdox"
        launcher.symlink_to(self.root / "gdox")
        output = self.launch({}, launcher)
        self.assertEqual(output[0], "gaming=unset")
        self.assertEqual(output[1], "host=desktop")
        self.assertEqual(output[2], f"libraries={self.root}/lib")

    def assert_mode(self, values: dict[str, str], expected: str) -> None:
        output = self.launch(values)
        self.assertEqual(output[0], f"gaming={expected}")
        self.assertEqual(output[1], "host=desktop")
        self.assertEqual(
            output[2],
            f"libraries={self.root}/lib",
        )

    def test_packaged_host_profile_cannot_be_overridden(self) -> None:
        output = self.launch({"GDOX_HOST_PROFILE": "handheld"})
        self.assertEqual(output[1], "host=desktop")

    def test_steamdeck_package_profile_is_handheld(self) -> None:
        launcher = self.root / "gdox"
        _set_linux_host_profile(launcher, "handheld")
        output = self.launch({})
        self.assertEqual(output[1], "host=handheld")

    def test_actual_gaming_mode_indicators(self) -> None:
        self.assert_mode({"SteamGamepadUI": "1"}, "1")
        self.assert_mode({"XDG_CURRENT_DESKTOP": "gamescope"}, "1")
        self.assert_mode(
            {"XDG_CURRENT_DESKTOP": "KDE:gamescope"}, "1"
        )
        self.assert_mode({"DESKTOP_SESSION": "gamescope-wayland"}, "1")

    def test_desktop_mode_remains_unset(self) -> None:
        self.assert_mode(
            {
                "XDG_CURRENT_DESKTOP": "KDE",
                "XDG_SESSION_DESKTOP": "plasma",
                "DESKTOP_SESSION": "plasma",
            },
            "unset",
        )

    def test_explicit_mode_overrides_detection(self) -> None:
        self.assert_mode(
            {"GDOX_GAMING_MODE": "0", "SteamGamepadUI": "1"}, "0"
        )
        self.assert_mode({"GDOX_GAMING_MODE": "1"}, "1")

    def test_bridge_launcher_scopes_private_library(self) -> None:
        environment = os.environ.copy()
        environment.pop("LD_LIBRARY_PATH", None)
        completed = subprocess.run(
            [str(self.root / "libexec" / "nbdfuse"), "--version"],
            check=True,
            capture_output=True,
            env=environment,
            text=True,
        )
        self.assertEqual(
            completed.stdout.strip(),
            f"bridge-libraries={self.root}/runtime/bridge/lib",
        )

    def test_xemu_launcher_scopes_appimage_libraries(self) -> None:
        environment = os.environ.copy()
        environment.pop("LD_LIBRARY_PATH", None)
        completed = subprocess.run(
            [str(self.root / "runtime" / "xemu" / "xemu")],
            check=True,
            capture_output=True,
            env=environment,
            text=True,
        )
        self.assertEqual(
            completed.stdout.strip(),
            "xemu-libraries="
            f"{self.root}/runtime/xemu/AppDir/usr/lib",
        )


if __name__ == "__main__":
    unittest.main()
