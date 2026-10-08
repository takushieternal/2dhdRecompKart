#!/usr/bin/env python3
"""Run the coverage campaigns with smktrace, then regenerate and verify the disassembly.

    python trace/run_coverage.py path/to/smk.sfc [--out trace/out] [--only tt,battle,...] [--skip-disasm]

Coverage accumulates in the output directory across runs (delete it to start over).
"""
import argparse, os, random, subprocess, sys, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = ".exe" if os.name == "nt" else ""


def drive(frames, seed):
    """Random 'driving': gas held, random steering/hops/items/rear-view."""
    r = random.Random(seed); out = []; left = frames
    while left > 0:
        n = r.randint(10, 60)
        st = r.choice(["", "+Left", "+Right", "", "+Left+R", "+Right+R"])
        ex = r.choice(["", "", "", "+A", "+L", "+Y", "+X"])
        out.append("%d B%s%s" % (n, st, ex)); left -= n
    return "\n".join(out) + "\n"


def drive2(frames, seed):
    r = random.Random(seed); out = []; left = frames
    def one():
        return "B" + r.choice(["", "+Left", "+Right", "", "+Left+R", "+Right+R"]) + r.choice(["", "", "+A", "+L", "+Y", "+X"])
    while left > 0:
        n = r.randint(10, 60); out.append("both %d %s %s" % (n, one(), one())); left -= n
    return "\n".join(out) + "\n"


def gp_cup(cc, cup, tag):
    """1P GP from power-on/soft reset. Each race is won by poking the lap counter during the
    countdown (see docs/NOTES.md), so whole cups, podiums and unlocks get exercised."""
    s = "reset\n400\ntap Start\n60\ntap Up\n30\ntap B\n60\ntap B\n60\n"
    s += "tap Down\n30\n" * cc
    s += "shot %s_cc\ntap B\n60\ntap B\n120\ntap B\n30\ntap B\n240\n" % tag
    s += "tap Down\n30\n" * cup
    s += "shot %s_cup\ntap B\n" % tag
    for r in range(5):
        s += "untiltap 3a 04 4000 B\npoke 10c1 84\npoke 10f9 84\nuntil 3a 06 3000 B\n120 B\nshot %s_r%d\n" % (tag, r)
    s += "300\ntap B\n1500\nshot %s_pod\n1500\nshot %s_pod2\nuntiltap 3a 04 3000 B\nshot %s_end\n" % (tag, tag, tag)
    return s


def gp_cup_2p(cc, cup, tag):
    """2P GP. Both karts get the lap poke, and pad 2 holds gas during races so it crosses the line too."""
    s = "reset\nsramload unlocked\n400\ntap Start\n60\ntap B\n60\ntap B\n60\n"
    s += "tap Down\n30\n" * cc
    s += "shot %s_cc\ntap B\n60\ntap B\n120\ntap B\np2 6 B\n30\np2 6 B\n60\ntap B\n240\n" % tag
    s += "tap Down\n30\n" * cup
    s += "shot %s_cup\ntap B\n" % tag
    for r in range(5):
        s += ("untiltap 3a 04 4000 B\np2hold B\npoke 10c1 84\npoke 10f9 84\npoke 11c1 84\npoke 11f9 84\n"
              "until 10c1 85 3000 B\nuntil 11c1 85 3000 B\np2hold -\n120\nshot %s_r%d\n" % (tag, r))
    s += "300\ntap B\n1500\nshot %s_pod\n1500\nshot %s_pod2\n" % (tag, tag) + "tap B\n240\n" * 10 + "shot %s_end\n" % tag
    return s


