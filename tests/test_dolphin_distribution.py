"""Adversarial checks for the bundled Dolphin archive boundary."""
from __future__ import annotations
import copy
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import dolphin_distribution as distribution
from fetch_runtime import load_manifest


class DolphinDistributionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="gdox-dolphin-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = load_manifest()
        self.files = {"dolphin": b"launcher", "bin/dolphin-emu": b"runtime", "bin/nbdfuse": b"bridge"}
        self.pins = {name: distribution.identity(data) for name, data in self.files.items()}

    def archive(self, entries):
        path = self.root / "runtime.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            for name, data, kind in entries:
                member = tarfile.TarInfo(name)
                member.mode = 0o755
                member.type = kind
                if kind == tarfile.SYMTYPE:
                    member.linkname = "../../outside"
                else:
                    member.size = len(data)
                archive.addfile(member, io.BytesIO(data) if member.isfile() else None)
        return path

    def stage(self, entries):
        archive = self.archive(entries)
        destination = self.root / "stage/runtime"
        destination.mkdir(parents=True)
        with patch.object(distribution, "inventory", return_value=self.pins):
            distribution.bundle(distribution.LINUX_TARGET, destination, self.root,
                                self.manifest, lambda asset, cache: archive)
        return destination

    def entries(self):
        return [("dolphin/" + name, data, tarfile.REGTYPE) for name, data in self.files.items()]

    def test_verified_archive_and_tampered_stage(self):
        destination = self.stage([
            ("dolphin/", b"", tarfile.DIRTYPE),
            ("dolphin/bin/", b"", tarfile.DIRTYPE),
            *self.entries(),
        ])
        with patch.object(distribution, "inventory", return_value=self.pins):
            distribution.validate_stage(distribution.LINUX_TARGET, destination.parent, self.manifest)
            (destination / "dolphin/bin/dolphin-emu").write_bytes(b"changed")
            with self.assertRaisesRegex(SystemExit, "differs"):
                distribution.validate_stage(distribution.LINUX_TARGET, destination.parent, self.manifest)

    def test_root_must_be_a_directory(self):
        with self.assertRaisesRegex(SystemExit, "unexpected root"):
            self.stage([("dolphin", b"bad", tarfile.REGTYPE), *self.entries()])

    def test_missing_member(self):
        with self.assertRaisesRegex(SystemExit, "incomplete"):
            self.stage(self.entries()[:-1])

    def test_duplicate_member(self):
        with self.assertRaisesRegex(SystemExit, "unexpected file"):
            self.stage([*self.entries(), self.entries()[0]])

    def test_symlink_member(self):
        entries = self.entries()
        entries[0] = (entries[0][0], b"", tarfile.SYMTYPE)
        with self.assertRaisesRegex(SystemExit, "unexpected file"):
            self.stage(entries)

    def test_traversal_member(self):
        with self.assertRaisesRegex(SystemExit, "unexpected file"):
            self.stage([("dolphin/../../outside", b"bad", tarfile.REGTYPE), *self.entries()])
        self.assertFalse((self.root / "outside").exists())

    def test_changed_archive_bytes(self):
        entries = self.entries()
        entries[0] = (entries[0][0], b"tampered", tarfile.REGTYPE)
        with self.assertRaisesRegex(SystemExit, "verification"):
            self.stage(entries)

    def test_source_identity_and_inventory_are_independent_gates(self):
        manifest = copy.deepcopy(self.manifest)
        manifest["dolphin"]["upstream_commit"] = "0" * 40
        with self.assertRaisesRegex(SystemExit, "source identity"):
            distribution.validate_runtime_manifest(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest["dolphin"]["inventory"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(SystemExit, "inventory differs"):
            distribution.validate_runtime_manifest(manifest)

    def test_candidate_cannot_be_published(self):
        manifest = copy.deepcopy(self.manifest)
        manifest["dolphin"]["release_state"] = "candidate-only"
        with self.assertRaisesRegex(SystemExit, "validation candidate"):
            distribution.require_publishable(distribution.LINUX_TARGET, manifest)


if __name__ == "__main__":
    unittest.main()
