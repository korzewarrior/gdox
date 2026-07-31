#!/usr/bin/env python3

import argparse
import hashlib
import re
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "site" / "public"
INCLUDES = ROOT / "site" / "includes"
DEFAULT_OUTPUT = ROOT.parent / "gdox-output" / "site" / "public"
INCLUDE_LINE = re.compile(
    r"^(?P<indent>[ \t]*)\{\{INCLUDE:(?P<name>[a-z0-9][a-z0-9_./-]*\.html)\}\}[ \t]*$",
    re.MULTILINE,
)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def validate_output(output):
    resolved = output.resolve()
    protected = {Path("/"), Path.home().resolve(), ROOT.resolve(), SOURCE.resolve()}
    if resolved in protected:
        raise SystemExit(f"refusing unsafe output path: {resolved}")
    return resolved


def resolve_include(name):
    root = INCLUDES.resolve()
    include = (INCLUDES / name).resolve()
    try:
        include.relative_to(root)
    except ValueError as error:
        raise SystemExit(f"include escapes component directory: {name}") from error
    if not include.is_file():
        raise SystemExit(f"missing site component: {name}")
    return include


def expand_includes(source, document, stack=()):
    def replace(match):
        include = resolve_include(match.group("name"))
        if include in stack:
            chain = " -> ".join(path.relative_to(INCLUDES).as_posix() for path in (*stack, include))
            raise SystemExit(f"recursive site component in {document}: {chain}")
        content = include.read_text(encoding="utf-8").rstrip("\n")
        expanded = expand_includes(content, document, (*stack, include))
        indent = match.group("indent")
        return "\n".join(f"{indent}{line}" if line else "" for line in expanded.splitlines())

    rendered = INCLUDE_LINE.sub(replace, source)
    if "{{INCLUDE:" in rendered:
        raise SystemExit(f"invalid site component token in {document}")
    return rendered


def main():
    output = validate_output(parse_args().output)
    output.parent.mkdir(parents=True, exist_ok=True)
    version = hashlib.sha256((SOURCE / "assets" / "style.css").read_bytes()).hexdigest()[:12]

    with tempfile.TemporaryDirectory(prefix=".gdox-site-", dir=output.parent) as temporary:
        staging = Path(temporary) / "public"
        shutil.copytree(SOURCE, staging)
        shutil.copytree(ROOT / "docs" / "schemas", staging / "schemas")
        shutil.copy2(ROOT / "packaging" / "minisign.pub", staging / "minisign.pub")

        for html in staging.rglob("*.html"):
            source = html.read_text(encoding="utf-8")
            source = expand_includes(source, html.relative_to(staging))
            if source.count("{{STYLE_VERSION}}") != 1:
                raise SystemExit(f"{html.relative_to(staging)} must contain one style version token")
            html.write_text(source.replace("{{STYLE_VERSION}}", version), encoding="utf-8")

        if output.exists():
            shutil.rmtree(output)
        shutil.move(staging, output)

    print(f"built {output} (style {version})")


if __name__ == "__main__":
    main()
