# SY1

A polysynth for the [M-VAVE FM-1](https://github.com/m-vave/fm1_open) that plays **Synth1 patches** (`.sy1`).

Two oscillators and a sub, FM / ring / sync, four filter types, three envelopes, two LFOs, arpeggiator,
unison, ten effects, EQ, tempo delay and chorus: Synth1's 99 parameters, under Synth1's own numbers, so its
patches load as they are. Every parameter can also be edited on the FM-1 itself.

<img src="docs/img/screen-play.png" width="240" alt="SY1's Play page: the patch Warm Pad with filter and gain values"> <img src="docs/img/screen-filter.png" width="240" alt="SY1's Filter page: type, frequency, resonance and amount">

SY1 is built on [fm1-base](https://github.com/charlesvestal/fm1-base). Its parameter laws (envelope times,
filter curves, FM index and the rest) are the measurements of the original Synth1 published by
[oxo_synth2](https://github.com/chab/oxo_synth2) (MIT). See [THIRD_PARTY.md](THIRD_PARTY.md).

SY1 is not affiliated with or endorsed by Daichi Laboratory. It contains nothing of Synth1 itself and ships
no third-party patches.

## Status: early, not yet run on an FM-1

| Part | State |
| --- | --- |
| Engine (`app/dsp/`) | Works; tested on the host: `make test` |
| `.sy1` reader, old versions too (ver < 109) | Works; tested in C and in JavaScript against each other |
| App: screen, panel, keys, MIDI, patch storage | Passes in fm1-base's simulator (`app/tests/basic.txt`) and in `make test-app` |
| Browser emulator with a patch loader | Works: drop `.sy1` files or a `.zip` bank on the page |
| Firmware for the FM-1 (`./build.sh`) | **Not built yet** (needs the JieLi toolchain) |
| On a real FM-1 | **Not tested.** The number of voices its CPU manages is an estimate |
| Sound compared with the original Synth1 | **Not done yet** |

## Try it in the browser

```sh
git clone --recursive https://github.com/fenderberg/fm1-sy1 && cd fm1-sy1
./build.sh --web                                  # needs Emscripten (emcc) and Python with Pillow
python3 -m http.server 8471 --bind 127.0.0.1 --directory app/build/emu
```

Open <http://localhost:8471>, switch the FM-1 on, and drop your Synth1 patches on the page. Turn PRESETS
past the eight built-in patches to play them. With a real FM-1 running SY1 chosen under "MIDI ports", the
same page loads the patches into it.

## Build and test

```sh
make test          # the engine: maths, tuning, envelopes, the patch reader, 400 random patches
make test-app      # the app against a stand-in for the FM-1
make test-web      # the page's patch reader against the C one (node)
make all           # build/s1render (a patch to a WAV file) and build/sy1pack (patches to a SysEx file)

./build.sh --host  # fm1-base's simulator, then:
app/build/host/sy1_host app/tests/basic.txt app/build/host
./build.sh         # the firmware: app/build/sy1.fwsc (JieLi toolchain and AC79 SDK: see base/README.md)
```

`build/s1render PATCH.sy1 out.wav -n 48,60,64,67 -hold 2 -tail 2` renders a patch with the engine exactly as
the FM-1 runs it.

## Patches on the FM-1

Use the emulator page (above), or make a SysEx file and send it with any SysEx tool, with a pause of 200 ms
or more between messages (each one is a write to the FM-1's flash):

```sh
build/sy1pack bank.syx -first 1 folder/*.sy1
```

The FM-1 holds 1024 patches. The protocol is at the top of [app/bank.h](app/bank.h).

## Controls

| Control | What it does |
| --- | --- |
| PRESETS / ALGORITHM | Patch ±1 / ±10. `P01`–`P08` are built in, `001`–`1024` are yours |
| HOME, EDIT, SEL, ENV, LFO, FX, ARP, GLO | Pages: Play, Osc, Filter, Env, LFO, Effects, Arp, Voice. Again: the next row |
| SELECT | Choose a row |
| KNOB1–4 | The chosen row's four values |
| SAVE | Save the patch (a built-in one goes to the first free slot) |
| SEQ | Undo the edits |
| PLAY | All notes off |
| OCT− / OCT+ | Octave |

MIDI in over USB and TRS: notes, pitch bend, the wheel and other controllers (as the patch routes them),
sustain pedal, aftertouch, program change (the patch) and MIDI clock (the tempo of the arpeggiator, the delay
and tempo-synced LFOs).

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
base/                 fm1-base (git submodule)
```

## Licence

MIT: see [LICENSE](LICENSE). Third-party material keeps its own licence: [THIRD_PARTY.md](THIRD_PARTY.md).
