# SY1

SY1 is a polysynth for the M-VAVE FM-1 that plays Synth1 patches. This manual explains the screen, the pages,
how to get patches into the FM-1, and MIDI.

![SY1's Play page: the patch number P02 and its name Warm Pad at the top, two rows of four values, and at the bottom the voices, the processor load, the octave and the tempo](img/screen-play.png)

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

The top shows the patch's number and name, and the page you are on. A patch you have changed says EDITED, and
SAVE lights.

The middle shows the page's rows. Each row has up to four values; the lit row is the one KNOB1 to KNOB4 turn.

The bottom shows the voices sounding and the most allowed, the processor's load, the octave of the keys, the
tempo (CLOCK while a MIDI clock sets it), and MIDI, which lights when notes come in.

## 3. Patches

Turn PRESETS for the next or the previous patch, and ALGORITHM to go ten at a time.

`P01` to `P08` are built in. Your own patches follow them, numbered `001` to `1024`; empty slots are skipped.

SAVE keeps the patch with your changes. A built-in patch is saved to the first free slot of your own. SEQ puts
the patch back as it was saved.

## 4. Pages and editing

| Button | Page | What is on it |
| --- | --- | --- |
| HOME | Play | Filter frequency, resonance and amount, gain; portamento, pan, tempo, voices |
| EDIT | Osc | Both oscillators, mix, pulse width, sync, ring, FM, detune, the sub, the mod envelope |
| SEL | Filter | Type, frequency, resonance, envelope amount, keyboard tracking, saturation |
| ENV | Env | The amp envelope, the filter envelope, gain and velocity |
| LFO | LFO | Both LFOs: destination, wave, speed, depth, tempo and key sync |
| FX | Effects | The effect, the equalizer, the delay and the chorus |
| ARP | Arp | The arpeggiator: on, type, range, beat, gate, tempo |
| GLO | Voice | Poly, mono or legato, portamento, bend range, unison, polyphony, pan |

Press a page's button again for its next row, or turn SELECT to choose a row. KNOB1 to KNOB4 change the row's
four values; a fast turn goes further.

PLAY silences every note. OCT- and OCT+ move the keys by an octave.

VOICES, on the Play and Voice pages, is the most voices SY1 plays at once, 1 to 16. The FM-1 manages about
seven. When the processor runs out of time, SY1 lowers it by one and says CPU FULL.

## 5. Loading your own patches

Open the Patch Manager on SY1's site in Chrome or Edge, with the FM-1 connected by USB. Press Connect, drop your
`.sy1` files, folders or `.zip` banks on the page, and send them. The page shows what is on the FM-1 and can
remove a patch or erase them all.

From a command line, `tools/sy1_send.py` in the source does the same.

Patches made with old versions of Synth1 are converted as Synth1 itself converts them.

## 6. MIDI

MIDI comes in over USB and the TRS jack. SY1 plays notes with velocity, pitch bend, the sustain pedal and
channel pressure. The modulation wheel and other controllers do what the patch routes them to. A program change
selects the patch of that number. A MIDI clock sets the tempo of the arpeggiator, the delay and the LFOs that
are synced to the tempo.

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
