
from __future__ import annotations

import hashlib
import os
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "packaging" / "linux" / "install.sh"


def write_executable(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    path.chmod(0o755)


def tree_manifest(root: Path) -> tuple[tuple[object, ...], ...]:
    if not root.exists():
        return ()
    entries: list[tuple[object, ...]] = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        metadata = path.lstat()
        mode = stat.S_IMODE(metadata.st_mode)
        if path.is_symlink():
            entries.append((relative, "link", mode, os.readlink(path)))
        elif path.is_dir():
            entries.append((relative, "directory", mode))
        elif path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            entries.append((relative, "file", mode, digest))
        else:
            entries.append((relative, "other", mode))
    return tuple(entries)


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"required installer test tool is unavailable: {name}")
    return path


def prepare_tool_path(root: Path) -> Path:
    tools = root / "tools"
    tools.mkdir()
    for name in (
        "chmod",
        "cp",
        "dirname",
        "find",
        "grep",
        "ln",
        "mkdir",
        "rm",
        "sed",
        "sh",
    ):
        (tools / name).symlink_to(require_tool(name))

    real_mv = require_tool("mv")
    write_executable(
        tools / "mv",
        f"""#!/bin/sh
set -eu

fault=${{GDOX_INSTALLER_TEST_FAULT:-}}
source_path=
destination_path=
for argument do
    if [ "${{argument}}" = "--" ]; then
        continue
    fi
    if [ -z "${{source_path}}" ]; then
        source_path=${{argument}}
    else
        destination_path=${{argument}}
    fi
done

boundary=
case "${{destination_path}}" in
    *.previous.*) boundary=backup ;;
esac
case "${{source_path}}:${{destination_path}}" in
    *.installing.*:*/gdox) boundary=activation ;;
    */legacy-game.xiso:*/preservations/*) boundary=migration ;;
esac

if [ "${{fault}}" = "${{boundary}}_before" ]; then
    exit 71
fi
if [ "${{fault}}" = "${{boundary}}_after" ]; then
    "{real_mv}" "$@"
    exit 72
fi
exec "{real_mv}" "$@"
""",
    )
    write_executable(tools / "fusermount3", "#!/bin/sh\nexit 0\n")
    return tools


class InstallerFixture:
    def __init__(self, root: Path, with_previous: bool = True) -> None:
        self.root = root
        self.home = root / "home"
        self.data_home = self.home / ".local" / "share"
        self.install_root = self.home / ".local" / "opt" / "gdox"
        self.private_data = self.data_home / "gdox"
        self.source = root / "release"
        self.tools = prepare_tool_path(root)

        self.source.mkdir()
        shutil.copy2(INSTALLER, self.source / "install.sh")
        write_executable(
            self.source / "gdox", "#!/bin/sh\necho new-gdox\n"
        )
        write_executable(
            self.source / "libexec" / "nbdfuse",
            "#!/bin/sh\nexit 0\n",
        )
        write_executable(
            self.source / "libexec" / "nbdfuse.bin",
            "#!/bin/sh\nexit 0\n",
        )
        packaging = self.source / "packaging"
        packaging.mkdir()
        (packaging / "gdox.png").write_bytes(b"new-application-icon")
        (packaging / "org.gdox.gdox.desktop").write_text(
            "[Desktop Entry]\nName=GDOX\nExec=gdox\nType=Application\n",
            encoding="utf-8",
        )
        write_executable(
            self.source / "setup-device-access.sh",
            "#!/bin/sh\nexit 0\n",
        )

        self.private_data.mkdir(parents=True)
        (self.private_data / "settings.conf").write_bytes(
            b"user-setting=preserve-exactly\n"
        )
        saves = self.private_data / "xemu" / "saves"
        saves.mkdir(parents=True)
        (saves / "slot.bin").write_bytes(bytes(range(64)))

        legacy_icon = (
            self.data_home / "icons" / "hicolor" / "scalable" / "apps"
            / "gdox.svg"
        )
        legacy_icon.parent.mkdir(parents=True)
        legacy_icon.write_bytes(b"legacy-application-icon")

        if with_previous:
            self.install_root.mkdir(parents=True)
            write_executable(
                self.install_root / "gdox", "#!/bin/sh\necho old-gdox\n"
            )
            runtime = self.install_root / "runtime" / "old"
            runtime.mkdir(parents=True)
            (runtime / "payload.bin").write_bytes(b"old-runtime\x00payload")
            (self.install_root / "current-runtime").symlink_to(
                "runtime/old/payload.bin"
            )
            (self.install_root / "legacy-game.xiso").write_bytes(
                b"legacy-preservation"
            )
            (self.install_root / "second-game.iso").write_bytes(
                b"second-preservation"
            )

    def run(self, fault: str | None = None) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update(
            {
                "HOME": str(self.home),
                "XDG_DATA_HOME": str(self.data_home),
                "GDOX_SKIP_DEVICE_SETUP": "1",
                "PATH": str(self.tools),
            }
        )
        if fault is not None:
            environment["GDOX_INSTALLER_TEST_FAULT"] = fault
        else:
            environment.pop("GDOX_INSTALLER_TEST_FAULT", None)
        return subprocess.run(
            [str(self.source / "install.sh")],
            env=environment,
            text=True,
            capture_output=True,
            timeout=20,
            check=False,
        )

    def stale_transactions(self) -> list[Path]:
        opt_home = self.install_root.parent
        return sorted(opt_home.glob("gdox.installing.*")) + sorted(
            opt_home.glob("gdox.previous.*")
        )


