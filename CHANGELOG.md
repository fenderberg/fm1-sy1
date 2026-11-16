# Changelog

## 0.3.0 — 2026-11-16

**500 community presets are included in the firmware and browser synth, ready from first boot.**

- Factory 500 is a separate, read-only bank in program flash: **F001–F500**. From ALL, turn
  ALGORITHM left once to FACTORY, then browse with PRESETS. The eight original sounds remain.
- All **1,024 user slots** remain available. Existing user records, slot numbers, MIDI program
  numbers and saved selections are preserved. SAVE makes an editable user copy; erasing user
  patches leaves the factory bank available.
- 500 distinct parameter sets from all 27 redistributable website banks, balanced as 100 Bass,
  100 Pad, 90 Lead, 80 Keys, 50 Pluck, 40 Arp, 30 FX and 10 Other. Every selection renders audible,
  finite audio in a two-second four-note host test. Maximum pre-limiter peak: 3.43. This is automated
  curation and testing, not individual listening tests or a claim of exact original-Synth1 matching.
- The manager's Factory 500 filter previews the included selection. A downloadable ZIP retains
  every selected original .sy1 file and complete original source-bank readmes. Full credits and
  readme text are also embedded in the firmware and readable over SysEx command 0x06.
- Original author terms remain separate from the MIT code license. The website still offers all
  3,174 public presets, original bank downloads and source attribution.
- NORD, GREEN LED and GREEN LCD remain available, with current screenshots and documentation.

Validation: 202 engine checks; 3,064 app checks including all 500 records, category browsing,
user-copy saving, erasing, full user storage, preserved MIDI IDs and read-only credits; 62 reader/ZIP
checks; all 500 source files, 27 banks and full readmes verified; 255 simulator checks and 142
screenshots. Baseline audio remains byte-identical. Firmware uses about 359 KB of program flash;
RAM data/bss and the audio pool remain unchanged.

## 0.2.1 — 2026-11-16

Choose the display that suits your FM-1: **NORD**, **GREEN LED**, or **GREEN LCD**.

- **Display choice:** GLO → DISPLAY (6/6), KNOB1. NORD keeps the red strip, ivory labels and amber focus.
  GREEN LED uses green pixel letters and segment numerals on black. GREEN LCD puts dark lettering and
  graphs on a soft green background, with a lighter focus panel.
- **Same controls and feedback:** all 32 subpages retain saved-value marks, units, edit indication,
  graphs, envelope/LFO/arp motion, and screen-reader text in every style.
- **Remembered preference:** saved globally while idle. Older 12-byte settings migrate while retaining
  the selected patch, octave, tempo, voice limit, category and drone choice. Changing style does not edit
  the sound or send audio-engine events.
- **Current screenshots:** 137 real simulator captures, plus a three-style comparison on the website,
  updated manual and the [full screen gallery](https://fenderberg.github.io/fm1-sy1/manual/screens/).
- **Browser panel:** homepage knob styling is scoped to the decorative rack, keeping emulator controls
  in their original positions and appearance.
- **Patch library:** 3,174 presets / 27 original banks remain available with author credits and terms.

Validation: 202 engine checks, 445 app checks, 62 JavaScript/C and ZIP checks, all 3,174 preset files,
234 simulator checks, 32 subpages in three styles, and browser checks for style changes, persisted reloads
and screen-reader values. The baseline WAV remains byte-identical. Firmware and WebAssembly build checks pass.
The preceding 0.2.1-green candidate was installed on a physical FM-1: 513 slots and the selected patch were
retained; LCD selection was confirmed and the device reported no crash. Screen readability and audio load
during playing have not been fully assessed. This remains an early release.

## 0.2.0 — 2026-11-16

SY1 now puts its Synth1 sound library first, with a complete Nord-inspired screen and website redesign.

- **Sound library:** 3,174 presets in 27 original community banks, with author credits, complete readmes
  and original ZIP downloads. Preset terms remain separate from the MIT source-code license.
- **Patch Manager:** search, bank and suggested-character filters, persistent favourites, individual or
  whole-bank selection, and browser-synth previews. Imports `.sy1`, folders, bank ZIPs and nested ZIP
  collections locally; tested with the supplied 175-bank / 16,698-patch collection.
- **USB transfer:** reads device slots, adds after the last patch by default, checks the complete selection
  against 1,024 slots, confirms occupied-slot replacement, shows progress, and keeps unsent patches for retry.
  MIDI responses are matched by command, input port and slot. Stop waits for the current acknowledgement.
- **Firmware UX:** consistent patch header, red section strip, sound display, four encoder columns and
  playing-status footer across all 31 subpages. Saved-value marks, precise edit indication, remembered
  subpages, units, live envelope/LFO/arp feedback and accessible spoken values.
- **Performance:** arp chord hold, drone stack/replace modes, category browsing and stored category overrides.
- **Website and manual:** shared navigation across Overview, Play, Patch Manager, Install and Manual;
  matching firmware, WebAssembly emulator, reader words and installation package.

Validation: 202 engine checks, 220 app checks, 62 JavaScript/C and ZIP checks, integrity checks for all
3,174 hosted presets, 130 simulator UX checks with 57 screen captures, and browser integration checks
including the full local collection. Browser USB tests use a simulated MIDI device; the physical FM-1
was checked with the redesigned development firmware while retaining its existing 513 patch slots.
The engine's sound has not yet been closely compared with original Synth1. This remains an early release.
