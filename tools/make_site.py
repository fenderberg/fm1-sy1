#!/usr/bin/env python3
"""Build SY1's complete site, including its manager and shared navigation.

    python3 tools/make_site.py PACKAGE VERSION [OUT_DIR]

fm1-base owns the firmware installer, manual conversion and emulator packaging.
This app-owned layer adds SY1's design without modifying the base submodule.
"""
import html
import csv
import io
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "app" / "site"
PAGES = (("", "Overview"), ("emu", "Play"), ("manager", "Patch Manager"),
         ("install", "Install"), ("manual", "Manual"), ("news", "What's new"))
SOURCE = "https://github.com/fenderberg/fm1-sy1"


def bundle_library(directory, catalog):
    index = io.StringIO(newline="")
    rows = csv.writer(index)
    rows.writerow(("Bank", "Author", "Patch", "Category", "Bank ZIP", "Patch file", "Patch ID"))
    with ZipFile(directory / "sy1-community-library.zip", "w") as bundle:
        for bank in catalog["banks"]:
            filename = f'banks/{bank["name"]}.zip'
            bundle.writestr(ZipInfo(filename), (directory / bank["file"]).read_bytes())
            for patch in bank["patches"]:
                rows.writerow((bank["name"], bank["author"], patch["name"], patch["category"],
                               filename, patch["path"], patch["id"]))
        bundle.writestr(ZipInfo("presets.csv"), index.getvalue().encode("utf-8-sig"), ZIP_DEFLATED)
        bundle.writestr(ZipInfo("CREDITS.md"), (directory / "CREDITS.md").read_bytes(), ZIP_DEFLATED)


def navigation(current, up):
    links = []
    for route, label in PAGES:
        href = up + (route + "/" if route else "") or "./"
        active = ' aria-current="page"' if route == current.split("/")[0] else ""
        if route == "manager" and not active:    # the manager is an app: it gets its own tab
            active = ' target="_blank" rel="noopener"'
        links.append(f'<a href="{href}"{active}><span class="nav-led" aria-hidden="true"></span>{label}</a>')
    links.append(f'<a href="{SOURCE}" class="source-link">Source <span aria-hidden="true">↗</span></a>')
    return ('<a class="site-skip" href="#content">Skip to content</a>\n'
            '<header class="site-header"><div class="site-header-inner">'
            f'<a class="site-brand" href="{up or "./"}" aria-label="SY1 home">SY<span>1</span>'
            '<span class="brand-caption">VIRTUAL ANALOG<br>FOR THE FM-1</span></a>'
            '<nav class="site-nav" aria-label="Main navigation">' + "".join(links) + '</nav></div></header>')


def decorate(document, current, version):
    up = "../" * len(Path(current).parts) if current else ""
    # Remove only the old global navigation; keep the manual's contents navigation.
    document = re.sub(r'<header class="site">.*?</header>', "", document, flags=re.S)
    document = re.sub(r'^[ \t]*<nav aria-label="SY1">.*?</nav>[ \t]*\n?', "", document, flags=re.S | re.M)
    document = re.sub(r'<a class="skip" href="#content">.*?</a>', "", document)
    if current == "emu":
        document = re.sub(r'<header>.*?</header>',
                          '<header class="page-heading"><p class="eyebrow">01 / PLAY</p>'
                          '<h1>Meet your next sound.</h1><p>The real SY1 engine, right in your browser. '
                          'Switch on, play a few notes, and turn a knob. Choose your screen style on '
                          'GLO → DISPLAY with KNOB1: NORD, GREEN (LED), or LCD. Further down, the bitimbral demo '
                          'plays two sounds at once.</p></header>', document, count=1, flags=re.S)
    elif current == "install":
        document = document.replace('<main>', '<main><p class="factory-note">Includes 256 community presets in program flash, a cross-section of all 13 sound categories, plus 1,024 separate user slots. '
                                    '<a href="../library/factory500-credits.txt">Included preset credits and terms</a>.</p>', 1)
    document = document.replace('</head>', f'<link rel="stylesheet" href="{up}assets/site.css">\n</head>', 1)
    document = document.replace('<body>', f'<body data-site-page="{current or "home"}">\n' + navigation(current, up), 1)
    # A single, predictable skip-link destination on every page.
    if not re.search(r'<main\b[^>]*\bid="content"', document):
        document = document.replace('<main>', '<main id="content" tabindex="-1">', 1)
    footer = ('<footer class="site-footer"><div><a class="footer-wordmark" href="' + (up or './') + '">SY1</a>'
              '<span>A little hardware. A lot of possibility.</span></div>'
              f'<p>v{html.escape(version)} · Code: MIT · <a href="{up}library/CREDITS.md">Preset credits</a></p></footer>')
    return document.replace('</body>', footer + '\n</body>', 1)


