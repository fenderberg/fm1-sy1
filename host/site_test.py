#!/usr/bin/env python3
"""Check the built site: complete navigation, local assets and executable page scripts."""
from html.parser import HTMLParser
import json
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SITE = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "app/build/site"
MAIN_ROUTES = ("", "emu", "manager", "install", "manual")
ROUTES = (*MAIN_ROUTES, "manual/screens")
LABELS = ["Overview", "Play", "Patch Manager", "Install", "Manual", "Source ↗"]


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.links, self.nav, self.scripts = [], [], []
        self.in_nav = self.in_script = False
        self.current_link = None
        self.main = 0
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "nav" and attrs.get("class") == "site-nav":
            self.in_nav = True
        if tag == "main" and attrs.get("id") == "content":
            self.main += 1
        for key in ("href", "src"):
            if key in attrs and "baklink" not in attrs.get("class", "").split():
                self.links.append(attrs[key])
        if tag == "a" and self.in_nav:
            self.current_link = {"label": "", **attrs}
            self.nav.append(self.current_link)
        if tag == "script" and "src" not in attrs:
            self.in_script = True
            self.scripts.append("")

    def handle_endtag(self, tag):
        if tag == "nav":
            self.in_nav = False
        if tag == "a":
            self.current_link = None
        if tag == "script":
            self.in_script = False

    def handle_data(self, data):
        if self.current_link is not None:
            self.current_link["label"] += data
        if self.in_script:
            self.scripts[-1] += data


checks = 0
for route in ROUTES:
    file = SITE / route / "index.html"
    assert file.is_file(), f"Missing page: {file}"
    page = Page(file.read_text(encoding="utf-8"))
    assert [link["label"].strip() for link in page.nav] == LABELS, f"Navigation differs: {route}"
    active = [link for link in page.nav if link.get("aria-current") == "page"]
    assert len(active) == 1 and active[0]["label"] == LABELS[MAIN_ROUTES.index(route.split('/')[0])], f"Active page: {route}"
    assert page.main == 1, f"Missing or duplicate skip-link destination: {route}"
    checks += 3
    for target in page.links:
        url = urlsplit(target)
        if url.scheme or url.netloc or not url.path:
            continue
        local = (file.parent / unquote(url.path)).resolve()
        assert local.is_relative_to(SITE), f"Link escapes website: {file}: {target}"
        assert local.exists(), f"Broken local link: {file}: {target}"
        checks += 1
    if shutil.which("node"):
        for script in page.scripts:
            subprocess.run(["node", "--input-type=module", "--check"], input=script, text=True, check=True,
                           capture_output=True)
            checks += 1
# The manager imports this module; omitting it makes the whole interface inert.
assert "export function parseSy1" in (SITE / "emu/app_own.js").read_text(encoding="utf-8")
assert (SITE / "assets/site.css").stat().st_size > 0
catalog = json.loads((SITE / "library/catalog.json").read_text(encoding="utf-8"))
assert catalog["patchCount"] == sum(bank["count"] for bank in catalog["banks"]) == 3174
for bank in catalog["banks"]:
    assert (SITE / "library" / bank["file"]).is_file(), f"Missing downloadable bank: {bank['name']}"
assert (SITE / "manager/library.js").is_file()
assert (SITE / "manager/app.js").is_file()
report = json.loads((SITE / "manual/screens/validation.json").read_text(encoding="utf-8"))
assert report["subpages"] == 33 and report["display_styles"] == 3 and report["screenshots"] == 145
assert len(list((SITE / "manual/screens").glob("*.png"))) == report["screenshots"]
factory = json.loads((SITE / "library/factory500.json").read_text(encoding="utf-8"))
assert factory["count"] == len(factory["patches"]) == 256
assert (SITE / "library/factory500.zip").is_file() and (SITE / "library/factory500-credits.txt").is_file()
print(f"site: {checks + 34} checks passed (6 pages, shared navigation, local links, 27 preset banks, 145 screen captures)")
