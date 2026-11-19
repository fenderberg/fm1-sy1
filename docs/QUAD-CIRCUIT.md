# QUAD Circuit experiment

Local worktree: `../synth1-quad`, branch `experiment/quad-circuit`, based on `6ba0cb1` plus a copy
of the original workspace's existing changes. The original `synth1` workspace stays untouched.
Nothing is installed on hardware or published by creating this experiment.

## Use

GLO retains PART, BITIMBRAL, DISPLAY and DIAGNOSTIC in their existing positions and appends:

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| QUAD (5/6) | Standard / Bitimbral / Quad | Part 1–4 | Split / Stereo | Selected part's voice limit |
| QUAD / MIDI CHANNELS (6/6) | Part 1 channel | Part 2 channel | Part 3 channel | Part 4 channel |

In QUAD, tap OCT- / OCT+ for the previous/next part; hold for octave. Both buttons together return
to Standard. Enter QUAD again in settings. Each part has its own preset, edits, category browser,
octave, arp, Hold/Drone and effects. Standard/Bitimbral use their original two-part settings and sounds.
Mode/channel/output changes stop notes; held keys send note-off to the part and outgoing channel
on which they started. Use Save to retain edits to a sound across power-off.

## Circuit Tracks

| FM-1 part | Circuit track | Default channel | Macro controllers |
| --- | --- | --- | --- |
| 1 | Synth 1 | 1 | CC80–87 |
| 2 | Synth 2 | 2 | CC80–87 |
| 3 | MIDI 1 | 3 | CC1,2,5,11,12,13,71,74 |
| 4 | MIDI 2 | 4 | CC1,2,5,11,12,13,71,74 |

The eight macros keep the existing musical mapping: oscillator mix, oscillator modulation,
amp envelope, filter envelope, cutoff, resonance, modulation and FX. Routing is by each part's
configured channel, while macro type follows the part's role above. Shared channel assignments
intentionally layer matching parts. Drum channel 10 is ignored with the default channels.

Enable Circuit MIDI notes, CC, program change and clock transmission as needed. Circuit Start/clock
drive all four arpeggiators together; after clock stops they return to the shared internal tempo.

Circuit synth-track parameter automation reaches USB only. Use a computer as MIDI host/router to
forward the Circuit's USB MIDI to the FM-1 when using all four tracks' macro automation. Connecting
the two devices' USB device ports directly is insufficient: this firmware does not enable USB host.
The two MIDI tracks' template automation also reaches DIN; notes/clock via a suitable DIN-to-TRS MIDI
connection remain useful, with the synth-track macro restriction above.

In **Split**, connect FM-1 L to Circuit Input 1 and R to Input 2: parts 1+2 share Input 1, parts 3+4 share
Input 2. Input level, pan and Circuit effects act on those groups. **Stereo** preserves each part's
stereo signal in the summed FM-1 output; pan the receiving mixer inputs as a stereo pair.
Circuit's internal synths can also sound while their sequencers drive the FM-1; choose the desired
internal sound/mix, or silent internal patches if the FM-1 should supply those sounds entirely.

Primary references:

