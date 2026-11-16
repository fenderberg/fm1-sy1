# SY1

A polysynth for the [M-VAVE FM-1](https://github.com/m-vave/fm1_open) that plays **Synth1 patches** (`.sy1`).

**[Website](https://fenderberg.github.io/fm1-sy1/)** · [Try it in the browser](https://fenderberg.github.io/fm1-sy1/emu/) · [Install](https://fenderberg.github.io/fm1-sy1/install/) · [Patch Manager](https://fenderberg.github.io/fm1-sy1/manager/) · [Manual](https://fenderberg.github.io/fm1-sy1/manual/)

Two oscillators and a sub, FM / ring / sync, four filter types, three envelopes, two LFOs, arpeggiator,
unison, ten effects, EQ, tempo delay and chorus: Synth1's 99 parameters, under Synth1's own numbers, so its
patches load as they are. Every parameter can also be edited on the FM-1 itself.

**Find a sound first:** the [Patch Manager](https://fenderberg.github.io/fm1-sy1/manager/) includes **3,174 presets in 27 community banks**. Search names, filter banks and sound categories, save favourites, preview in the browser, then load a selection over USB. It also imports your own `.sy1` files, folders, bank ZIPs and ZIP collections containing bank ZIPs, entirely locally.

<img src="docs/img/screen-play.png" width="240" alt="SY1's Play page: the patch Warm Pad with filter and gain values"> <img src="docs/img/screen-filter.png" width="240" alt="SY1's Filter page: type, frequency, resonance and amount">

SY1 is built on [fm1-base](https://github.com/charlesvestal/fm1-base). Its parameter laws (envelope times,
filter curves, FM index and the rest) are the measurements of the original Synth1 published by
[oxo_synth2](https://github.com/chab/oxo_synth2) (MIT). See [THIRD_PARTY.md](THIRD_PARTY.md).

SY1 is not affiliated with or endorsed by Daichi Laboratory. Its engine contains no Synth1 code. The separate community preset library retains original banks, readmes and author terms; see [preset credits](app/site/library/CREDITS.md).

Latest release: [v0.2.0](https://github.com/fenderberg/fm1-sy1/releases/tag/v0.2.0) · [What changed](CHANGELOG.md)

## Status: early

| Part | State |
| --- | --- |
| Engine (`app/dsp/`) | Works; tested on the host: `make test` |
| `.sy1` reader, old versions too (ver < 109) | Works; tested in C and in JavaScript against each other |
| App: screen, panel, keys, MIDI, patch storage | Passes in fm1-base's simulator (`app/tests/basic.txt`) and in `make test-app` |
| Browser emulator with a patch loader | Works: drop `.sy1` files or a `.zip` bank on the page |
| Firmware (`./build.sh`) | Builds; installs and runs on one FM-1 (driven over USB with `base/tools/fm1_remote.py`) |
| Voices on the FM-1 | About 7 on one core: a plain voice costs about 10 % of the render's time, a detuned one with chorus about 12 %. The second core is not used yet |
| Sound on the FM-1, and compared with the original Synth1 | **Not judged yet** |
| Patch library and hardware manager | 3,174 hosted patches; local nested-ZIP import; search, favourites, browser preview and acknowledged USB transfer |

## Try it in the browser

```sh
git clone --recursive https://github.com/fenderberg/fm1-sy1 && cd fm1-sy1
./build.sh --web                                  # needs Emscripten (emcc) and Python with Pillow
python3 -m http.server 8471 --bind 127.0.0.1 --directory app/build/emu
```

Open <http://localhost:8471>, switch the FM-1 on, and drop your Synth1 patches on the page. Turn PRESETS
past the eight built-in patches to play them. The page loads the emulated FM-1 only; for a real one, see
below.

## Build and test

```sh
make test          # the engine: maths, tuning, envelopes, the patch reader, 400 random patches
make test-app      # the app against a stand-in for the FM-1
make test-web      # reader against C, nested ZIPs, library integrity and placement (Node >= 22.16)
make test-site     # website navigation, local links and scripts (needs existing release and browser builds)
make preview-nord  # the full Nord UX: simulator checks and screenshots of all 31 subpages
make all           # build/s1render (a patch to a WAV file) and build/sy1pack (patches to a SysEx file)

./build.sh --host  # fm1-base's simulator, then:
app/build/host/sy1_host app/tests/basic.txt app/build/host
./build.sh         # the firmware: app/build/sy1.fwsc (JieLi toolchain and AC79 SDK: see base/README.md)
```

Build the complete website with `make site VERSION=0.2.0` after building the matching release package and
browser emulator. It includes the Patch Manager and shared navigation on every page. Preview it with
`python3 -m http.server 8471 --bind 127.0.0.1 --directory app/build/site`.
The app-owned builder is `tools/make_site.py`; the shared theme and homepage are in `app/site/`.
`tools/publish_site.sh VERSION [--push]` builds the firmware, emulator and complete site, and optionally publishes it
with a normal update to `gh-pages` (no history rewrite).

Optional browser integration QA, with Playwright already installed and the complete site served locally:

```sh
PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs SY1_TEST_COLLECTION=/path/to/zipbank.zip node host/manager_browser_test.mjs
```

It uses a simulated MIDI device and never writes to physical hardware. `SY1_TEST_URL` defaults to
`http://127.0.0.1:8473`; omit `SY1_TEST_COLLECTION` to skip the external full-collection import check.

`build/s1render PATCH.sy1 out.wav -n 48,60,64,67 -hold 2 -tail 2` renders a patch with the engine exactly as
the FM-1 runs it.

## Patches on the FM-1

The [Patch Manager](https://fenderberg.github.io/fm1-sy1/manager/) works without hardware for browsing,
local import and browser previews. Choose a bank or narrow the search, select individual sounds or all
filtered sounds, then connect an FM-1 running SY1 in Chrome or Edge (allow MIDI with SysEx). The default
destination adds after the last imported patch. Manual placement shows and confirms occupied-slot
replacement. Selections must fit completely; unsent patches remain selected after Stop or a failure.
The FM-1 holds 1,024 imported patches at a time, in addition to its eight built-in patches.

The supplied `zipbank.zip` matches the file in the [Internet Archive Synth1 collection](https://archive.org/details/synth1_202202),
whose metadata credits the compilation to **techno-addicts**. Our inventory found 175 banks / 16,698 patch
entries (16,376 byte-unique), independently of the archive title’s count.
Import the full collection locally and browse it a bank at a time. Only the 27 banks with explicit
redistribution grants in their original notes are publicly hosted here; others stay local. Imports last
for the current tab; favourite IDs are saved in this browser. No imported files are uploaded.
Original Synth1 presets may sound different in this lighter engine; see the manual's limitations.

For the command-line sender, with the FM-1 running SY1 connected over USB:

```sh
make all                                     # once: builds build/sy1pack
tools/sy1_send.py folder/ bank.zip one.sy1   # .sy1 files, folders of them, .zip banks
tools/sy1_send.py --count                    # how many slots are in use
tools/sy1_send.py --erase                    # erase every loaded patch (the built-in ones stay)
```

The patches follow the ones already there (`--first N` chooses the slot). Each is acknowledged by the FM-1
before the next is sent. It needs Python with `mido` and `python-rtmidi`. The FM-1 holds 1024 patches; turn
PRESETS past `P08` to reach them.

`build/sy1pack bank.syx folder/*.sy1` writes the same frames to a SysEx file instead, for any SysEx tool
(leave 200 ms or more between messages: each is a write to the flash). The protocol is at the top of
[app/bank.h](app/bank.h).

## Controls

| Control | What it does |
| --- | --- |
| PRESETS / ALGORITHM | Patch within category / category. Empty categories are skipped; both lists wrap |
| HOME, EDIT, SEL, ENV, LFO, FX, GLO | Pages: Play, Osc, Filter, Env, LFO, Effects, Voice. Again: the next row |
| ARP | Tap: arp on/off. Hold 0.5 s: Arp settings; hold again: next row |
| SEQ | Arp on: chord HOLD. Arp off: DRONE. Tap again to release |
| SELECT | Choose a row |
| KNOB1–4 | The chosen row's four values |
| SAVE | Save the patch (a built-in one goes to the first free slot) |
| REC | Undo the edits and release hold/drone |
| PLAY | All notes off |
| OCT− / OCT+ | Octave |

MIDI in over USB and TRS: notes, pitch bend, the wheel and other controllers (as the patch routes them),
sustain pedal, aftertouch, program change (the patch) and MIDI clock (the tempo of the arpeggiator, the delay
and tempo-synced LFOs).

The firmware, host simulator and browser emulator share the Nord-style screen: a fixed patch header, red
subpage strip, sound diagram, four encoder columns and a permanent playing-status footer. Amber shows the
active control; grey notches show saved values. Pages remember their last subpage during the session.
Envelope, LFO and arp feedback uses a compact engine snapshot. See the
[complete screen gallery](docs/mockups/nord-redesign/index.html) and [manual](app/docs/MANUAL.md).

HOLD replaces the latched chord when you play again after releasing every key. Drone has STACK (add notes;
play one again to remove it) and REPLACE (new gesture replaces the chord), selected on Arp row 2 or Voice row 4.
Both work with the panel and MIDI. PLAY and MIDI Stop clear all held notes. A patch change also clears hold.

Categories: ALL, BASS, PAD, LEAD, KEYS, PLUCK, ARP, FX, OTHER and USER (every stored patch).
Imported names suggest a category; Voice row 4 lets you correct it and SAVE keeps it with the patch.
The header shows the browser category; ARP/SEQ lights and the footer show ARP, HLD or DRN.

See [performance ideas](docs/PERFORMANCE.md) for the next improvements, without adding a sequencer.

## How it differs from Synth1

The engine is a lighter reimplementation of the same structure, not a bit-exact copy. Simplified for the
FM-1 on purpose:

- Oscillators use polyBLEP instead of band-limited tables per note (no memory for the tables).
- Envelopes, LFOs, pitch and cutoff move at 1/16 of the sample rate, interpolated.
- The LPDL filter (type 4) plays as the 24 dB ladder; effect a.d.1 uses a.d.2's curve.
- Delay: 0.74 s a side at most (memory); longer times play an octave shorter. 16-bit storage.
- Delay and chorus together: the echoes also reach the output directly, not only through the chorus.
- Detune: osc1 gets its eight copies, the sub does not.
- Pitch bend and LFO pitch are not quantised to 1/128-octave steps.
- 8 voices by default (VOICE > VOICES, 1–16); on an overload one is dropped.

## Layout

```
app/dsp/              the engine: s1_synth (voices), s1_fx (effects), s1_patch (.sy1), s1_tables (generated)
app/app.c, bank.h     the FM-1 app; patch storage and the built-in patches
app/web/emu/app.js    the emulator page's patch loader
host/                 WAV renderer, patch packer, tests
tools/gen_tables.py   makes s1_tables.* and the loader's tables from oxo_synth2's measured tables
tools/sy1_send.py     sends patches to an FM-1 over USB MIDI
tools/publish_site.sh builds the site (app/site, app/docs) and pushes it to gh-pages
app/site/             the site's front page and the Patch Manager
base/                 fm1-base (git submodule)
```

## Licence

MIT: see [LICENSE](LICENSE). Third-party material keeps its own licence: [THIRD_PARTY.md](THIRD_PARTY.md).
