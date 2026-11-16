# SY1

SY1 is a polysynth for the M-VAVE FM-1 that plays Synth1 patches. This manual explains the screen, the pages,
how to get patches into the FM-1, and MIDI. Version **0.3.0** includes 500 community factory presets plus NORD, GREEN LED and GREEN LCD display styles.

![SY1's Play page: Warm Pad, a filter diagram, four encoder values and the playing status](img/screen-play.png)

---

## Contents

1. Install
2. The screen
3. Patches
4. Pages and editing
5. Loading your own patches
6. MIDI
7. What differs from Synth1
8. Troubleshooting

---

## 1. Install

To try it first, open the emulator. It runs the same code in your browser, with sound.

To install, open the installer in Chrome or Edge with the FM-1 connected by USB, and follow it. Back up what
you want to keep from the firmware you have now: SY1 keeps its settings and patches in the same part of the
flash, and overwrites what another firmware left there. The installer can put M-VAVE's own firmware back.

## 2. The screen

The patch number, browsing category and patch name stay at the top. EDIT and the SAVE light mean that the
patch differs from the saved version. Turning every value and the patch category back to their saved settings
clears EDIT again. Short save and error messages appear in the red strip without hiding the patch name.

The red strip names the current subpage and its position, such as FILTER / MAIN 1/2. The large diagram below it
shows that section's settings. Four fixed columns correspond left to right to KNOB1–KNOB4. The last encoder
you turn lights amber; a small grey notch marks its saved value. A new subpage starts without encoder focus.
Switches and choices use separate positions; a centred parameter has a centre mark.

Filter shapes, oscillator waveforms and effect diagrams are schematic setting views. The envelope's moving point
follows the most recently started voice through its release, the LFO point follows its actual phase/value,
and the arp shows the current step/note. Random LFOs show recent values. These are not audio oscilloscopes.

Values use meaningful units where the engine supplies a reliable conversion: attack in ms or s, pitch in semitones
and cents, sustain in percent and LFO speed in Hz or exact fractions of a beat. D.TAU, R.TAU and M.TAU are decay
and release **time constants**, not the total duration of a note. Pulse width shows the duty of the low part
(50% at its maximum). Other values keep their Synth1 numbers. GLO's DIAGNOSTIC subpage shows CPU,
peak CPU, voice-limit events and the raw value of the last edited Synth1 parameter. Its four encoders are read-only.

The bottom always shows active/max voices, octave, tempo and its source: INT for internal, EXT for MIDI clock.
MIDI lights briefly when a note arrives. The second line explicitly shows ARP ON/OFF, HOLD ON/OFF or
DRONE / STACK or REPLACE. LIMIT means that the CPU has reduced the voice limit during this session.

### Choosing a display style

On **GLO → DISPLAY (6/6)**, turn **KNOB1** to choose **NORD**, **GREEN** (LED) or **LCD**. The change is immediate.
NORD keeps the red section strip, ivory text and amber focus. GREEN LED uses a dark green background,
uppercase 5×7 pixel lettering and seven-segment numerals. GREEN LCD reverses the contrast with dark
characters and graphics on a soft green background; the focused control gets a lighter panel and dark
underline. Units, patch names, saved-value marks,
encoder focus, graphs, playing status and live envelope/LFO feedback remain available in all three styles.
The screen reader keeps the original text and announces the style choice.

![NORD display: Warm Pad with the red section strip and ivory labels](img/screen-play.png)

![GREEN LED display: green pixel letters and segment numerals on black](img/screen-green-play.png)

![GREEN LCD display: dark letters and graphs on a green background](img/screen-lcd-play.png)

Compare every page in the [screen gallery](https://fenderberg.github.io/fm1-sy1/manual/screens/): 32 subpages in all three styles.

This is a global preference, separate from the sound. It is automatically saved when the synth is idle;
existing patch edits and imported banks are preserved. Upgrading from older 12-byte settings keeps the
selected patch, octave, voice limit, tempo, category and drone preference, and starts in NORD.

## 3. Patches

Turn ALGORITHM to choose a category, and PRESETS for the next or previous patch within it. Both wrap at the
ends. Empty categories are skipped. ALL contains every patch; USER contains every patch in the user bank. FACTORY contains the 500 bundled community presets.
The musical categories are BASS, PAD, LEAD, KEYS, PLUCK, ARP, FX and OTHER.

**500 community presets are ready from the first boot**, numbered **F001–F500**. From ALL, turn
**ALGORITHM left once to FACTORY**, then use PRESETS. No import or USB transfer is needed.
![F001 FM Slap Bass in the included Factory 500 bank, using the Green LCD display](img/screen-factory-lcd.png)

These presets live in program flash and leave the 1,024 user slots available. Existing user slots,
stored selections and MIDI program numbers are preserved when upgrading.

`P01` to `P08` are the eight original built-in sounds. Your own patches are numbered `001` to `1024`;
empty slots are skipped. From the manager, the slot count always refers to these user slots.

SAVE keeps the patch with your changes. An original or factory patch is saved as a copy to the first free user slot, keeping the built-in original. REC puts
the patch back as it was saved and releases hold/drone. Changing patches also releases hold/drone.

Imported patches are categorized from their names, ignoring case (for example, "deep bass", "Warm Pad" and
"FM Bell"). Unrecognized names go under OTHER. To correct a category, use GLO (Voice), SELECT to row 4,
then KNOB3 (CAT), and SAVE. Editing CAT returns the browser to ALL. KNOB4 on that row selects the Drone mode.
MIDI program changes select their original patch number; if necessary the browser returns to ALL.

## 4. Pages and editing

| Button | Page | What is on it |
| --- | --- | --- |
| HOME | Play | Filter frequency, resonance and amount, gain; portamento, pan, tempo, voices |
| EDIT | Osc | Both oscillators, mix, pulse width, sync, ring, FM, detune, the sub, the mod envelope |
| SEL | Filter | Type, frequency, resonance, envelope amount, keyboard tracking, saturation |
| ENV | Env | The amp envelope, the filter envelope, gain and velocity |
| LFO | LFO | Both LFOs: destination, wave, speed, depth, tempo and key sync |
| FX | Effects | The effect, the equalizer, the delay and the chorus |
| Hold ARP 0.5 s | Arp | The arpeggiator: on, type, range, beat, gate, tempo; Drone mode |
| GLO | Voice | Poly, mono or legato, portamento, bend range, unison, polyphony, pan |

Press a page's button again for its next subpage (hold again for ARP), or turn SELECT to choose one. Each page
remembers its last subpage during the session. KNOB1 to KNOB4 change the four displayed values; a fast turn goes
further. This navigation memory is not written to flash. A restart begins on HOME.

The screen reader announces the subpage and the same displayed value/unit when you turn a knob. Read whole
screen also includes the four controls and the current playing state. Animations do not generate spoken updates.

PLAY silences every note. OCT- and OCT+ move the keys by an octave.

### Arp, HOLD and Drone

- **Tap ARP:** switch the arpeggiator on or off from any page. Its light shows on/off, including on the Arp page.
  Hold ARP for half a second to open its settings without toggling it. Hold again to advance the row.
- **SEQ with arp on:** toggle HOLD. Notes keep arpeggiating after key-up. Notes added while any key is down
  belong to the same chord; after all keys are up, the next gesture replaces it.
- **SEQ with arp off:** toggle Drone. In **STACK**, notes accumulate; play a droning note again to remove it.
  In **REPLACE**, the next gesture after all keys are up replaces the previous chord. The old chord uses its
  release envelope, and the new chord its attack; available polyphony still limits overlapping voices.
- **Change Drone mode:** Arp row 2, KNOB3, or Voice row 4, KNOB4. The setting is remembered.
- Switching ARP while SEQ is lit changes the held chord between arpeggiation and Drone.
- Toggle SEQ off to release latched notes; physical keys and the sustain pedal keep their own notes.
  PLAY, MIDI Stop, MIDI All Notes Off and MIDI All Sound Off clear the latch. Hold/drone never resumes at power-on.

These controls work with the FM-1 keys and incoming MIDI. A Drone keeps notes held, so a patch with zero amp
sustain still decays naturally. There is no sequencer or note recording.

VOICES, on the Play and Voice pages, is the most voices SY1 plays at once, 1 to 16. The FM-1 manages about
seven. When the processor runs out of time, SY1 lowers it by one and says CPU FULL.

## 5. Loading your own patches

The Patch Manager on SY1's website includes **3,174 community sounds in 27 banks**. Search by name,
filter by bank or suggested sound character, and mark favourites. Character is inferred from names,
so it is a starting point rather than a guarantee. Select individual sounds or all the filtered sounds.
Click **Play** beside a sound, switch on the browser synth, and use its keyboard or preview phrase to
hear it before loading hardware. Preview uses the separate emulator and stores the sound in its first
imported slot; it never sends patches to USB hardware.

To bring your own collection, use **Import your banks**, choose a folder, or drop `.sy1` files and ZIPs.
ZIPs containing other bank ZIPs work too. Even the complete supplied collection (175 banks / 16,698 patch
entries) can be browsed locally. Select a bank or narrow the search before selecting sounds: the FM-1
holds **1,024 imported patches**, in addition to the 500 community factory presets and eight original built-in patches. Imports remain in this tab
until it is closed or reloaded; favourite IDs are remembered in this browser. Imported files are not uploaded.

Connect the FM-1 by USB, open the manager in Chrome or Edge, press **Connect**, and allow MIDI with SysEx.
The manager reads existing slots and names. **Add after my last patch** is the default destination.
**Choose a starting slot** lets you replace patches; the manager shows the overlap and asks before replacing
occupied slots. A selection must fit in full. Press **Send** to transfer, with acknowledgement for every
patch and visible progress. **Stop after this patch** stops between acknowledged writes. Unsent patches
stay selected for retry. After a lost acknowledgement, reconnect and check the device's slots first.

**On your FM-1** lists existing patches and offers individual removal. Erasing all imported patches requires
confirmation; the built-in patches stay. Turn PRESETS past the built-in patches to play imported sounds.

The collection comes from [Internet Archive](https://archive.org/details/synth1_202202); its metadata credits
the compilation to techno-addicts. Our count of the supplied ZIP is 175 banks / 16,698 patch entries.

Use the manager's **Factory 500** filter to preview the included sounds. The [downloadable selection](https://fenderberg.github.io/fm1-sy1/library/factory500.zip) contains the original patch files and complete original readmes.

Each hosted bank keeps its original ZIP, readme, author credits and distribution terms in **About this bank**.
These presets have their authors' own terms, separately from the code's MIT license. The full supplied
collection contains banks that restrict redistribution; those can be imported locally but are not hosted.
Patches from older Synth1 versions are converted as Synth1 itself converts them.

## 6. MIDI

MIDI comes in over USB and the TRS jack. SY1 plays notes with velocity, pitch bend, the sustain pedal and
channel pressure. The modulation wheel and other controllers do what the patch routes them to. A program change
selects the patch of that number. A MIDI clock sets the tempo of the arpeggiator, the delay and the LFOs that
are synced to the tempo. After two seconds without clock ticks SY1 returns to the internal tempo.
MIDI Stop releases all notes and disables hold/drone. The arp follows the clock's measured tempo; it does not
yet align its step phase to the external transport grid.

The FM-1's own keys also go out as notes over USB.

## 7. What differs from Synth1

SY1 follows Synth1's structure and its measured parameter laws, but it is a lighter engine made for the FM-1,
not an exact copy:

- The LPDL filter plays as the 24 dB filter, and the effect a.d.1 uses a.d.2's curve.
- The delay is 0.74 seconds a side at most; a longer time plays an octave shorter.
- With the delay and the chorus both on, the echoes also reach the output directly.
- Osc1's detune does not thicken the sub.
- It plays about seven voices, where Synth1 plays up to 32.

## 8. Troubleshooting

- **The sound breaks up on big chords.** Lower VOICES on the Voice page.
- **The Patch Manager finds no FM-1.** Use Chrome or Edge, allow MIDI for the page, and check the USB cable
  carries data. Close other programs that hold the FM-1's MIDI port.
- **A patch sounds different from Synth1.** See section 7; reports with the patch are welcome at the source's
  issue tracker.
- **To go back to another firmware,** use that firmware's installer, or SY1's installer for M-VAVE's own.
