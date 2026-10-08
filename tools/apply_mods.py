#!/usr/bin/env python3
"""Build the ROM from asm/, then apply asar patches from mods/ on top.

    python tools/apply_mods.py                 # every mods/*.asm, alphabetical
    python tools/apply_mods.py short_races     # only the named mods

Output: build/smk_mod.sfc (play it with bin/win/smkplay.exe build/smk_mod.sfc)
"""
import glob, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_asar():
    for p in (os.path.join(ROOT, "bin", "win", "asar.exe") if os.name == "nt" else "",
              os.path.join(ROOT, "bin", "asar"), shutil.which("asar") or ""):
        if p and os.path.exists(p):
            return p
    sys.exit("asar not found (bin/win/asar.exe on Windows, or put asar on PATH)")


def main():
    asar = find_asar()
    build = os.path.join(ROOT, "build")
    base = os.path.join(build, "smk_rebuilt.sfc")
    r = subprocess.call([sys.executable, os.path.join(ROOT, "tools", "build.py"), os.path.join(ROOT, "asm"), base,
                         "--asar", asar])
    if r:
        sys.exit("base build failed")
    out = os.path.join(build, "smk_mod.sfc")
    shutil.copyfile(base, out)
    names = sys.argv[1:]
    mods = sorted(glob.glob(os.path.join(ROOT, "mods", "*.asm")))
    if names:
        mods = [m for m in mods if os.path.splitext(os.path.basename(m))[0] in names]
    for m in mods:
        print("applying", os.path.basename(m))
        if subprocess.call([asar, "--fix-checksum=on", m, out]):
            sys.exit("patch failed: " + m)
    print("wrote", out)


if __name__ == "__main__":
    main()
