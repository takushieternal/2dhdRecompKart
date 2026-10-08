#!/usr/bin/env python3
"""Assemble the disassembly with asar and verify it against the original ROM.

    build.py <asm_dir> <out.sfc> [--asar path] [--verify original.sfc]
"""
import argparse, os, subprocess, sys, zlib, shutil

EXPECTED_CRC32 = 0xCD80DB86   # Super Mario Kart (USA), No-Intro


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asm_dir"); ap.add_argument("out")
    ap.add_argument("--asar", default=shutil.which("asar") or "asar")
    ap.add_argument("--verify", default=None, help="original ROM to diff against")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(bytes(0x80000))
    if not (os.path.exists(a.asar) or shutil.which(a.asar)):
        print("asar not found: pass --asar path/to/asar (Windows: bin\\win\\asar.exe)"); return 1
    r = subprocess.run([a.asar, "--no-title-check", "--fix-checksum=off",
                        os.path.join(a.asm_dir, "smk.asm"), a.out], capture_output=True, text=True)
    sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print("asar failed"); return 1
    data = open(a.out, "rb").read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print("built %s: %d bytes, CRC32 %08X %s" % (a.out, len(data), crc,
          "(matches original)" if crc == EXPECTED_CRC32 else "(MODIFIED / mismatch)"))
    if a.verify:
        orig = open(a.verify, "rb").read()
        diffs = [i for i in range(min(len(orig), len(data))) if orig[i] != data[i]]
        print("%d differing bytes" % len(diffs))
        for i in diffs[:20]:
            print("  $%06X: orig %02X built %02X" % (i, orig[i], data[i]))
        return 1 if diffs else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
