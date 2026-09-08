from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gdox-optical-inventory-", dir=args.scratch_root) as temporary:
        root = Path(temporary).resolve()
        blocks, usb, dev = root / "blocks", root / "usb", root / "dev"
        for directory in (blocks, usb, dev):
            directory.mkdir()

        def write(path: Path, value: str) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value + "\n")

        def optical(port: int, model: str, revision: str) -> Path:
            physical = root / "physical" / f"1-{port}"
            for key, value in {
                "busnum": "1", "devnum": str(port + 1), "devpath": str(port),
                "idVendor": "0e8d", "idProduct": "1887", "serial": f"drive-{port}",
            }.items():
                write(physical / key, value)
            scsi = physical / "scsi"
            for key, value in {"vendor": "HL-DT-ST", "model": model, "rev": revision}.items():
                write(scsi / key, value)
            (usb / f"1-{port}").symlink_to(physical)
            (blocks / f"sr{port - 1}").mkdir()
            (blocks / f"sr{port - 1}" / "device").symlink_to(scsi)
            write(dev / f"sr{port - 1}", "device placeholder")
            write(dev / "bus/usb/001" / f"{port + 1:03}", "USB node")
            return physical

        first = optical(1, "DVDRAM GP63EX70", "RF02")
        optical(2, "DVDRAM GP63EX70", "RF02")
        optical(3, "DVDRAM GP57EB40", "PB00")

        def run(capacity: int = 8, media: bool = False):
            result = subprocess.run(
                [str(args.probe), str(blocks), str(usb), str(dev), str(capacity), str(int(media))],
                capture_output=True, text=True, timeout=10, check=False,
            )
            lines = [line.split("\t") for line in result.stdout.splitlines()]
            assert lines and lines[0][0] == "status", result
            return result, lines[0], {line[3]: line for line in lines[1:]}

        result, status, devices = run()
        assert result.returncode == 0 and status[2:4] == ["3", "0"], (result, status)
        a, b, unsupported = devices["/dev/sr0"], devices["/dev/sr1"], devices["/dev/sr2"]
        assert a[1] == b[1] == "0" and a[2] != b[2], devices
        assert unsupported[1] != "0" and unsupported[1] != "6", unsupported
        assert all(row[4:] == ["1", "1", "0"] for row in devices.values()), devices
        original_id = a[2]
        # Device numbers may change while topology and serial remain stable.
        (blocks / "sr0").rename(blocks / "sr7")
        result, _, devices = run()
        assert devices["/dev/sr7"][2] == original_id, devices
        # A replacement unit at the same port must receive a different ID.
        write(first / "serial", "replacement")
        result, _, devices = run()
        assert devices["/dev/sr7"][2] != original_id, devices
        result, status, _ = run(media=True)
        assert result.returncode == 0 and int(status[3]) == 3, (result, status)
        result, status, _ = run(capacity=1)
        assert result.returncode != 0 and status[2] == "1" and "capacity" in result.stderr, result
        result, status, _ = run(capacity=0)
        assert result.returncode != 0 and status[2] == "0", result
    print("Linux optical inventory preserves duplicate devices and passive discovery")


if __name__ == "__main__":
    main()
