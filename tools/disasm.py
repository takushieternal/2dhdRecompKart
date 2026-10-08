#!/usr/bin/env python3
"""Coverage-guided disassembler for Super Mario Kart (USA), HiROM 512 KiB.

    disasm.py <rom.sfc> <tracedir> <outdir> [--symbols symbols.txt]

Inputs from smktrace (tracedir): cov.bin, covctx.bin, edges.txt.
Output: asar-compatible source (smk.asm + bank_XX.asm) that rebuilds the ROM byte-for-byte.

Strategy
  1. Every instruction the tracer saw executed is code, with the M/X widths it ran with.
  2. Recursive descent extends that into branches/calls that were never taken, carrying M/X.
     A speculative path is thrown away if it runs into data the game read, overlaps another
     instruction, or hits an opcode that is almost never real code (BRK/COP/WDM/STP).
  3. Everything else is emitted as `db` data, with labels wherever code points into it.
  4. Instructions are emitted with explicit .b/.w/.l sizes and operand expressions that evaluate
     to the original bytes, so the build is byte-identical by construction (and verified by build.py).

Layout: each 64 KiB bank is emitted as two halves so labels carry the address the CPU actually
uses: $0000-$7FFF at $C0-$C7 (data only reachable there) and $8000-$FFFF at $80-$87 (FastROM code).
"""
import sys, os, struct, argparse, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from op65816 import OPS, operand_len, fmt_operand

ROMSZ = 0x80000
C_OP_M1, C_OP_M0, C_OP_X1, C_OP_X0, C_OPERAND, C_DATA, C_EMU = 1, 2, 4, 8, 16, 32, 64

TERMINAL = {"RTS", "RTL", "RTI", "JMP", "JML", "BRA", "BRL", "STP"}
SUSPECT = {0x00, 0x02, 0x42, 0xDB}       # BRK COP WDM STP
DATA_MODES = {"dp", "dpx", "dpy", "idp", "idx", "idy", "idl", "ily", "abs", "abx", "aby", "abl", "alx"}


def label_addr(off):
    """Address a label at this ROM offset gets (see Layout)."""
    bank, lo = off >> 16, off & 0xFFFF
    return ((0x80 + bank) << 16 | lo) if lo >= 0x8000 else ((0xC0 + bank) << 16 | lo)


def rom_off(addr):
    """Map a 24-bit CPU address to a ROM offset (HiROM), or None."""
    bank, lo = (addr >> 16) & 0xFF, addr & 0xFFFF
    b = bank & 0x7F
    if b in (0x7E, 0x7F):
        return None
    if lo >= 0x8000 or b >= 0x40:
        return ((b & 0x3F) << 16 | lo) % ROMSZ
    return None


