#!/usr/bin/env python3
"""Send Synth1 patches to an FM-1 running SY1, over USB MIDI.

  tools/sy1_send.py [--first N] [--port NAME] PATCH.sy1 | FOLDER | BANK.zip ...
  tools/sy1_send.py --count            how many slots are in use
  tools/sy1_send.py --erase            erase every loaded patch (the built-in ones stay)

The patches go into the slots N, N + 1, ... (as the FM-1's screen counts them, from 1). Without --first they
follow the patches already there. Each patch is one SysEx frame (app/bank.h), made by build/sy1pack (`make all`)
and acknowledged by the FM-1 before the next is sent. Needs mido with python-rtmidi.
"""
import argparse
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

import mido

ROOT = Path(__file__).resolve().parent.parent
HEAD = [0x7D, 0x53, 0x59, 0x31]          # after F0: the non-commercial ID, "SY1"
SLOTS = 1024


def find_port(names, want):
    # by the name asked for; else the one named SY1; else any FM-1 (macOS keeps showing the name of the firmware
    # that was on it before: the count's answer then says whether it is SY1)
    tests = [lambda n: want.lower() in n.lower()] if want else [lambda n: "SY1" in n, lambda n: "FM-1" in n]
    for t in tests:
        for n in names:
            if t(n):
                return n
    return None


def ask(out, inp, data, answer, timeout):
    """a frame out (the bytes between F0 and F7), its answer back: the bytes after the command, or None"""
    for _ in inp.iter_pending():
        pass
    out.send(mido.Message("sysex", data=data))
    end = time.time() + timeout
    while time.time() < end:
        for m in inp.iter_pending():
            if m.type == "sysex" and list(m.data[:4]) == HEAD and len(m.data) > 4 and m.data[4] == answer:
                return list(m.data[5:])
        time.sleep(0.005)
    return None


def collect(paths, tmp):
    files = []
    for p in map(Path, paths):
        if p.is_dir():
            files += sorted(f for f in p.rglob("*") if f.suffix.lower() == ".sy1" and not f.name.startswith("._"))
        elif p.suffix.lower() == ".zip":
            with zipfile.ZipFile(p) as z:
                for i, name in enumerate(sorted(n for n in z.namelist()
                                                if n.lower().endswith(".sy1") and "/._" not in "/" + n)):
                    f = tmp / f"{p.stem}-{i:04d}.sy1"
                    f.write_bytes(z.read(name))
                    files.append(f)
        elif p.exists():
            files.append(p)
        else:
            sys.exit(f"{p}: no such file")
    return files


def main():
    ap = argparse.ArgumentParser(description="Send Synth1 patches to an FM-1 running SY1")
    ap.add_argument("patches", nargs="*", help=".sy1 files, folders of them, or .zip banks")
    ap.add_argument("--first", type=int, help="the first slot to fill (from 1; default: after the last one in use)")
    ap.add_argument("--port", help="the FM-1's MIDI port (part of its name; default: the one named SY1)")
    ap.add_argument("--count", action="store_true", help="print how many slots are in use")
    ap.add_argument("--erase", action="store_true", help="erase every loaded patch first")
    a = ap.parse_args()
    if not (a.patches or a.count or a.erase):
        ap.error("nothing to do: give patches, --count or --erase")

    name_in = find_port(mido.get_input_names(), a.port)
    name_out = find_port(mido.get_output_names(), a.port)
    if not name_in or not name_out:
        sys.exit("No FM-1 running SY1 found on USB. Ports: " + ", ".join(mido.get_output_names()))
    with mido.open_input(name_in) as inp, mido.open_output(name_out) as out:
        if a.erase:
            r = ask(out, inp, HEAD + [0x02], 0x42, 15)
            if not r or r[0] != 0:
                sys.exit("The FM-1 did not confirm the erase.")
            print("All loaded patches erased.")
        r = ask(out, inp, HEAD + [0x03], 0x43, 3)
        if not r:
            sys.exit(f"{name_out} does not answer as SY1.")
        used = r[0] | r[1] << 7
        if a.count or not a.patches:
            print(f"{used} of {SLOTS} slots in use.")
        if not a.patches:
            return
        packer = ROOT / "build" / "sy1pack"
        if not packer.exists():
            sys.exit("build/sy1pack is missing: run `make all` first.")
        with tempfile.TemporaryDirectory() as t:
            tmp = Path(t)
            files = collect(a.patches, tmp)
            if not files:
                sys.exit("No .sy1 files in what you gave.")
            first = a.first if a.first else used + 1
            if first < 1 or first > SLOTS:
                sys.exit(f"--first must be 1..{SLOTS}")
            syx = tmp / "out.syx"
            # (in chunks: a bank of a thousand files is a long command line)
            frames, names = [], []
            for at in range(0, len(files), 200):
                p = subprocess.run([str(packer), str(syx), "-first", str(first + len(frames)), *map(str, files[at:at + 200])],
                                   capture_output=True, text=True, errors="replace")
                sys.stderr.write(p.stderr)
                raw = syx.read_bytes() if syx.exists() else b""
                frames += [f + b"\xf7" for f in raw.split(b"\xf7") if f]
                names += [ln[6:] for ln in p.stdout.splitlines() if ln[:4].strip().isdigit()]
            stored = 0
            for i, f in enumerate(frames):
                slot = f[6] | f[7] << 7
                r = ask(out, inp, list(f[1:-1]), 0x41, 5)
                label = names[i] if i < len(names) else ""
                if not r or len(r) < 3 or r[2] != 0:
                    print(f"{slot + 1:04d}  NOT STORED  {label}")
                    continue
                stored += 1
                print(f"{slot + 1:04d}  {label}")
            print(f"{stored} of {len(frames)} patches stored on the FM-1 ({name_out}).")
            if stored != len(frames):
                sys.exit(1)


if __name__ == "__main__":
    main()
