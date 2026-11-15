# SY1

SY1 turns the M-VAVE FM-1 into a polysynth that plays **Synth1 patches**. It has two oscillators and a sub,
FM, ring modulation and sync, four filter types, three envelopes, two LFOs, an arpeggiator, unison, ten effects,
an equalizer, a tempo delay and a chorus: Synth1's 99 parameters under Synth1's own numbers, so `.sy1` patches
load as they are. Every parameter can also be edited on the FM-1 itself.

Built for playing: tap ARP to switch arpeggiation on, SEQ to hold the chord (or a Drone when the arp is off).
Drone can stack notes or replace chords. ALGORITHM browses sound categories, PRESETS the sounds within them.
The panel and MIDI use the same performance controls. There is no sequencer.

![SY1's Play page: the patch Warm Pad, with the filter's frequency, resonance and amount and the gain on the four knobs](manual/img/screen-play.png "SY1 on the FM-1's screen")

This is version {{VERSION}}, an early one. It runs on an FM-1 with about seven voices, and its sound has not yet
been compared closely with the original Synth1's. You install it at your own risk, and the installer can put
M-VAVE's own firmware back.

- [Try it in the browser](emu/). Play it right here, with sound, and drop your own Synth1 patches on the page. It
  runs the same code as the FM-1, so you don't need one.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Patch Manager](manager/) loads `.sy1` files, folders and `.zip` banks into the FM-1 from the browser.
- The [Manual](manual/) explains the screen, the pages, patches and MIDI.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/fenderberg/fm1-sy1) is under the MIT licence.

SY1 is built on fm1-base and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments. Its
parameter laws are the measurements of the original Synth1 published by oxo_synth2 (MIT, Francois Chabloz).

Synth1 is the work of Daichi Laboratory. SY1 is not affiliated with or endorsed by Daichi Laboratory, contains
nothing of Synth1 itself, and comes with no third-party patches. M-VAVE and FM-1 are trademarks of their owners.
SY1 is not affiliated with M-VAVE.