def whats_new(out):
    """news/index.html: CHANGELOG.md, every version with what changed, newest first."""
    changes = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    body = re.sub(r"\A# [^\n]*\n", "", changes).lstrip()
    body = re.sub(r"^## (\d)", r"## Version \1", body, flags=re.M)   # "## 1.0.0" would read as section 1
    md = ("# What's new\n\nWhat changed in each version of SY1, newest first. Installing a new version keeps your "
          "user slots, patches and settings. Older firmware packages are on the "
          f"[releases page]({SOURCE}/releases).\n\n" + body)
    page = out / "news"
    page.mkdir(parents=True, exist_ok=True)
    source = page / "CHANGELOG.md"
    source.write_text(md, encoding="utf-8")
    subprocess.run([sys.executable, str(ROOT / "base" / "tools" / "manual_html.py"), str(source),
                    str(page / "index.html"), "--name", "SY1", "--repo", SOURCE], check=True, capture_output=True)
    source.unlink()


def main(pkg, version, destination=None):
    out = Path(destination).resolve() if destination else ROOT / "app" / "build" / "site"
    subprocess.run([sys.executable, str(ROOT / "base" / "tools" / "make_site.py"),
                    str(ROOT / "app"), str(Path(pkg).resolve()), version, str(out)], check=True)
    shutil.copytree(SITE / "assets", out / "assets", dirs_exist_ok=True)
    shutil.copytree(SITE / "manager", out / "manager", dirs_exist_ok=True)
    shutil.copytree(SITE / "library", out / "library", dirs_exist_ok=True)
    gallery = ROOT / "docs/mockups/nord-redesign"
    screens = out / "manual/screens"
    screens.mkdir(parents=True, exist_ok=True)
    for source in gallery.glob("*.png"):
        shutil.copy2(source, screens / source.name)
    shutil.copy2(gallery / "validation.json", screens / "validation.json")
    document = (gallery / "index.html").read_text(encoding="utf-8")
    document = document.replace('href="README.md"', f'href="{SOURCE}/blob/main/docs/mockups/nord-redesign/README.md"')
    document = document.replace('href="../nord-pixel/index.html"', 'href="../"')
    document = document.replace("De eerdere pixelproef A", "Back to the manual")
    (screens / "index.html").write_text(document, encoding="utf-8")
    whats_new(out)
    catalog = json.loads((SITE / "library/catalog.json").read_text(encoding="utf-8"))
    bundle_library(out / "library", catalog)
    landing = (SITE / "index.html").read_text(encoding="utf-8")
    landing = landing.replace("{{PRESET_COUNT}}", f'{catalog["patchCount"]:,}').replace("{{BANK_COUNT}}", str(catalog["bankCount"]))
    landing = landing.replace("{{VERSION}}", html.escape(version)).replace(
        "{{PKG}}", html.escape(f"sy1-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"))
    (out / "index.html").write_text(landing, encoding="utf-8")
    for page in sorted(out.rglob("*.html")):
        current = page.parent.relative_to(out).as_posix()
        current = "" if current == "." else current
        page.write_text(decorate(page.read_text(encoding="utf-8"), current, version), encoding="utf-8")
    print(f"SY1 site: {out} (shared navigation, Patch Manager, hardware theme)")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