def campaigns():
    c = {}
    s = ""
    for cup in range(3):
        for trk in range(5):
            s += "load tt_course\ntap Left\n30\n" + "tap Down\n30\n" * cup + "tap B\n60\n" + "tap Down\n30\n" * trk
            s += "tap B\n120\ntap B\n200\nshot tt%d%d\n" % (cup, trk) + drive(2500, cup * 10 + trk)
    c["tt"] = s
    s = ""
    for k in range(4):
        s += "load battle_sel\n120\ntap B\n120\n" + "tap Down\n30\n" * k + "tap B\n60\ntap B\n300\n"
        s += "shot bt%d\n" % k + drive2(5000, 100 + k)
    c["battle"] = s
    c["gp"] = "".join("load gp_start\n" + drive(6000, 200 + i) + "shot gp%d\n" % i for i in range(3))
    # 50cc and 100cc golds in all three cups (SRAM persists across soft resets within one run)
    # 100cc golds in the first three cups unlock the Special Cup; winning it unlocks 150cc.
    order = [(0, k) for k in range(3)] + [(1, k) for k in range(4)] + [(2, k) for k in range(4)]
    c["gpfull"] = "".join(gp_cup(cc, cup, "g%d%d" % (cc, cup)) for cc, cup in order) + "sramsave unlocked\n"
    # needs the SRAM saved by gpfull: Special Cup time trials, and 2P match race
    s = ""
    for trk in range(5):
        s += "reset\nsramload unlocked\n400\ntap Start\n60\ntap Up\n30\ntap B\n60\ntap Down\n30\ntap B\n60\ntap B\n60\ntap B\n150\ntap B\n30\ntap B\n240\n"
        s += "tap Left\n30\n" + "tap Down\n30\n" * 3 + "tap B\n60\n" + "tap Down\n30\n" * trk
        s += "tap B\n120\ntap B\n200\nshot sp%d\n" % trk + drive(2000, 300 + trk)
    s += ("reset\nsramload unlocked\n400\ntap Start\n60\ntap B\n60\ntap Down\n20\ntap B\n60\ntap B\n60\ntap B\n120\n"
          "tap B\np2 6 B\n30\np2 6 B\n60\ntap B\n240\ntap B\n120\ntap B\n120\ntap B\n300\nshot mr\n") + drive2(4000, 400)
    c["extra"] = s
    # 2P Grand Prix: the three base cups at 50cc, the Special Cup at 100cc and 150cc
    # (the Special Cup is only listed from 100cc up)
    c["gp2p"] = "".join(gp_cup_2p(0, k, "h0%d" % k) for k in range(3)) + gp_cup_2p(1, 3, "h13") + gp_cup_2p(2, 3, "h23")
    c["demo"] = "load title\n6000\nshot demo1\n6000\nshot demo2\n"
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--only", default=None)
    ap.add_argument("--skip-disasm", action="store_true")
    a = ap.parse_args()
    cands = [os.path.join(ROOT, "bin", "win", "smktrace.exe")] if os.name == "nt" else []
    cands += [os.path.join(ROOT, "emu", "smktrace" + EXE), os.path.join(ROOT, "bin", "smktrace" + EXE)]
    tracer = next((c for c in cands if os.path.exists(c)), None)
    if not tracer:
        sys.exit("smktrace not found: build it with `make -C emu` (or use bin/win/smktrace.exe on Windows)")
    os.makedirs(a.out, exist_ok=True)

    def run(script_text, name):
        p = os.path.join(a.out, name + ".txt")
        open(p, "w").write(script_text)
        print("== %s" % name, flush=True)
        subprocess.check_call([tracer, a.rom, p, a.out])

    if not os.path.exists(os.path.join(a.out, "gp_start.state")):
        run(open(os.path.join(HERE, "bootstrap.txt")).read(), "bootstrap")
    for name, text in campaigns().items():
        if a.only and name not in a.only.split(","):
            continue
        run(text, "camp_" + name)
    if not a.skip_disasm:
        asm = os.path.join(ROOT, "asm")
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "disasm.py"), a.rom, a.out, asm,
                               "--symbols", os.path.join(ROOT, "symbols.txt")])
        asar = [p for p in (os.path.join(ROOT, "bin", "win", "asar.exe") if os.name == "nt" else "",
                            os.path.join(ROOT, "bin", "asar")) if p and os.path.exists(p)]
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "build.py"), asm,
                               os.path.join(ROOT, "build", "smk_rebuilt.sfc"), "--verify", a.rom]
                              + (["--asar", asar[0]] if asar else []))


if __name__ == "__main__":
    main()
