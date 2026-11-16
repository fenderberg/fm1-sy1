#!/usr/bin/env python3
"""Check the hub card and the actual release package before publishing."""
import json
from pathlib import Path
import re
import sys
import tomllib

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base/tools"))
from fm1_install import load_package, LOADER_MARK

card = json.loads((ROOT / "fmhub.json").read_text())
assert not set(card) - {"what", "author", "kinds", "play", "editor", "cheat", "image", "sounds", "color"}
assert 20 <= len(card["what"]) <= 300
assert not re.search(r"[<>{}`]|https?://|\bwww\.", card["what"])
assert 2 <= len(card["author"]) <= 60
assert card["kinds"] == ["synth"]
assert all(card[k].startswith("https://") for k in ("play", "editor", "cheat"))
assert re.fullmatch(r"#[0-9A-Fa-f]{6}", card["color"])
assert card["sounds"].startswith("licensed: ") and len(card["sounds"]) <= 300
for credit in ("app/site/library/CREDITS.md", "app/presets/CREDITS.txt"):
    assert credit in card["sounds"] and (ROOT / credit).is_file()
picture = (ROOT / card["image"]).resolve()
assert picture.is_relative_to(ROOT) and picture.stat().st_size <= 300_000
with Image.open(picture) as image:
    assert image.format in ("PNG", "WEBP", "JPEG")
    assert image.width == image.height and 240 <= image.width <= 1024
    image.verify()

package = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "app/build/sy1-0.3.1.fwsc"
version = re.fullmatch(r"sy1-(\d{1,2})\.(\d{1,2})\.(\d{1,2})\.fwsc", package.name)
assert version, "Pass the versioned release package"
config = tomllib.loads((ROOT / "app/fm1.toml").read_text())
assert config["id_prefix"] == "FM-1_51", "SY1 owns prefix 51; 47 belongs to FRASIL"
expected = config["id_prefix"] + "".join(f"{int(v):02d}" for v in version.groups())
identity, image, digest = load_package(package, force=False)
assert identity == expected, (identity, expected)
assert LOADER_MARK in image
assert (ROOT / "app/presets/CREDITS.txt").read_bytes() in (ROOT / "app/build/sy1.bin").read_bytes()
print(f"hub: card, screenshot, preset credits, {identity}, package loader and SHA256 {digest.hex()} checked")
