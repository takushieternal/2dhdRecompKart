#!/usr/bin/env python3
"""Lockstep check: every coverage campaign is run twice, interpreter vs recompiled code, logging a
hash of WRAM+VRAM+CGRAM+OAM+CPU registers (and the master-cycle count) every frame. Any difference
is reported with the first frame where it happens.

    python tools/verify_recomp.py smk.sfc [--only tt,battle] [--work build/verify]
"""
import argparse, importlib.util, os, subprocess, sys, shutil
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = ".exe" if os.name == "nt" else ""


def load_campaigns():
    spec = importlib.util.spec_from_file_location("rc", os.path.join(ROOT, "trace", "run_coverage.py"))
    rc = importlib.util.module_from_spec(spec); spec.loader.exec_module(rc)
    return rc.campaigns()


def run_all(tracer, rom, d, scripts, recomp):
    os.makedirs(d, exist_ok=True)
    for name, text in scripts:
        p = os.path.join(d, name + ".txt")
        open(p, "w").write("hashlog %s.hash\n" % name + text)
        args = [tracer, rom, p, d] + (["--recomp"] if recomp else [])
        out = subprocess.run(args, capture_output=True, text=True).stdout
        interp = [l for l in out.splitlines() if l.startswith("coverage:")]
        print("  %-10s %-6s %s" % (name, "recomp" if recomp else "interp", interp[-1] if interp else out[-200:]), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--only", default=None)
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "verify"))
    a = ap.parse_args()
    tracer = next(p for p in (os.path.join(ROOT, "emu", "smktrace" + EXE), os.path.join(ROOT, "bin", "win", "smktrace.exe"))
                  if os.path.exists(p))
    scripts = [("bootstrap", open(os.path.join(ROOT, "trace", "bootstrap.txt")).read())]
    scripts += [(k, v) for k, v in load_campaigns().items() if not a.only or k in a.only.split(",")]
    shutil.rmtree(a.work, ignore_errors=True)
    di, dr = os.path.join(a.work, "interp"), os.path.join(a.work, "recomp")
    with ThreadPoolExecutor(2) as ex:
        f1 = ex.submit(run_all, tracer, a.rom, di, scripts, False)
        f2 = ex.submit(run_all, tracer, a.rom, dr, scripts, True)
        f1.result(); f2.result()
    ok = True
    for name, _ in scripts:
        x = open(os.path.join(di, name + ".hash")).read().splitlines()
        y = open(os.path.join(dr, name + ".hash")).read().splitlines()
        bad = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), None)
        if bad is None and len(x) == len(y):
            print("OK    %-10s %d frames identical" % (name, len(x)))
        else:
            ok = False
            print("DIFF  %-10s first difference at frame %s:\n      interp %s\n      recomp %s" % (
                name, x[bad].split()[0] if bad is not None else "end", x[bad] if bad is not None else "", y[bad] if bad is not None else ""))
    print("recompiled build matches the interpreter" if ok else "MISMATCH")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
