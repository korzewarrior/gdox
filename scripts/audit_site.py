#!/usr/bin/env python3

import argparse
import hashlib
import json
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SITE = ROOT.parent / "gdox-output" / "site" / "public"


class Document(HTMLParser):
    def __init__(self):
        super().__init__()
        self.canonical = []
        self.description = []
        self.doctype = False
        self.h1 = 0
        self.ids = []
        self.lang = []
        self.scripts = 0
        self.styles = []
        self.urls = []

    def handle_decl(self, declaration):
        self.doctype = declaration.lower() == "doctype html"

    def handle_starttag(self, tag, attributes):
        values = dict(attributes)
        if tag == "html":
            self.lang.append(values.get("lang"))
        if tag == "h1":
            self.h1 += 1
        if tag == "script":
            self.scripts += 1
        if "id" in values:
            self.ids.append(values["id"])
        if tag == "meta" and values.get("name") == "description":
            self.description.append(values.get("content"))
        if tag == "link" and values.get("rel") == "canonical":
            self.canonical.append(values.get("href"))
        if tag == "link" and values.get("rel") == "stylesheet":
            self.styles.append(values.get("href"))
        for name in ("href", "src"):
            if name in values:
                self.urls.append(values[name])


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--site", type=Path, default=DEFAULT_SITE)
    return parser.parse_args()


def page_target(site, url):
    parsed = urlsplit(url)
    if parsed.scheme or parsed.netloc or not parsed.path.startswith("/"):
        return None, None
    relative = parsed.path.lstrip("/")
    if not relative or parsed.path.endswith("/"):
        relative += "index.html"
    return site / relative, unquote(parsed.fragment)


def main():
    site = parse_args().site.resolve()
    errors = []
    documents = {}
    canonical = set()
    style_version = hashlib.sha256((site / "assets" / "style.css").read_bytes()).hexdigest()[:12]

    for html in sorted(site.rglob("*.html")):
        relative = html.relative_to(site)
        source = html.read_text(encoding="utf-8")
        document = Document()
        document.feed(source)
        documents[html] = document

        if "{{" in source or "}}" in source:
            errors.append(f"{relative}: unresolved template token")
        if not document.doctype:
            errors.append(f"{relative}: missing HTML doctype")
        if document.lang != ["en"]:
            errors.append(f"{relative}: expected one lang=en")
        if document.h1 != 1:
            errors.append(f"{relative}: expected one h1")
        if document.scripts:
            errors.append(f"{relative}: runtime scripts are not expected")
        if len(document.ids) != len(set(document.ids)):
            errors.append(f"{relative}: duplicate id")
        if document.styles != [f"/assets/style.css?v={style_version}"]:
            errors.append(f"{relative}: stylesheet version mismatch")

        is_404 = relative == Path("404.html")
        if is_404:
            if document.canonical or document.description:
                errors.append("404.html: canonical and description must be omitted")
        else:
            if len(document.canonical) != 1 or len(document.description) != 1:
                errors.append(f"{relative}: expected one canonical and description")
            else:
                canonical.add(document.canonical[0])

    for html, document in documents.items():
        for url in document.urls:
            target, fragment = page_target(site, url)
            if target is None or urlsplit(url).path.startswith("/downloads/"):
                continue
            if not target.exists():
                errors.append(f"{html.relative_to(site)}: missing {url}")
                continue
            if fragment and target.suffix == ".html":
                target_document = documents.get(target)
                if not target_document or fragment not in target_document.ids:
                    errors.append(f"{html.relative_to(site)}: missing fragment {url}")

    namespace = {"s": "http://www.sitemaps.org/schemas/sitemap/0.9"}
    sitemap = ElementTree.parse(site / "sitemap.xml")
    sitemap_urls = {node.text for node in sitemap.findall("s:url/s:loc", namespace)}
    if sitemap_urls != canonical:
        errors.append("sitemap URLs do not match canonical pages")

    for schema in (site / "schemas").glob("*.json"):
        json.loads(schema.read_text(encoding="utf-8"))

    if (site / "minisign.pub").read_bytes() != (ROOT / "packaging" / "minisign.pub").read_bytes():
        errors.append("minisign public key differs from packaging source")
    if list(site.rglob("*.js")):
        errors.append("unexpected JavaScript in static output")

    if errors:
        raise SystemExit("\n".join(errors))
    print(f"site audit passed ({len(documents)} pages, {len(canonical)} canonical URLs)")


if __name__ == "__main__":
    main()
