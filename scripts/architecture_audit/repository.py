"""Small source and CMake parsers used by architecture checks."""

from __future__ import annotations

import ast
import re
from dataclasses import dataclass
from pathlib import Path

SOURCE_SUFFIXES = frozenset({".c", ".cpp", ".h", ".hpp"})
_INCLUDE = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]', re.MULTILINE)
_KOTLIN_IMPORT = re.compile(r"^\s*import\s+([A-Za-z_][\w.]*)", re.MULTILINE)
_CALL = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
_CMAKE_VARIABLE = re.compile(r"^\$\{([A-Za-z_][A-Za-z0-9_]*)\}$")
_CMAKE_SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".m", ".mm")
_CMAKE_SCOPES = frozenset({"PRIVATE", "PUBLIC", "INTERFACE"})
_CMAKE_LIBRARY_KINDS = frozenset(
    {"STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "UNKNOWN"}
)


@dataclass(frozen=True)
class CMakeCommand:
    name: str
    arguments: tuple[str, ...]
    body: str


def _split_cmake_arguments(body: str) -> tuple[str, ...]:
    arguments: list[str] = []
    token: list[str] = []
    quoted = False
    escaped = False
    index = 0

    def finish() -> None:
        if not token:
            return
        value = "".join(token)
        token.clear()
        arguments.extend(part for part in value.split(";") if part)

    while index < len(body):
        character = body[index]
        if escaped:
            token.append(character)
            escaped = False
        elif character == "\\" and quoted:
            escaped = True
        elif character == '"':
            quoted = not quoted
        elif character == "#" and not quoted:
            finish()
            newline = body.find("\n", index)
            if newline < 0:
                break
            index = newline
        elif character.isspace() and not quoted:
            finish()
        else:
            token.append(character)
        index += 1
    finish()
    return tuple(arguments)


def parse_cmake_commands(text: str) -> tuple[CMakeCommand, ...]:
    commands: list[CMakeCommand] = []
    index = 0
    quoted = False
    escaped = False

    while index < len(text):
        character = text[index]
        if escaped:
            escaped = False
            index += 1
            continue
        if character == "\\" and quoted:
            escaped = True
            index += 1
            continue
        if character == '"':
            quoted = not quoted
            index += 1
            continue
        if character == "#" and not quoted:
            newline = text.find("\n", index)
            index = len(text) if newline < 0 else newline + 1
            continue
        if not quoted and (character.isalpha() or character == "_"):
            end = index + 1
            while end < len(text) and (
                text[end].isalnum() or text[end] == "_"
            ):
                end += 1
            open_parenthesis = end
            while open_parenthesis < len(text) and text[open_parenthesis].isspace():
                open_parenthesis += 1
            if open_parenthesis < len(text) and text[open_parenthesis] == "(":
                depth = 1
                cursor = open_parenthesis + 1
                inner_quoted = False
                inner_escaped = False
                while cursor < len(text) and depth > 0:
                    inner = text[cursor]
                    if inner_escaped:
                        inner_escaped = False
                    elif inner == "\\" and inner_quoted:
                        inner_escaped = True
                    elif inner == '"':
                        inner_quoted = not inner_quoted
                    elif inner == "#" and not inner_quoted:
                        newline = text.find("\n", cursor)
                        cursor = len(text) if newline < 0 else newline
                    elif not inner_quoted and inner == "(":
                        depth += 1
                    elif not inner_quoted and inner == ")":
                        depth -= 1
                    cursor += 1
                if depth != 0:
                    raise ValueError(f"unterminated CMake command {text[index:end]}")
                body = text[open_parenthesis + 1 : cursor - 1]
                commands.append(
                    CMakeCommand(
                        text[index:end].lower(),
                        _split_cmake_arguments(body),
                        body,
                    )
                )
                index = cursor
                continue
        index += 1
    return tuple(commands)


@dataclass(frozen=True)
class CMakeDocument:
    path: Path
    commands: tuple[CMakeCommand, ...]

    @classmethod
    def read(cls, path: Path) -> CMakeDocument:
        return cls(path, parse_cmake_commands(path.read_text(encoding="utf-8")))

    def named(self, name: str) -> tuple[CMakeCommand, ...]:
        normalized = name.lower()
        return tuple(command for command in self.commands if command.name == normalized)


class CMakeProject:
    def __init__(self, root: Path):
        paths = [root / "CMakeLists.txt", *sorted((root / "cmake").glob("*.cmake"))]
        self.root = root
        self.documents = tuple(CMakeDocument.read(path) for path in paths)
        variables: dict[str, list[str]] = {}
        for document in self.documents:
            for command in document.named("set"):
                if command.arguments:
                    variables.setdefault(command.arguments[0], []).extend(
                        command.arguments[1:]
                    )
        self.variables = {name: tuple(values) for name, values in variables.items()}

    def document(self, relative: str) -> CMakeDocument:
        requested = (self.root / relative).resolve()
        for document in self.documents:
            if document.path.resolve() == requested:
                return document
        return CMakeDocument.read(requested)

    def expand(self, values: tuple[str, ...]) -> tuple[str, ...]:
        expanded: list[str] = []
        pending = list(values)
        expansions = 0
        while pending:
            value = pending.pop(0)
            match = _CMAKE_VARIABLE.fullmatch(value)
            if match is None or match.group(1) not in self.variables:
                expanded.append(value)
                continue
            pending[:0] = self.variables[match.group(1)]
            expansions += 1
            if expansions > 10000:
                raise ValueError("recursive CMake variable expansion")
        return tuple(expanded)

    def variable(self, name: str) -> tuple[str, ...]:
        return self.expand(self.variables.get(name, ()))

    def includes(self, relative: str) -> tuple[str, ...]:
        document = self.document(relative)
        return tuple(
            command.arguments[0]
            for command in document.named("include")
            if command.arguments
        )

    def aliases(self) -> dict[str, str]:
        aliases: dict[str, str] = {}
        for document in self.documents:
            for command in document.named("add_library"):
                arguments = command.arguments
                if len(arguments) >= 3 and arguments[1] == "ALIAS":
                    aliases[arguments[0]] = arguments[2]
        return aliases

    def has_target(self, target: str) -> bool:
        for document in self.documents:
            for command in document.commands:
                if command.name not in {"add_library", "add_executable"}:
                    continue
                if command.arguments and command.arguments[0] == target:
                    return True
        return False

    def has_target_command(self, command_name: str, target: str) -> bool:
        for document in self.documents:
            for command in document.named(command_name):
                if command.arguments and command.arguments[0] == target:
                    return True
        return False

    def target_sources(self, target: str) -> tuple[str, ...]:
        sources: list[str] = []
        for document in self.documents:
            for command in document.commands:
                arguments = command.arguments
                if not arguments or arguments[0] != target:
                    continue
                values: tuple[str, ...]
                if command.name == "target_sources":
                    values = arguments[1:]
                elif command.name in {"add_library", "add_executable"}:
                    if "ALIAS" in arguments:
                        continue
                    values = arguments[1:]
                    if values and values[0] in _CMAKE_LIBRARY_KINDS:
                        values = values[1:]
                else:
                    continue
                for value in self.expand(values):
                    if value in _CMAKE_SCOPES:
                        continue
                    if value.endswith(_CMAKE_SOURCE_SUFFIXES) or value.startswith("$<"):
                        sources.append(value)
        return tuple(sources)

    def target_links(self, target: str) -> tuple[str, ...]:
        links: list[str] = []
        for document in self.documents:
            for command in document.named("target_link_libraries"):
                if not command.arguments or command.arguments[0] != target:
                    continue
                links.extend(
                    value
                    for value in self.expand(command.arguments[1:])
                    if value not in _CMAKE_SCOPES
                )
        return tuple(links)

    def target_compile_definitions(self, target: str) -> tuple[str, ...]:
        definitions: list[str] = []
        for document in self.documents:
            for command in document.named("target_compile_definitions"):
                if not command.arguments or command.arguments[0] != target:
                    continue
                definitions.extend(
                    value
                    for value in self.expand(command.arguments[1:])
                    if value not in _CMAKE_SCOPES
                )
        return tuple(definitions)

    def all_source_paths(self) -> tuple[str, ...]:
        paths: list[str] = []
        for document in self.documents:
            for command in document.commands:
                for value in self.expand(command.arguments):
                    if value.endswith(_CMAKE_SOURCE_SUFFIXES):
                        paths.append(value)
        return tuple(paths)


@dataclass(frozen=True)
class SourceDocument:
    path: Path
    text: str

    @classmethod
    def read(cls, path: Path) -> SourceDocument:
        return cls(path, path.read_text(encoding="utf-8"))

    @property
    def includes(self) -> tuple[str, ...]:
        return tuple(_INCLUDE.findall(self.text))

    @property
    def imports(self) -> tuple[str, ...]:
        return tuple(_KOTLIN_IMPORT.findall(self.text))

    @property
    def calls(self) -> tuple[str, ...]:
        return tuple(_CALL.findall(self.text))

    @property
    def line_count(self) -> int:
        return len(self.text.splitlines())

    def has_identifier(self, identifier: str) -> bool:
        return re.search(rf"\b{re.escape(identifier)}\b", self.text) is not None

    def defines_c_function(self, name: str) -> bool:
        return re.search(
            rf"(?m)^[^#\n;{{}}]*\b{re.escape(name)}\s*"
            r"\([^;{}]*\)\s*\{",
            self.text,
        ) is not None

    def declares_c_function(self, name: str) -> bool:
        return re.search(
            rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*;",
            self.text,
            re.MULTILINE,
        ) is not None

    def call_positions(self, names: tuple[str, ...]) -> tuple[int, ...]:
        return tuple(
            self.text.find(f"{name}(")
            for name in names
        )


class PythonDocument:
    def __init__(self, path: Path):
        self.path = path
        self.tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    def calls(self, name: str) -> bool:
        for node in ast.walk(self.tree):
            if not isinstance(node, ast.Call):
                continue
            function = node.func
            if isinstance(function, ast.Name) and function.id == name:
                return True
            if isinstance(function, ast.Attribute) and function.attr == name:
                return True
        return False


class Repository:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.cmake = CMakeProject(self.root)
        self._sources: dict[str, SourceDocument] = {}

    def source(self, relative: str) -> SourceDocument:
        if relative not in self._sources:
            self._sources[relative] = SourceDocument.read(self.root / relative)
        return self._sources[relative]

    def python(self, relative: str) -> PythonDocument:
        return PythonDocument(self.root / relative)

    def source_files(self, directory: str) -> tuple[SourceDocument, ...]:
        return tuple(
            SourceDocument.read(path)
            for path in sorted((self.root / directory).rglob("*"))
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )

    def relative(self, path: Path) -> str:
        return path.relative_to(self.root).as_posix()