def test_failed_update_rolls_back(fault: str) -> None:
    with tempfile.TemporaryDirectory(prefix="gdox-installer-") as directory:
        fixture = InstallerFixture(Path(directory))
        install_before = tree_manifest(fixture.install_root)
        private_before = tree_manifest(fixture.private_data)

        result = fixture.run(fault)

        assert result.returncode != 0, (fault, result.stdout, result.stderr)
        assert tree_manifest(fixture.install_root) == install_before, fault
        assert tree_manifest(fixture.private_data) == private_before, fault
        assert fixture.stale_transactions() == [], fault


def test_failed_fresh_activation_is_removed() -> None:
    with tempfile.TemporaryDirectory(prefix="gdox-installer-") as directory:
        fixture = InstallerFixture(Path(directory), with_previous=False)
        private_before = tree_manifest(fixture.private_data)

        result = fixture.run("activation_after")

        assert result.returncode != 0, (result.stdout, result.stderr)
        assert not fixture.install_root.exists()
        assert tree_manifest(fixture.private_data) == private_before
        assert fixture.stale_transactions() == []


def test_failed_migration_retains_images(fault: str) -> None:
    with tempfile.TemporaryDirectory(prefix="gdox-installer-") as directory:
        fixture = InstallerFixture(Path(directory))
        settings_before = (fixture.private_data / "settings.conf").read_bytes()
        save_before = (
            fixture.private_data / "xemu" / "saves" / "slot.bin"
        ).read_bytes()

        result = fixture.run(fault)

        assert result.returncode != 0, (fault, result.stdout, result.stderr)
        assert (fixture.install_root / "gdox").read_bytes().endswith(
            b"echo new-gdox\n"
        )
        previous = sorted(
            fixture.install_root.parent.glob("gdox.previous.*")
        )
        installing = sorted(
            fixture.install_root.parent.glob("gdox.installing.*")
        )
        assert len(previous) == 1, fault
        assert installing == [], fault
        assert str(previous[0]) in result.stderr, fault
        expected_images = {
            "legacy-game.xiso": b"legacy-preservation",
            "second-game.iso": b"second-preservation",
        }
        for name, expected in expected_images.items():
            surviving_images = [
                path
                for path in (
                    previous[0] / name,
                    fixture.private_data / "preservations" / name,
                )
                if path.is_file()
            ]
            assert len(surviving_images) == 1, (fault, name)
            assert surviving_images[0].read_bytes() == expected, (fault, name)
        assert (
            fixture.private_data / "settings.conf"
        ).read_bytes() == settings_before
        assert (
            fixture.private_data / "xemu" / "saves" / "slot.bin"
        ).read_bytes() == save_before


def test_successful_update_commits() -> None:
    with tempfile.TemporaryDirectory(prefix="gdox-installer-") as directory:
        fixture = InstallerFixture(Path(directory))
        settings_before = (fixture.private_data / "settings.conf").read_bytes()
        save_before = (
            fixture.private_data / "xemu" / "saves" / "slot.bin"
        ).read_bytes()

        result = fixture.run()

        assert result.returncode == 0, (result.stdout, result.stderr)
        assert (fixture.install_root / "gdox").read_bytes().endswith(
            b"echo new-gdox\n"
        )
        assert not (fixture.install_root / "legacy-game.xiso").exists()
        assert (
            fixture.private_data / "preservations" / "legacy-game.xiso"
        ).read_bytes() == b"legacy-preservation"
        assert (
            fixture.private_data / "preservations" / "second-game.iso"
        ).read_bytes() == b"second-preservation"
        assert (
            fixture.private_data / "settings.conf"
        ).read_bytes() == settings_before
        assert (
            fixture.private_data / "xemu" / "saves" / "slot.bin"
        ).read_bytes() == save_before
        assert fixture.stale_transactions() == []
        assert "nbdfuse" not in result.stderr
        assert (
            fixture.data_home
            / "icons"
            / "hicolor"
            / "512x512"
            / "apps"
            / "gdox.png"
        ).read_bytes() == b"new-application-icon"
        assert not (
            fixture.data_home
            / "icons"
            / "hicolor"
            / "scalable"
            / "apps"
            / "gdox.svg"
        ).exists()


def test_symlinked_private_data_is_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="gdox-installer-") as directory:
        fixture = InstallerFixture(Path(directory))
        external = fixture.root / "external-private-data"
        install_before = tree_manifest(fixture.install_root)

        shutil.rmtree(fixture.private_data)
        (external / "docs").mkdir(parents=True)
        (external / "docs" / "keep.txt").write_bytes(b"unrelated-docs")
        (external / "runtime").mkdir()
        (external / "runtime" / "keep.bin").write_bytes(b"unrelated-runtime")
        external_before = tree_manifest(external)
        fixture.private_data.symlink_to(external, target_is_directory=True)

        result = fixture.run()

        assert result.returncode != 0, (result.stdout, result.stderr)
        assert "refused a symbolic link" in result.stderr
        assert tree_manifest(fixture.install_root) == install_before
        assert tree_manifest(external) == external_before


def main() -> None:
    for fault in (
        "backup_before",
        "backup_after",
        "activation_before",
        "activation_after",
    ):
        test_failed_update_rolls_back(fault)
    test_failed_fresh_activation_is_removed()
    test_failed_migration_retains_images("migration_before")
    test_failed_migration_retains_images("migration_after")
    test_successful_update_commits()
    test_symlinked_private_data_is_rejected()


if __name__ == "__main__":
    main()
