#!/usr/bin/env python3

import argparse
import hashlib
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "site" / "public"
DEFAULT_OUTPUT = ROOT.parent / "gdox-output" / "site" / "public"


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
            if source.count("{{STYLE_VERSION}}") != 1:
                raise SystemExit(f"{html.relative_to(staging)} must contain one style version token")
            html.write_text(source.replace("{{STYLE_VERSION}}", version), encoding="utf-8")

        if output.exists():
            shutil.rmtree(output)
        shutil.move(staging, output)

    print(f"built {output} (style {version})")


if __name__ == "__main__":
    main()
