# Title

I turned the M-VAVE FM-1 into a Synth1-compatible hardware synth — thousands of patches to explore

# Body

I've been building **SY1**, alternative firmware for the **M-VAVE FM-1** with an independent Synth1-compatible synth engine.

The big appeal for me is the sound library: years of community sound design, from deep basses and lush pads to leads, plucks and evolving textures. You can start with a finished sound and make it your own on the hardware.

The browser-based **Patch Manager already includes 3,174 presets across 27 community banks**, ready to explore. Search, filter, save favourites and listen before loading anything onto the hardware.

With SY1 installed, adding sounds is simple:

1. Connect the FM-1 by USB and open the Patch Manager in Chrome or Edge.
2. Click **Connect** and allow MIDI access.
3. Choose individual patches or a whole bank, then click **Send**.

The manager checks available space and shows transfer progress. By default it adds sounds after your existing patches. You can also drag in your own Synth1 `.sy1` files, bank ZIPs or a ZIP containing multiple banks — everything is imported locally in your browser.

The FM-1 holds **1,024 user patches at a time, plus the eight built-in sounds**. Once transferred, they're stored on the device and ready to play without a computer.

Everything is synthesized live on the FM-1: two oscillators plus a sub, FM/ring/sync, filters, envelopes, LFOs, effects and an arpeggiator. All **99 Synth1 parameters** are editable on the device.

Version 0.2.1 adds three screen styles: NORD, GREEN LED and GREEN LCD. Choose bright green pixels on black or dark lettering on a classic green display, with clear parameter feedback and live envelope/LFO visuals.

It's an early release; close sound matching with the original Synth1 hasn't been validated yet. Original patchmakers keep their credits and terms.

**Credits — this project builds on other people's work:**

- **Ichiro “Daichi” Toda / Daichi Laboratory**, creator of the original [Synth1](https://daichilab.sakura.ne.jp/softsynth/index.html).
- **Charles Vestal and the [fm1-base](https://github.com/charlesvestal/fm1-base) contributors**, for the firmware foundation, platform and tools.
- **Leo Kuroshita / Hügelton Instruments**, for the original Felucca firmware and [felucca-bsp](https://github.com/hugelton/felucca-bsp), the hardware foundation underneath fm1-base.
- **Francois Chabloz / [oxo_synth2](https://github.com/chab/oxo_synth2)**, for the measured Synth1 parameter laws, ranges and legacy-patch conversion data used by SY1 under the MIT license.

Thanks also to the community patchmakers: **AZur Studio, Christoph Mueller, Rob “Lunatique” Chang, Nolwenn, Reinhard Reschner / Echopark Studio and SOS**. The source collection is [preserved on Internet Archive](https://archive.org/details/synth1_202202), whose metadata credits **techno-addicts** as the compiler. Each bank keeps its original credits and terms: [preset credits](https://github.com/fenderberg/fm1-sy1/blob/main/app/site/library/CREDITS.md).

SY1 is an independent reimplementation; its engine contains no original Synth1 code. See the [code provenance and licenses](https://github.com/fenderberg/fm1-sy1/blob/main/THIRD_PARTY.md) and [full upstream credits](https://github.com/charlesvestal/fm1-base/blob/main/CREDITS.md) for the foundations and their contributors.

**Try it and browse the sounds:** https://fenderberg.github.io/fm1-sy1/

**Source and firmware:** https://github.com/fenderberg/fm1-sy1

Which Synth1 banks would you load first?

# Image caption

SY1 in the browser emulator, showing the same synth engine and screen UI used on the FM-1. Emulator screenshot, not a photograph of the physical unit.
