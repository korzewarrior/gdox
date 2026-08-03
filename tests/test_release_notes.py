"""Tests for reviewed release-note extraction."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))

from project_version import project_version
from release_notes import release_notes


class ReleaseNotesTest(unittest.TestCase):
    def test_extracts_exact_release_section(self) -> None:
        changelog = """# changes

## 0.2.0

- first
- second

## 0.1.0

- old
"""
        self.assertEqual(
            release_notes(changelog, "0.2.0", require_released=True),
            "- first\n- second\n",
        )

    def test_rejects_unreleased_section_for_publication(self) -> None:
        with self.assertRaisesRegex(ValueError, "still marks 0.2.0 as unreleased"):
            release_notes(
                "# changes\n\n## 0.2.0 (unreleased)\n\n- pending\n",
                "0.2.0",
                require_released=True,
            )

    def test_rejects_missing_duplicate_and_empty_sections(self) -> None:
        cases = (
            "# changes\n",
            "## 0.2.0\n\n- one\n\n## 0.2.0\n\n- two\n",
            "## 0.2.0\n\n## 0.1.0\n\n- old\n",
        )
        for changelog in cases:
            with self.subTest(changelog=changelog), self.assertRaises(ValueError):
                release_notes(changelog, "0.2.0", require_released=False)

    def test_current_changelog_is_ready_for_project_version(self) -> None:
        notes = release_notes(
            (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"),
            project_version(),
            require_released=True,
        )
        self.assertTrue(notes.startswith("- add reviewed"))


if __name__ == "__main__":
    unittest.main()
