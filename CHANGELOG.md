# Changelog

## Unreleased

**Bitimbral: two sounds at once, made for the Novation Circuit Tracks.**

- **Two parts, L and R**, each with its own patch, arpeggiator, HOLD/Drone and effects. Part R renders on the
  FM-1's second processor core, so each part has the voices one SY1 had. An FM-1 whose second core does not run
  plays both on one. Standard mode is SY1 as before and does not use the second core.
- **OCT- and OCT+ together** switch between standard and bitimbral. In bitimbral a tap on OCT- or OCT+ puts the
  panel and the keys on L or R (its light on; PLAY green for L, red for R); holding one moves the octave.
- **Output: SPLIT** (L left, R right, each mono, for two inputs of a mixer or the Circuit), **STEREO**, or
  **DOUBLE** (every note plays both patches). GLO's new last subpage has the mode, the output and the parts' MIDI
  channels (3 and 4 to begin with, the Circuit Tracks' MIDI tracks).
- **The Circuit's eight macro knobs** move what their labels say (controllers 1, 2, 5, 11, 71, 74, 12, 13), from
  the patch as it is in the knob's middle, gliding, with the knob, its parameter and value on the screen.
- **The arpeggiator steps on the MIDI clock**, counted from Start, instead of following a measured tempo.
- Each part keeps its own category, octave, VOICES limit, Drone mode and panel page; the tempo is shared.
- Settings grow from 16 to 24 bytes; older settings load as they are. An older SY1 installed over this one starts
  with default settings (the patches stay).
- `tools/sy1_send.py` asks again when an answer is lost over USB.
- **The Patch Manager knows the parts.** A strip switches Standard/Bitimbral and the output; L and R buttons on
  every slot make the FM-1 play it on that part, any of the 1,024 (a program change reaches only the first 120);
  the slot map rings part L green and R red. In the library, **L** pairs a sound as part L and ▶ then plays others
  as part R with it in the browser synth; **Add pair** stages both side by side.
- SysEx: 08's answer adds the parts' channels and the user slots they play; new **09** makes a part play a user
  slot. The 03 answer's features byte announces both (bit 1), so an older SY1 never receives them.

Validation: `make test`, `make test-app`, the simulator scripts with the new `app/tests/bitimbral.txt` (mode,
parts, channels, outputs, knobs, lights, clock). On one FM-1: the load of two parts on two cores and on one,
the mode switch, the part switch and the channel routing. **Not yet on the FM-1:** DOUBLE, the knobs with a real
Circuit, the arpeggiator against a real clock, and a long run of the second core.

**The Patch Manager becomes a librarian for the FM-1's 1,024 user slots.**

- Two panes: the library on the left, **My FM-1** on the right, with a map of all 1,024 slots coloured by
  character. Add sounds by drag or button, drag slots to reorder, rename, set a character, remove, close
  gaps, sort A–Z, group by character or bank.
- Every edit is staged in the browser with undo. The sync bar lists what will change; **Sync** writes only
  the changed slots, each acknowledged, and can be stopped or retried. Removing patches asks first.
- **Sets** save whole layouts in the browser, with file export and import, and an automatic backup of the
  old layout before a sync that moves, changes or removes patches.
- Preview plays inside the page through a hidden browser synth instead of a new tab; arrow keys and Space
  audition quickly. The library shows which sounds are already on the FM-1 and in which slot.
- Firmware: new read-only SysEx command `07` returns a slot's whole record in two parts, so the manager can
  move and back up any patch, including ones saved on the FM-1. The manager detects an FM-1 without it
  (0.3.1 and earlier) and then only adds and removes.

Validation so far: library, record and layout checks in `host/library_test.mjs`; the manager in Chrome
against a simulated FM-1 (`host/manager_browser_test.mjs`). **Not yet done:** the firmware change and its
new check in `host/app_test.c` have not been compiled or run, and nothing was tested on a physical FM-1.

## 0.3.1 — 2026-11-16

- Assign SY1 its own project prefix 51: package and full USB identity **FM-1_51000301**, replacing the prefix 47 range already used by FRASIL. The fm1-base submodule is unchanged.
- Add the root `fmhub.json` with the current synth description, maker name, emulator, Patch Manager, manual, square GREEN LCD screenshot and preset rights declaration.
- Keep all 500 factory presets, eight original sounds, user-slot IDs and saved settings from 0.3.0.
- Update the release, website and documentation, and record the hub requirements and physical test status in `docs/FIRMWARE-HUB.md`.

Validation: release package identity and embedded Felucca loader checked; hub metadata, image and original preset readmes checked. The exact release package was installed from official V15 and Dinghy 0.5 on one physical FM-1; leaving SY1 for Dinghy and V15 worked. The full identity and factory F001/F500 browsing were confirmed after both installs. All 513 existing user patches and settings were backed up, restored and read back byte for byte; the final public firmware retained the count and selected patch and reported no crash.

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