class Disasm:
    def __init__(self, rom, cov, db, dp, multi, seen, edges, symbols):
        self.rom, self.cov, self.db, self.dp, self.multi, self.seen = rom, cov, db, dp, multi, seen
        self.edges = edges
        self.symbols = symbols              # off -> name (user names)
        self.ins = {}                       # off -> (len, m8, x8, source) source: 'exec'|'static'
        self.owner = [-1] * ROMSZ           # byte -> instruction start
        self.labels = {}                    # off -> kind ('code'|'data'|'jtab')
        self.raw = set()                    # instruction starts emitted as raw bytes (overlaps etc.)
        self.notes = collections.defaultdict(list)
        self.words = {}                     # off -> target off, emitted as `dw label`

    def decode(self, off, m8, x8):
        op = self.rom[off]
        l = operand_len(op, m8, x8)
        v = 0
        for i in range(l):
            v |= self.rom[(off + 1 + i) % ROMSZ] << (8 * i)
        return op, l, v

    # ---------------------------------------------------------- phase 1: executed code
    def place_executed(self):
        for off in range(ROMSZ):
            f = self.cov[off]
            if not f & 15:
                continue
            m_both = (f & C_OP_M1) and (f & C_OP_M0)
            x_both = (f & C_OP_X1) and (f & C_OP_X0)
            m8 = bool(f & C_OP_M1) and not (f & C_OP_M0)
            x8 = bool(f & C_OP_X1) and not (f & C_OP_X0)
            op = self.rom[off]
            mode = OPS[op][1]
            if (m_both and mode == "imm") or (x_both and mode == "imx"):
                # executed with both widths: length differs, cannot be one instruction
                self.raw.add(off)
                self.notes[off].append("executed with both %s widths" % ("M" if mode == "imm" else "X"))
                m8 = True if mode == "imm" else m8
                x8 = True if mode == "imx" else x8
            op, l, v = self.decode(off, m8, x8)
            self.ins[off] = (l + 1, m8, x8, "exec")
        # ownership & overlap detection
        for off in sorted(self.ins):
            ln = self.ins[off][0]
            for i in range(ln):
                o = off + i
                if o >= ROMSZ:
                    self.raw.add(off); break
                if self.owner[o] != -1 and self.owner[o] != off:
                    self.raw.add(off); self.raw.add(self.owner[o])
                else:
                    self.owner[o] = off

    # ---------------------------------------------------------- phase 2: static extension
    def targets(self, off, op, l, v):
        """(code targets, falls_through) for an instruction at off."""
        mn, mode = OPS[op]
        bank = off & 0x70000
        nxt = off + 1 + l
        out = []
        if mode == "rel":
            out.append(bank | ((nxt + (v - 256 if v & 0x80 else v)) & 0xFFFF))
        elif mode == "rll" and mn == "BRL":
            out.append(bank | ((nxt + (v - 65536 if v & 0x8000 else v)) & 0xFFFF))
        elif mn in ("JSR", "JMP") and mode == "abs" and v >= 0x8000:
            out.append(bank | v)
        elif mn in ("JSL", "JML") and mode == "abl":
            t = rom_off(v)
            if t is not None:
                out.append(t)
        falls = mn not in TERMINAL
        return out, falls

    def try_path(self, start, m8, x8, accepted):
        """Speculatively decode from start; return list of new instructions or None."""
        new = []
        claimed = set()
        work = [(start, m8, x8)]
        seen = set()
        while work:
            off, m8, x8 = work.pop()
            while True:
                if off >= ROMSZ or off in seen:
                    break
                if off in self.ins:           # joined known code
                    break
                if self.owner[off] != -1 or off in claimed:
                    return None               # misaligned with known code
                op = self.rom[off]
                if op in SUSPECT:
                    return None
                op, l, v = self.decode(off, m8, x8)
                for i in range(l + 1):
                    o = off + i
                    if o >= ROMSZ or (self.cov[o] & C_DATA) or self.owner[o] != -1 or o in claimed:
                        return None
                seen.add(off)
                for i in range(l + 1):
                    claimed.add(off + i)
                new.append((off, l + 1, m8, x8))
                if len(new) > 4000:
                    return None
                mn = OPS[op][0]
                tg, falls = self.targets(off, op, l, v)
                for t in tg:
                    if t not in self.ins and (t & 0xFFFF) >= 0x8000:
                        work.append((t, m8, x8))
                if mn == "REP":
                    m8 = m8 and not (v & 0x20); x8 = x8 and not (v & 0x10)
                elif mn == "SEP":
                    m8 = m8 or bool(v & 0x20); x8 = x8 or bool(v & 0x10)
                elif mn in ("PLP", "RTI", "XCE"):
                    return new if new else None   # flags unknown afterwards: stop here, keep path
                if not falls:
                    break
                off = off + 1 + l
        return new

    def find_jump_tables(self):
        """Tables used by executed JMP/JSR (abs,X): emit as dw and seed their targets."""
        seeds = []
        observed = collections.defaultdict(set)
        for (frm, to, op) in self.edges:
            if op in (0x7C, 0xFC):
                f, t = rom_off(frm), rom_off(to)
                if f is not None and t is not None:
                    observed[f].add(t)
        for off, (ln, m8, x8, src) in list(self.ins.items()):
            op = self.rom[off]
            if op not in (0x7C, 0xFC) or src != "exec" or off in self.raw:
                continue
            v = self.rom[off + 1] | self.rom[off + 2] << 8
            if v < 0x8000:
                continue
            bank = off & 0x70000
            base = bank | v
            obs = observed.get(off, set())
            # find furthest entry index that matches an observed target
            last = -1
            for i in range(256):
                e = base + 2 * i
                if e + 1 >= ROMSZ or (e & 0xFFFF) < 0x8000 and i > 0:
                    break
                w = self.rom[e] | self.rom[e + 1] << 8
                if (bank | w) in obs:
                    last = i
            if last < 0:
                continue
            n = last + 1
            # extend past the last observed entry while entries still look like code pointers
            while n < 256:
                e = base + 2 * n
                if e + 1 >= ROMSZ or self.owner[e] != -1 or self.owner[e + 1] != -1 or (self.cov[e] & 15):
                    break
                w = self.rom[e] | self.rom[e + 1] << 8
                t = bank | w
                if w < 0x8000 or self.rom[t] in SUSPECT or (self.cov[t] & C_DATA) or \
                        (self.owner[t] != -1 and t not in self.ins):
                    break
                n += 1
            ok = True
            for i in range(n):
                e = base + 2 * i
                if self.owner[e] != -1 or self.owner[e + 1] != -1:
                    ok = False; break
            if not ok:
                continue
            for i in range(n):
                e = base + 2 * i
                w = self.rom[e] | self.rom[e + 1] << 8
                t = bank | w
                self.words[e] = t
                if t not in self.ins:
                    seeds.append((t, m8, x8))
        return seeds

    def extend_static(self, extra=()):
        added = 0
        for rnd in range(20):
            frontier = list(extra) if rnd == 0 else []
            for off in sorted(self.ins):
                if off in self.raw:
                    continue
                ln, m8, x8, src = self.ins[off]
                op, l, v = self.decode(off, m8, x8)
                tg, falls = self.targets(off, op, l, v)
                mn = OPS[op][0]
                # flags after this instruction (for fall-through)
                fm8, fx8 = m8, x8
                if mn == "REP":
                    fm8 = m8 and not (v & 0x20); fx8 = x8 and not (v & 0x10)
                elif mn == "SEP":
                    fm8 = m8 or bool(v & 0x20); fx8 = x8 or bool(v & 0x10)
                for t in tg:
                    if t not in self.ins and self.owner[t] == -1 and (t & 0xFFFF) >= 0x8000:
                        frontier.append((t, m8, x8))
                nxt = off + ln
                if falls and mn not in ("PLP", "RTI", "XCE", "JSL", "JSR", "BRK", "COP") and nxt < ROMSZ \
                        and nxt not in self.ins and self.owner[nxt] == -1:
                    frontier.append((nxt, fm8, fx8))
            if not frontier:
                break
            round_added = 0
            for (t, m8, x8) in frontier:
                if t in self.ins or self.owner[t] != -1:
                    continue
                path = self.try_path(t, m8, x8, None)
                if not path:
                    continue
                for (o, ln, pm8, px8) in path:
                    if o in self.ins:
                        continue
                    self.ins[o] = (ln, pm8, px8, "static")
                    for i in range(ln):
                        self.owner[o + i] = o
                    round_added += 1
            added += round_added
            if not round_added:
                break
        return added

    # ---------------------------------------------------------- phase 3: references
    def operand_ref(self, off):
        """Return (target_off, kind, value, width) for an instruction operand that points into ROM."""
        ln, m8, x8, src = self.ins[off]
        op, l, v = self.decode(off, m8, x8)
        mn, mode = OPS[op]
        bank = off & 0x70000
        nxt = off + 1 + l
        if mode == "rel":
            return (bank | ((nxt + (v - 256 if v & 0x80 else v)) & 0xFFFF), "code")
        if mode == "rll":
            return (bank | ((nxt + (v - 65536 if v & 0x8000 else v)) & 0xFFFF), "code" if mn == "BRL" else "data")
        if mn in ("JSR", "JMP") and mode == "abs":
            return ((bank | v), "code") if v >= 0x8000 else None
        if mn in ("JSR", "JMP") and mode == "iax":
            return ((bank | v), "jtab") if v >= 0x8000 else None
        if mode in ("abl", "alx"):
            t = rom_off(v)
            if t is None:
                return None
            return (t, "code" if mn in ("JSL", "JML") else "data")
        if mode in ("abs", "abx", "aby") and mn != "PEA" and src == "exec" and self.seen[off] and not (self.multi[off] & 1):
            t = rom_off((self.db[off] << 16) | v)
            if t is not None:
                return (t, "data")
        return None

    def collect_labels(self):
        for off in self.ins:
            if off in self.raw:
                continue
            r = self.operand_ref(off)
            if r:
                t, kind = r
                if kind == "code" and t not in self.ins:
                    kind = "data"   # target not decoded as code: still label it, as data
                self.labels.setdefault(t, kind)
        # observed indirect-jump / return targets
        for (frm, to, op) in self.edges:
            t = rom_off(to)
            if t is not None and t in self.ins and op in (0x6C, 0x7C, 0xDC, 0xFC):
                self.labels.setdefault(t, "code")
        for e, t in self.words.items():
            self.labels.setdefault(t, "code" if t in self.ins else "data")
        for off in self.symbols:
            self.labels.setdefault(off, "code" if off in self.ins else "data")

    def name(self, off):
        if off in self.symbols:
            return self.symbols[off]
        kind = self.labels.get(off, "data")
        pre = {"code": "CODE", "jtab": "JTAB", "data": "DATA"}[kind]
        return "%s_%06X" % (pre, label_addr(off))

    # ---------------------------------------------------------- phase 4: emission
    def emit_ins(self, off):
        ln, m8, x8, src = self.ins[off]
        op, l, v = self.decode(off, m8, x8)
        mn, mode = OPS[op]
        ref = self.operand_ref(off)
        size = {0: "", 1: ".b", 2: ".w", 3: ".l"}[l] if mode not in ("rel", "rll", "bm", "acc", "imp", "sr", "isy", "im8") else ""
        if mode in ("sr", "isy"):
            size = ".b"
        if mode == "im8":
            size = ""
        lab_ok = ref is not None and ref[0] in self.labels \
            and (ref[0] in self.ins or ref[0] not in self.owner_mid)

        def sym(addr, kind):
            if not lab_ok:
                return None
            t = ref[0]
            name = self.name(t)
            la = label_addr(t)
            if mode in ("rel", "rll"):
                return name
            if l == 3:
                delta = v - la
                return name if delta == 0 else "%s%+d" % (name, delta) if abs(delta) < 0x100 else \
                    "%s%s$%06X" % (name, "+" if delta > 0 else "-", abs(delta))
            # 16-bit operand: asar takes the low 16 bits of the label
            if (la & 0xFFFF) != v:
                return None
            return name
        if mode in ("rel", "rll") and not lab_ok:
            return None   # caller emits raw bytes
        operand = fmt_operand(mode, v, l, (off & 0xFF0000) | ((off + 1 + l) & 0xFFFF), sym)
        text = (mn + size + (" " + operand if operand else "")).strip()
        if mn in ("BRK", "COP", "WDM") and not operand.startswith("#"):
            text = "%s #$%02X" % (mn, v)
        return text

    def emit_bank(self, bank, out):
        start, end = bank << 16, (bank + 1) << 16
        w = out.write
        w("; ---------------------------------------------------------------------------\n")
        w("; ROM bank %d  (file $%06X-$%06X)\n" % (bank, start, end - 1))
        w("; ---------------------------------------------------------------------------\n\n")
        off = start
        pend = []   # pending db bytes

        def flush():
            for i in range(0, len(pend), 16):
                w("\tdb " + ",".join("$%02X" % b for b in pend[i:i + 16]) + "\n")
            pend.clear()
        while off < end:
            if off & 0xFFFF == 0x0000:
                flush(); w("\norg $%06X\n" % (0xC00000 | off))
            if off & 0xFFFF == 0x8000:
                flush(); w("\norg $%06X\n" % (0x800000 | off))
            is_ins = off in self.ins and off not in self.raw
            text = self.emit_ins(off) if is_ins else None
            if is_ins and text is None:
                is_ins = False
                self.raw.add(off)
            if off in self.labels or (is_ins and off in self.ins and self.ins[off][3] == "exec" and self.is_entry(off)):
                flush()
                self.labels.setdefault(off, "code" if is_ins else "data")
                w("%s:\n" % self.name(off))
            if not is_ins and off in self.words and off + 1 < end and (off + 1) not in self.labels \
                    and self.words[off] in self.labels:
                flush()
                w("\tdw %s\n" % self.name(self.words[off]))
                off += 2
                continue
            if is_ins:
                flush()
                ln, m8, x8, src = self.ins[off]
                if off + ln > end or any((off + i) in self.labels for i in range(1, ln)):
                    # crosses a bank or a label lands mid-instruction: raw bytes
                    self.raw.add(off)
                    pend.append(self.rom[off]); off += 1
                    continue
                cmt = []
                if src == "static":
                    cmt.append("s")
                if off in self.notes:
                    cmt += self.notes[off]
                bytes_ = " ".join("%02X" % self.rom[off + i] for i in range(ln))
                f = self.cov[off]
                ms = "M*" if (f & 3) == 3 else ("M8" if m8 else "M16")
                xs = "X*" if (f & 12) == 12 else ("X8" if x8 else "X16")
                w("\t%-28s; %06X %s %s%s\n" % (text, label_addr(off), ms, xs,
                                              (" " + " ".join(cmt)) if cmt else ""))
                off += ln
            else:
                pend.append(self.rom[off])
                off += 1
                if len(pend) >= 16:
                    flush()
        flush()

    def is_entry(self, off):
        return False

    def run(self, outdir):
        self.place_executed()
        n_exec = len(self.ins)
        seeds = self.find_jump_tables()
        n_static = self.extend_static(seeds)
        self.collect_labels()
        # bytes that are inside an instruction (not its first byte): labels can't land there
        self.owner_mid = set()
        for o, (ln, m8, x8, src) in self.ins.items():
            for i in range(1, ln):
                self.owner_mid.add(o + i)
        # labels that land mid-instruction force the instruction to raw bytes (handled in emit)
        os.makedirs(outdir, exist_ok=True)
        for bank in range(ROMSZ >> 16):
            with open(os.path.join(outdir, "bank_%02X.asm" % (0x80 + bank)), "w") as f:
                self.emit_bank(bank, f)
        with open(os.path.join(outdir, "smk.asm"), "w") as f:
            f.write("; Super Mario Kart (USA) - coverage-guided disassembly\n")
            f.write("; Generated by tools/disasm.py. Build with tools/build.py (asar).\n")
            f.write("; Comment columns: CPU address, register widths at that point; 's' = statically inferred code.\n\n")
            f.write("hirom\n\n")
            for bank in range(ROMSZ >> 16):
                f.write('incsrc "bank_%02X.asm"\n' % (0x80 + bank))
        code_bytes = sum(ln for (ln, *_r) in self.ins.values())
        print("instructions: %d executed + %d static; %d labels; %d raw; code bytes %d (%.1f%% of ROM)" % (
            n_exec, n_static, len(self.labels), len(self.raw), code_bytes, 100.0 * code_bytes / ROMSZ))


