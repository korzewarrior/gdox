"""Focused tests for the structured architecture audit."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from architecture_audit import audit_repository
from architecture_audit.checks_layers import _reject_includes, _reject_tokens
from architecture_audit.checks_media import _normalized_sources
from architecture_audit.repository import (
    CMakeProject,
    PythonDocument,
    Repository,
    SourceDocument,
    parse_cmake_commands,
)


class CMakeParserTests(unittest.TestCase):
    def test_parses_multiline_commands_comments_quotes_and_nesting(self) -> None:
        commands = parse_cmake_commands(
            """
            # add_library(ignored STATIC ignored.c)
            set(SOURCES a.c # ignored.c
                b.c)
            add_library(sample STATIC ${SOURCES})
            target_link_libraries(
                sample PRIVATE "-framework Core Foundation" $<IF:$<BOOL:X>,a,b>
            )
            """
        )
        self.assertEqual([command.name for command in commands], [
            "set",
            "add_library",
            "target_link_libraries",
        ])
        self.assertEqual(commands[0].arguments, ("SOURCES", "a.c", "b.c"))
        self.assertIn("-framework Core Foundation", commands[2].arguments)
        self.assertIn("$<IF:$<BOOL:X>,a,b>", commands[2].arguments)

    def test_project_resolves_target_ownership(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "cmake").mkdir()
            (root / "CMakeLists.txt").write_text(
                """
                set(PRIVATE_SOURCES src/private.c)
                add_library(real STATIC src/base.c)
                add_library(ns::real ALIAS real)
                target_sources(real PRIVATE ${PRIVATE_SOURCES})
                target_link_libraries(real PUBLIC dep::one PRIVATE dep::two)
                target_compile_definitions(real PRIVATE ENABLED=1)
                """,
                encoding="utf-8",
            )
            project = CMakeProject(root)
            self.assertEqual(project.aliases(), {"ns::real": "real"})
            self.assertTrue(project.has_target("real"))
            self.assertFalse(project.has_target("missing"))
            self.assertTrue(
                project.has_target_command("target_link_libraries", "real")
            )
            self.assertEqual(
                project.target_sources("real"),
                ("src/base.c", "src/private.c"),
            )
            self.assertEqual(
                project.target_links("real"),
                ("dep::one", "dep::two"),
            )
            self.assertEqual(
                project.target_compile_definitions("real"),
                ("ENABLED=1",),
            )


class SourceParserTests(unittest.TestCase):
    def test_extracts_source_relationships_and_symbols(self) -> None:
        source = SourceDocument(
            Path("sample.c"),
            """
            #include "core/source.h"
            bool declared(int value);
            bool defined(int value) { return helper(value); }
            """,
        )
        self.assertEqual(source.includes, ("core/source.h",))
        self.assertTrue(source.declares_c_function("declared"))
        self.assertTrue(source.defines_c_function("defined"))
        self.assertIn("helper", source.calls)
        positions = source.call_positions(("defined", "missing"))
        self.assertGreaterEqual(positions[0], 0)
        self.assertEqual(positions[1], -1)

    def test_python_call_detection_uses_the_ast(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "policy.py"
            path.write_text(
                "# validate_release_artifact()\nchecker.validate_release_artifact(path)\n",
                encoding="utf-8",
            )
            document = PythonDocument(path)
            self.assertTrue(document.calls("validate_release_artifact"))
            self.assertFalse(document.calls("missing_gate"))


class FocusedCheckTests(unittest.TestCase):
    def test_layer_checks_report_only_real_source_relationships(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "cmake").mkdir()
            (root / "CMakeLists.txt").write_text("", encoding="utf-8")
            (root / "src/core").mkdir(parents=True)
            (root / "src/core/sample.c").write_text(
                '#include "platform/private.h"\nconst char *host = "__ANDROID__";\n',
                encoding="utf-8",
            )
            repository = Repository(root)
            self.assertEqual(
                _reject_includes(repository, "src/core", ("platform/",)),
                [
                    (
                        "src/core/sample.c crosses its layer boundary through "
                        "platform/private.h"
                    )
                ],
            )
            self.assertEqual(
                _reject_tokens(repository, "src/core", ("__ANDROID__",)),
                [
                    (
                        "src/core/sample.c contains platform-specific token "
                        "__ANDROID__"
                    )
                ],
            )

    def test_source_set_normalization_preserves_owned_paths(self) -> None:
        self.assertEqual(
            _normalized_sources(
                (
                    "${GDOX_ROOT}/src/core/live.c",
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/disc.c",
                    "generated.c",
                )
            ),
            ("src/core/live.c", "src/core/disc.c", "generated.c"),
        )


class RepositoryAuditTests(unittest.TestCase):
    def test_repository_satisfies_every_architecture_check(self) -> None:
        self.assertEqual(audit_repository(ROOT), [])

    def test_cli_succeeds_without_writing_bytecode(self) -> None:
        environment = os.environ.copy()
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        result = subprocess.run(
            [sys.executable, "-B", "scripts/audit_architecture.py"],
            cwd=ROOT,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "Architecture boundaries are clean.\n")


if __name__ == "__main__":
    unittest.main()