- [Novation: synths and MIDI tracks](https://userguides.novationmusic.com/hc/en-gb/articles/26207128061458-Using-Circuit-Tracks-Synths-MIDI-Tracks-and-Drums)
- [Novation: Programmer's Reference v3](https://fael-downloads-prod.focusrite.com/customer/prod/downloads/circuit_tracks_programmer_s_reference_guide_v3.pdf)
- [Novation: MIDI channels and setup](https://userguides.novationmusic.com/hc/en-gb/articles/25494477003154-Circuit-Tracks-appendix)

## CPU and memory

Two parts run sequentially on each core: 1+3 on CPU0, 2+4 on CPU1, so both cores contribute to both
split buses. Each starts at three notes; the configurable total is six per core, one to five per part.
An overload reduces the busier core's eligible part, down to one note. The existing single-core and
dropped-worker fallback paths render all four parts on CPU0 when required. This does not guarantee
that four heavy patches fit at full polyphony; actual hardware load and worst-case timing remain to measure.

One union reuses exactly **256 KiB** of effect buffers. Standard/Bitimbral retain the existing
32768/16384-frame stereo delays and two full chorus buffers. QUAD uses four 8192-frame stereo
delays (about 0.186 s per side) and four full chorus buffers. Longer echoes are octave-folded as before.
Only the audio interrupt changes the buffer bindings, before launching a worker. Effect tails are
cleared when changing between QUAD and legacy modes.

The settings object keeps its 24-byte legacy prefix and appends a versioned QUAD configuration;
12-, 16-, 20- and 24-byte legacy settings migrate. Each mode retains its own selected preset numbers,
voice limits and channel/output settings. The original 0.5 firmware does not understand the larger
settings object; downgrading can reset global settings, but the user patch bank is unchanged.

## SysEx extensions

The existing `F0 7D 53 59 31` header and `F7` terminator remain unchanged. Values are 7-bit bytes.

- Feature byte from `03` adds bit 2 (`4`), announcing QUAD support.
- `08 <mode> <output>` accepts mode `2` for QUAD, output `0` split or `1` stereo. `7F` keeps a field.
  Legacy modes retain their exact nine-byte reply payload. QUAD's `48` reply retains those fields
  with mode `2`, and appends `<ch3-1> <ch4-1> <slot3 lo7> <hi7> <slot4 lo7> <hi7>`.
  Slots are one-based, `0` for factory/built-in, as in the existing reply.
- `09 <part> <slot lo7> <hi7>` accepts parts `0..3` in QUAD; legacy retains parts `0..1`.
- `0A <focus> <output> <ch1-1> <ch2-1> <ch3-1> <ch4-1>` updates QUAD settings; `7F` leaves each field.
  Reply `4A <focus> <output> <ch1-1> <ch2-1> <ch3-1> <ch4-1>` reports the resulting settings.
  A query of all `7F` leaves sounding notes intact.

The Patch Manager gates QUAD controls on feature bit 2. Older firmware keeps its existing controls.
The browser's library preview remains limited to its existing one/two-sound preview.

## Validate and build

```sh
make test test-app test-web
./build.sh --host
mkdir -p app/build/quad-check
app/build/host/sy1_host app/tests/quad.txt app/build/quad-check
./build.sh --web
./build.sh
```

A development package is `app/build/sy1.fwsc`; this experiment also includes
`app/build/sy1-0.5.1-quad.fwsc`, with the USB product name **SY1 QUAD Experiment**.
Host tests cover each audio bus sample, FX isolation, all four MIDI/clock/macro routes, persistence,
legacy settings migration, CPU fallback and overload shedding. Simulator screenshots are generated
by `app/tests/quad.txt`; hardware timing, listening and the actual Circuit workflow remain unverified.

Validated locally on 2026-11-19:

- Engine: 202 checks; app: 5,640 checks, including the QUAD integration tests.
- Eight simulator scenarios pass: basic, performance, nord-preview, ux, display, factory, bitimbral, quad.
- Browser manager passes legacy/QUAD switching, four slot selectors, channels, stereo/split, older
  firmware gating, desktop and mobile layout, plus the existing librarian workflows.
- Site: 428 checks. Firmware, host and browser emulator build successfully.
- Firmware `.data+.bss`: 77,848 / 98,304 bytes; pool: 335,624 / 344,064 bytes.
- UndefinedBehaviorSanitizer passes. Its initial run exposed an existing signed exponent-bit shift;
  the same bit operation now uses unsigned arithmetic. AddressSanitizer itself could not initialize
  on this Mac, so there is no successful AddressSanitizer run.

Logs and checksums are in `build/quad-*.log`, `build/quad-validation.json`,
`app/build/simulator-checks/`, and `app/build/SHA256SUMS-quad.txt`.
The local complete site can be served with
`python3 -m http.server 8481 --bind 127.0.0.1 --directory app/build/quad-site`.