def load_symbols(path):
    syms = {}
    if path and os.path.exists(path):
        for line in open(path):
            line = line.split(";")[0].strip()
            if not line:
                continue
            addr, name = line.split()[:2]
            t = rom_off(int(addr, 16))
            if t is not None:
                syms[t] = name
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("trace"); ap.add_argument("out")
    ap.add_argument("--symbols", default=None)
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    assert len(rom) == ROMSZ, "expected a 512 KiB unheadered ROM"
    cov = open(os.path.join(a.trace, "cov.bin"), "rb").read()
    ctx = open(os.path.join(a.trace, "covctx.bin"), "rb").read()
    db = ctx[:ROMSZ]
    dp = struct.unpack("<%dH" % ROMSZ, ctx[ROMSZ:ROMSZ * 3])
    multi = ctx[ROMSZ * 3:ROMSZ * 4]
    seen = ctx[ROMSZ * 4:ROMSZ * 5]
    edges = set()
    ep = os.path.join(a.trace, "edges.txt")
    if os.path.exists(ep):
        for line in open(ep):
            p = line.split()
            if len(p) == 3:
                edges.add((int(p[0], 16), int(p[1], 16), int(p[2], 16)))
    Disasm(rom, cov, db, dp, multi, seen, sorted(edges), load_symbols(a.symbols)).run(a.out)


if __name__ == "__main__":
    main()
