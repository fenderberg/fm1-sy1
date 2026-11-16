#!/usr/bin/env python3
"""Build the complete SY1 UX and export real simulator screens for all 32 subpages in all three display styles."""
import argparse
import html
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PAGES = (("play", "HOME", "Play", ("Filter", "Performance")),
         ("osc", "EDIT", "Oscillators", ("Waveforms", "Pitch", "FM / Ring", "Sub", "Mod envelope", "Mod timing")),
         ("filter", "SEL", "Filter", ("Main", "Tracking")),
         ("env", "ENV", "Envelopes", ("Amp", "Filter", "Gain / Velocity")),
         ("lfo", "LFO", "LFO", ("LFO 1", "LFO 1 routing", "LFO 2", "LFO 2 routing")),
         ("fx", "FX", "Effects", ("Insert", "Mix", "EQ", "Delay", "Delay mix", "Chorus", "Chorus modulation")),
         ("arp", "ARP", "Arpeggiator", ("Direction / Range", "Gate / Hold")),
         ("voice", "GLO", "Voice", ("Mode", "Unison", "Spread", "Setup", "Diagnostics", "Display")))


def checked(args, log=None):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, text=True, capture_output=True)
    if log:
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    print(result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "OK")
    return len(re.findall(r"^  ok", result.stdout, re.M))


def gallery(destination, sections):
    nav = "".join(f'<a href="#{slug}">{html.escape(title)}</a>' for slug, title, _ in sections)
    body = []
    for slug, title, images in sections:
        figures = "".join(f'<figure><a href="{file}"><img src="{file}" width="240" height="240" alt="{html.escape(label)}" loading="lazy"></a>'
                          f'<figcaption>{html.escape(label)}</figcaption></figure>' for file, label in images)
        body.append(f'<section id="{slug}"><h2>{html.escape(title)}</h2><div class="screens">{figures}</div></section>')
    document = '''<!doctype html><html lang="nl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SY1 · Volledig Nord-redesign</title><style>
:root{color-scheme:dark;background:#151718;color:#f3efdf;font:15px/1.6 system-ui,sans-serif}*{box-sizing:border-box}
body{margin:0;border-top:4px solid #d83d31}main{max-width:1200px;margin:auto;padding:36px 24px 60px}
h1{font-size:clamp(28px,5vw,44px);letter-spacing:-.04em;line-height:1.1}h2{font-size:24px;margin-top:45px}
p{color:#b8b8af;max-width:75ch}a{color:#ffc764;text-underline-offset:4px}.eyebrow{font:11px ui-monospace,monospace;color:#ffc764;letter-spacing:.1em}
nav,.toolbar{display:flex;flex-wrap:wrap;gap:10px;margin:24px 0}nav a,button{border:1px solid #555b57;border-radius:4px;padding:9px 14px;text-decoration:none;background:#252829;font:inherit;color:#f3efdf;min-height:44px}
button{cursor:pointer}button[aria-pressed=true]{color:#ffc764;border-color:#ffc764}:focus-visible{outline:3px solid #ffc764;outline-offset:4px}
.screens{display:grid;grid-template-columns:repeat(auto-fit,minmax(270px,1fr));gap:20px}figure{margin:0;padding:18px;background:#202324;border:1px solid #3c403e;border-radius:4px;overflow:auto}
img{width:240px;height:240px;display:block;margin:auto;image-rendering:pixelated}figcaption{margin-top:14px;color:#b8b8af;font-size:12px}
body.zoom .screens{grid-template-columns:repeat(auto-fit,minmax(min(100%,520px),1fr))}body.zoom img{width:480px;height:480px}
section{scroll-margin-top:20px}@media(max-width:400px){main{padding-inline:16px}figure{padding:14px}}
</style></head><body><main><p class="eyebrow">SY1 / NORD UX / ALLE PAGINA’S</p><h1>Eén instrument. Eén bediening.</h1>
<p>De volledige firmware-UX op 240 × 240 pixels: vier vaste bedieningsvakken, opgeslagen referenties,
encoderfocus en passende visualisaties. Kies op GLO → DISPLAY met KNOB1 tussen NORD, GREEN LED en GREEN LCD.
Dit zijn echte beelden uit de normale hostsimulator, in alle drie de schermstijlen.</p>
<div class="toolbar"><button id="native" aria-pressed="true">1× · 240 pixels</button><button id="zoom" aria-pressed="false">2× · pixels bekijken</button></div>
<p>De grafieken tonen instellingen. De punten in de envelope, LFO en arp komen uit de engine.
Decay en release tonen hun tijdconstante (TAU); de ruwe Synth1-waarde is te bekijken op GLO → Diagnostics.</p>'''
    document += '<nav aria-label="Schermgroepen">' + nav + '</nav>' + "".join(body)
    document += '''<p><a href="README.md">Validatie en opnieuw bouwen</a> · <a href="../nord-pixel/index.html">De eerdere pixelproef A</a></p></main>
<script>for(const id of ['native','zoom'])document.getElementById(id).addEventListener('click',()=>{
document.body.classList.toggle('zoom',id==='zoom');for(const choice of ['native','zoom'])document.getElementById(choice).setAttribute('aria-pressed',String(choice===id));});</script></body></html>'''
    (destination / "index.html").write_text(document, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/mockups/nord-redesign")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    work = ROOT / "app/build/ux-review"
    work.mkdir(parents=True, exist_ok=True)
    if not args.skip_build:
        checked([ROOT / "build.sh", "--host"], work / "host-build.log")
    host = ROOT / "app/build/host/sy1_host"
    checks = 0
    sections = []
    for case in ("basic", "performance", "nord-preview", "ux", "display", "factory"):
        out = work / case
        out.mkdir(exist_ok=True)
        checks += checked([host, ROOT / f"app/tests/{case}.txt", out], work / f"{case}.log")
    script = ["wait 600", "turn PRESET 1", "wait 1400"]
    for style, prefix in (("Nord", ""), ("Green LED", "green-"), ("Green LCD", "lcd-")):
        if prefix:
            script += ["tap REC", "tap GLO", "turn SELECT -20", "turn SELECT 5", "turn K1 1", "expect display "+("2" if prefix=="lcd-" else "1"), "tap HOME"]
        for page, (slug, button, title, rows) in enumerate(PAGES):
            if button == "ARP":
                script += ["press ARP", "wait 550", "release ARP", "tap ARP", "tap SEQ",
                           "key 7 down", "key 11 down", "key 14 down", "wait 120",
                           "key 7 up", "key 11 up", "key 14 up"]
            elif button != "HOME":
                script += [f"tap {button}"]
            script += ["turn SELECT -20", f"expect page {page}"]
            if slug == "env": script += ["key 7 down", "wait 120"]
            images = []
            for row, label in enumerate(rows):
                if row: script += ["turn SELECT 1"]
                file = f"{prefix}{slug}-{row+1:02d}.png"
                script += [f"expect row {row}", "wait 80", f"shot {file}"]
                images.append((file, f"{title} · {label}"))
            if slug == "env": script += ["key 7 up", "tap PLAY", "wait 100"]
            if slug == "arp": script += ["tap SEQ", "tap PLAY", "wait 100"]
            sections.append((prefix+slug, style+" · "+title, images))
    all_pages = work / "all-pages.txt"
    all_pages.write_text("\n".join(script) + "\n", encoding="utf-8")
    out = work / "all-pages"
    out.mkdir(exist_ok=True)
    checks += checked([host, all_pages, out], work / "all-pages.log")
    args.output.mkdir(parents=True, exist_ok=True)
    from PIL import Image
    exports = []
    for group in ("all-pages", "nord-preview", "ux", "display", "factory"):
        items = []
        for source in sorted((work / group).glob("*.png")):
            with Image.open(source) as screenshot:
                if screenshot.size != (240, 240): raise SystemExit(f"Wrong screen size: {source}")
            name = ("" if group == "all-pages" else group + "-") + source.name
            shutil.copy2(source, args.output / name)
            exports.append(name)
            items.append((name, source.stem.replace("-", " ")))
        if group != "all-pages": sections.append((group, "Bediening en speelstatus" if group == "nord-preview" else "500 fabriekspresets" if group == "factory" else "Displaykeuze en herstart" if group == "display" else "Referenties en live feedback", items))
    gallery(args.output, sections)
    # Keep the manual, README and homepage tied to the actual current renderer.
    if args.output.resolve() == (ROOT / "docs/mockups/nord-redesign").resolve():
        for prefix, name in (("", ""), ("green-", "green-"), ("lcd-", "lcd-")):
            for source, target in (("play-01.png", "play.png"), ("filter-01.png", "filter.png")):
                for directory in (ROOT / "app/docs/img", ROOT / "docs/img"):
                    directory.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(args.output / (prefix + source), directory / ("screen-" + name + target))
    if args.output.resolve() == (ROOT / "docs/mockups/nord-redesign").resolve():
        for source, target in (("factory-factory-first.png", "screen-factory-nord.png"),
                               ("factory-factory-led.png", "screen-factory-green.png"),
                               ("factory-factory-lcd.png", "screen-factory-lcd.png")):
            for directory in (ROOT / "app/docs/img", ROOT / "docs/img"):
                shutil.copy2(args.output / source, directory / target)
    report = {"subpages": sum(len(page[3]) for page in PAGES), "display_styles": 3, "simulator_checks": checks,
              "screenshots": len(exports), "baseline_audio_identical": None}
    baseline = ROOT / "app/build/ux-baseline/sy1_host"
    if baseline.exists():
        old_out = work / "baseline-basic"
        old_out.mkdir(exist_ok=True)
        checked([baseline, ROOT / "app/tests/basic.txt", old_out], work / "baseline.log")
        same = (old_out / "bass.wav").read_bytes() == (work / "basic/bass.wav").read_bytes()
        report["baseline_audio_identical"] = same
        if not same: raise SystemExit("Baseline audio changed")
    (args.output / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"UX review: {report['subpages']} subpages, {checks} simulator checks, {len(exports)} screenshots: {args.output}")


if __name__ == "__main__":
    main()
